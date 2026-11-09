#include "trajectory.hpp"

#include <cmath>

#include "checkpoint/player_state.hpp"
#include "engine/engine.hpp"
#include "engine/timeline.hpp"
#include "physics/collisions.hpp"
#include "physics/object.hpp"
#include "util/crash_log.hpp"
#include "vine/session.hpp"
#ifdef GEODE_IS_MOBILE
#include "engine/tick_patch.hpp"
#include "engine/tick_plan.hpp"
#endif

using namespace geode::prelude;
using Mode = Trajectory::TrajectoryMode;

namespace {

constexpr float MERGE_POINTS = 1.0f;
constexpr ccColor4F CLEAR{0, 0, 0, 0};

ccColor4F rgba(const float* channels) { return {channels[0], channels[1], channels[2], channels[3]}; }

bool jumpHeld(PlayerObject* player) {
    const auto held = player->m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
    return held != player->m_holdingButtons.end() && held->second;
}

void press(PlayerObject* player, PlayerButton button, bool down) {
    if (down)
        player->pushButton(button);
    else
        player->releaseButton(button);
}

void setJump(PlayerObject* player, bool down) {
    press(player, PlayerButton::Jump, down);
    if (!down) player->m_jumpBuffered = false;
}

CCRect inset(const CCRect& rect, float by) {
    return {rect.origin.x + by, rect.origin.y + by, rect.size.width - by * 2.f, rect.size.height - by * 2.f};
}

uint64_t stateBits(PlayerObject* player) {
    const bool bits[] = {
        player->m_isShip,         player->m_isBird,       player->m_isDart,         player->m_isSwing,
        player->m_isBall,         player->m_isSpider,     player->m_isRobot,        player->m_isUpsideDown,
        player->m_isDead,         player->m_isOnGround2,  player->m_isSideways,     player->m_isDashing,
        player->m_isAccelerating, player->m_holdingLeft,  player->m_holdingRight,   player->m_jumpBuffered,
        player->m_isPlatformer,   player->m_isGoingLeft,  player->m_maybeIsBoosted, jumpHeld(player),
    };
    uint64_t packed = 0;
    for (bool bit : bits) packed = packed << 1 | bit;
    return packed;
}

struct Fnv {
    uint64_t m_value = 14695981039346656037ull;

    template <class T>
    void add(const T& data) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&data);
        for (size_t i = 0; i < sizeof(T); ++i) m_value = (m_value ^ bytes[i]) * 1099511628211ull;
    }
};

size_t lookHash() {
    const auto& look = GrapeSettings::get()->trajectory;
    Fnv hash;
    for (const auto& [key, category] : look.categories) {
        hash.add(key);
        hash.add(category.enabled);
        hash.add(category.colors);
        hash.add(category.player2Colors);
    }
    hash.add(look.straightEnabled);
    hash.add(look.straightColor);
    hash.add(look.straightPlayer2Color);
    hash.add(look.orbSkipEnabled);
    hash.add(look.orbSkipColor);
    hash.add(look.orbSkipPlayer2Color);
    return static_cast<size_t>(hash.m_value);
}

#ifdef GEODE_IS_MOBILE
double predictionTps(double actual, double overridden = 0.0) {
    if (std::isfinite(overridden) && overridden > 0.0) return overridden;
    const bool usable = grape::ticks::patched() && std::isfinite(actual) && actual > 0.0;
    return usable ? actual : 240.0;
}
#endif

}

class Trajectory::Pen {
   public:
    Pen(CCDrawNode* node, float width, float zoom, const float* color)
        : m_node(node), m_width(width), m_color(rgba(color)), m_mergeSq(MERGE_POINTS * MERGE_POINTS / (zoom * zoom)) {}

    void dot(const CCPoint& at) {
        if (m_node) m_node->drawSegment(at, at, m_width, m_color);
    }

    void line(const CCPoint& from, const CCPoint& to) {
        if (!m_node) return;
        if (!m_open || !from.equals(m_tip)) {
            flush();
            m_anchor = from;
            m_open = true;
        }
        m_tip = to;
        if (m_anchor.getDistanceSq(m_tip) >= m_mergeSq) flush();
    }

    void flush() {
        if (!m_open || m_anchor.equals(m_tip)) return;
        m_node->drawSegment(m_anchor, m_tip, m_width, m_color);
        m_anchor = m_tip;
    }

   private:
    CCDrawNode* m_node;
    float m_width;
    ccColor4F m_color;
    float m_mergeSq;
    bool m_open = false;
    CCPoint m_anchor;
    CCPoint m_tip;
};

TrajectoryDrawNode* TrajectoryDrawNode::create() {
    auto* node = new TrajectoryDrawNode();
    if (!node->init()) {
        delete node;
        return nullptr;
    }
    node->autorelease();
    node->m_bUseArea = false;
    return node;
}

Trajectory* Trajectory::create(GJBaseGameLayer* layer) {
    CCNode* host = layer && layer->m_debugDrawNode ? layer->m_debugDrawNode->getParent() : nullptr;
    if (!host) return nullptr;

    std::unique_ptr<Trajectory> made(new Trajectory());
    made->m_layer = layer;
    made->m_node = TrajectoryDrawNode::create();
    if (!made->m_node) return nullptr;
    made->m_node->retain();
    made->m_node->setID("trajectory-node"_spr);
    made->m_node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});

    const std::string ids[] = {"trajectory-fake-player1"_spr, "trajectory-fake-player2"_spr};
    for (int slot = 0; slot < 2; ++slot) {
        PlayerObject* fake = PlayerObject::create(1, 1, layer, layer, true);
        if (!fake) return nullptr;
        fake->retain();
        made->m_fake[slot] = fake;
        fake->setPosition({0, 105});
        fake->setVisible(false);
        fake->setID(ids[slot]);
#ifndef GEODE_IS_MOBILE
        layer->m_objectLayer->addChild(fake);
#endif
    }

    host->addChild(made->m_node, (layer->m_uiLayer ? layer->m_uiLayer->getZOrder() : 0) + 10000);
    return made.release();
}

Trajectory::~Trajectory() {
    for (PlayerObject* fake : m_fake) {
        if (!fake) continue;
        fake->removeFromParentAndCleanup(true);
        fake->release();
    }
    if (m_node) {
        m_node->removeFromParentAndCleanup(true);
        m_node->release();
    }
}

int Trajectory::slotOf(PlayerObject* player) const {
    const auto found = std::ranges::find(m_fake, player);
    return found == m_fake.end() ? -1 : static_cast<int>(found - m_fake.begin());
}

bool Trajectory::hasDied(PlayerObject* player) {
    const int slot = slotOf(player);
    if (slot < 0) return false;
    m_dead[slot] = true;
    return true;
}

void Trajectory::rememberActivatedObject(EnhancedGameObject* object, PlayerObject* player) {
    if (const int slot = slotOf(player); slot >= 0) m_used[slot].insert(reinterpret_cast<uintptr_t>(object));
}

bool Trajectory::playerHasActivated(PlayerObject* player, EnhancedGameObject* object) {
    if (!object || object->m_isMultiActivate) return false;
    const int slot = slotOf(player);
    return slot >= 0 && m_used[slot].contains(reinterpret_cast<uintptr_t>(object));
}

bool Trajectory::realPlayerHasActivated(PlayerObject* player, EnhancedGameObject* object) {
    if (!object) return false;
    const int slot = slotOf(player);
    if (slot < 0) return phys::hasBeenActivatedByPlayer(player, object);
    PlayerObject* real = slot == 0 ? m_layer->m_player1 : m_layer->m_player2;
    return real && phys::hasBeenActivatedByPlayer(real, object);
}

int Trajectory::getPredictionLength(GJBaseGameLayer* layer) {
    if (!layer) return 0;
    const auto& look = GrapeSettings::get()->trajectory;
    const double tps = GrapeEngine::get()->timeline().getTps();
#ifdef GEODE_IS_MOBILE
    return grape::ticks::predictionSteps(std::clamp(look.length, 0.0, 60.0), predictionTps(tps),
                                         layer->m_gameState.m_timeWarp, look.maxSteps);
#else
    const int steps = static_cast<int>(look.length * std::max(tps, 240.0) / layer->m_gameState.m_timeWarp);
    return look.maxSteps > 0 ? std::min(steps, look.maxSteps) : steps;
#endif
}

float Trajectory::lineWidth(GJBaseGameLayer* layer) const {
    return m_state->m_width->inner() / layer->m_gameState.m_cameraZoom;
}

Trajectory::Snapshot Trajectory::snapshot(GJBaseGameLayer* layer) {
    Snapshot now;
    now.m_frame = static_cast<uint32_t>(GrapeEngine::get()->timeline().getFrame());

    const std::array<PlayerObject*, 2> real{layer->m_player1, layer->m_player2};
    for (int slot = 0; slot < 2; ++slot) {
        PlayerObject* player = real[slot];
        if (!player) continue;
        const CCPoint at = player->getPosition();
        now.m_motion[slot] = {at.x,
                              at.y,
                              player->getRotation(),
                              static_cast<float>(player->m_yVelocity),
                              static_cast<float>(player->m_platformerXVelocity),
                              player->m_gravityMod,
                              player->m_vehicleSize};
        now.m_flags[slot] = stateBits(player);
    }

    now.m_timeWarp = layer->m_gameState.m_timeWarp;
    now.m_zoom = layer->m_gameState.m_cameraZoom;
    now.m_width = static_cast<float>(m_state->m_width->inner());
    now.m_length = static_cast<float>(m_state->m_length->inner());
    now.m_maxSteps = GrapeSettings::get()->trajectory.maxSteps;

    const bool switches[] = {
        m_state->m_enabled->inner(),
        layer->m_gameState.m_isDualMode,
        layer->m_isPlatformer,
        layer->m_levelSettings->m_twoPlayerMode,
        m_holding[0],
        m_holding[1],
        layer->m_player1 != nullptr,
        layer->m_player2 != nullptr,
    };
    for (bool on : switches) now.m_switches = now.m_switches << 1 | on;
    now.m_colors = lookHash();
    return now;
}

bool Trajectory::advance(GJBaseGameLayer* layer, PlayerObject* player, int mode, Pen& pen, int& steps,
                         const PredictionConfig& config) {
    const CCPoint from = player->getPosition();
    auto& timeline = GrapeEngine::get()->timeline();
    auto& state = layer->m_gameState;

#ifdef GEODE_IS_MOBILE
    const double tick = grape::ticks::timestep(predictionTps(timeline.getTps(), config.m_overridenTPS), state.m_timeWarp);
    const float dt = static_cast<float>(tick);
#else
    const float dt = timeline.getPhysicsDt();
#endif
    const float warp = timeline.getTimeWarp();

    state.m_totalTime += dt;
    state.m_bestTime += dt / warp;
    ++state.m_currentProgress;
    state.m_unkUint5 += static_cast<int>(roundf(warp * 1000.0f));
    player->m_totalTime += dt;

    if (const float speed = state.m_timeModRelated; speed != 0.0f) {
        state.m_timeModRelated = 0;
        state.m_timeModRelated2 = 0;
        player->updateTimeMod(speed, true);
    }

    for (CCDictionary* log : {player->m_collisionLogTop, player->m_collisionLogBottom, player->m_collisionLogLeft,
                              player->m_collisionLogRight})
        log->removeAllObjects();

    const bool ended = (m_dead[0] && (mode & Mode::Player1)) || (m_dead[1] && (mode & Mode::Player2));
    if (ended) {
        pen.flush();
        if (config.m_draw && steps > 1) drawHitbox(layer, player);
        return true;
    }

#ifdef GEODE_IS_MOBILE
    const float delta = grape::ticks::stepDelta60(tick);
#else
    const float delta =
        config.m_overridenTPS == 0.0 ? m_delta : static_cast<float>(1.0 / config.m_overridenTPS * 60.0);
#endif

    player->m_playEffects = false;
    player->update(delta);
    player->m_unkUnused3 = player->getRotation();
    player->updateRotation(delta);
    player->m_shipRotation = player->getPosition();

    if (config.m_autoClickOrbs) {
        player->m_stateJumpBuffered = true;
        player->m_stateRingJump2 = true;
    }

    if (layer->checkCollisions(player, delta, false) == 1) hasDied(player);
    phys::checkSpawnObjects(layer, player);
    layer->m_effectManager->postCollisionCheck();

    pen.line(from, player->getPosition());
    ++steps;
    return false;
}

void Trajectory::applyStartingInput(GJBaseGameLayer* layer, PlayerObject* player, int mode) {
    const int click = mode & CLICK_MASK;
    if (click == Mode::Hold || click == Mode::Swift) player->pushButton(PlayerButton::Jump);
    if (click == Mode::Swift) player->releaseButton(PlayerButton::Jump);
    if (click == Mode::Release) setJump(player, false);

    if (!layer->m_levelSettings->m_platformerMode) return;
    const int direction = mode & DIRECTION_MASK;
    const bool left = direction == Mode::Left;
    const bool walking = left || direction == Mode::Right;
    press(player, left ? PlayerButton::Right : PlayerButton::Left, false);
    press(player, left ? PlayerButton::Left : PlayerButton::Right, walking);
}

TrajectoryPlayerData Trajectory::runPrediction(GJBaseGameLayer* layer, PlayerObject* player, PlayerObject* other,
                                               int mode, float* player1Colors, float* player2Colors, bool both,
                                               const PredictionConfig& config) {
    auto* engine = GrapeEngine::get();
    int length = getPredictionLength(layer);
    if (config.m_maxLength > 0) length = std::min(length, config.m_maxLength);

    const int self = slotOf(player);
    const int ownBit = self == 0 ? Mode::Player1 : Mode::Player2;
    const bool dual = both && layer->m_gameState.m_isDualMode;
    const std::array<PlayerObject*, 2> moving{player, other};
    const int movingCount = dual ? 2 : 1;

    CCDrawNode* canvas = config.m_draw ? m_node : nullptr;
    const float width = lineWidth(layer);
    const float zoom = layer->m_gameState.m_cameraZoom;
    const auto penFor = [&](PlayerObject* who) {
        return Pen(canvas, width, zoom, slotOf(who) == 0 ? player1Colors : player2Colors);
    };
    std::array<Pen, 2> pens{penFor(player), penFor(other)};

#ifdef GEODE_IS_MOBILE
    const bool presetInput = true;
#else
    const bool presetInput = !engine->isPlaying();
#endif
    if (presetInput)
        for (int i = 0; i < movingCount; ++i) applyStartingInput(layer, moving[i], mode);
    for (int i = 0; i < movingCount; ++i) pens[i].dot(moving[i]->getPosition());

    auto& replay = engine->replay();
    const auto& inputs = replay.m_inputs.m_events;
    size_t nextInput = replay.m_head.position();
#ifdef GEODE_IS_MOBILE
    const double macroTps = std::max(engine->timeline().getTps(), 1.0);
    const double simTps = predictionTps(macroTps, config.m_overridenTPS);
#endif

    std::array<bool, 2> stopped{};
    std::array<int, 2> steps{};
    int score = 0;
    const CCRect start = player->getObjectRect();
    float lowest = start.getMinY();
    float highest = start.getMaxY();
    bool holdPlanned = (mode & CLICK_MASK) == Mode::Hold;

    for (int i = 0; i < length; ++i) {
        if (engine->isPlaying()) {
#ifdef GEODE_IS_MOBILE
            const uint64_t frame =
                engine->timeline().getFrame() + static_cast<uint64_t>(std::floor(i * macroTps / simTps));
#else
            const uint64_t frame = engine->timeline().getFrame() + i;
#endif
            for (; nextInput < inputs.size() && inputs[nextInput].m_frame <= frame; ++nextInput) {
                const auto& input = inputs[nextInput];
                const auto button = static_cast<PlayerButton>(input.m_type);
                const int owner = input.m_player2 ? 1 : 0;
                press(m_fake[owner], button, input.m_holding);
                if (dual) press(m_fake[1 - owner], button, input.m_holding);
            }
        }

        if (config.m_flipPlan.flipsAt(i) && !engine->isPlaying()) {
            holdPlanned = !holdPlanned;
            for (int k = 0; k < movingCount; ++k) setJump(moving[k], holdPlanned);
        }

        if (!stopped[0] && i > 0 && config.m_doomed && config.m_doomed(i, player)) {
            m_dead[self] = true;
            stopped[0] = true;
        }
        if (!stopped[0]) {
            stopped[0] = advance(layer, player, mode | ownBit, pens[0], steps[0], config);
            const CCRect box = player->getObjectRect();
            lowest = std::min(lowest, box.getMinY());
            highest = std::max(highest, box.getMaxY());
            ++score;
        }
        if (dual && !stopped[1]) stopped[1] = advance(layer, other, mode | Mode::Player2, pens[1], steps[1], config);
    }
    for (Pen& pen : pens) pen.flush();

    return {
        .position = player->getPosition(),
        .hitbox = player->getObjectRect(),
        .innerHitbox = player->getObjectRect(0.3, 0.3),
        .rotation = player->getRotationX(),
        .p1 = (mode & Mode::Player1) != 0,
        .holding = (mode & Mode::Hold) != 0,
        .dead = m_dead[self],
        .score = score,
        .minY = lowest,
        .maxY = highest,
    };
}

TrajectoryPlayerData Trajectory::simulate(GJBaseGameLayer* layer, bool p1, int mode, bool clickBothPlayers,
                                          PredictionConfig config) {
    auto& look = GrapeSettings::get()->trajectory;
    const int self = p1 ? 0 : 1;
    PlayerObject* fake = m_fake[self];
    PlayerObject* otherFake = m_fake[1 - self];
    PlayerObject* real = p1 ? layer->m_player1 : layer->m_player2;
    PlayerObject* otherReal = p1 ? layer->m_player2 : layer->m_player1;

    const int click = mode & CLICK_MASK;
    const bool left = (mode & Mode::Left) != 0;
    const bool right = (mode & Mode::Right) != 0;
    const bool followsHeld = (left && real->m_holdingLeft) || (right && real->m_holdingRight) ||
                             (!left && !right && !real->m_holdingLeft && !real->m_holdingRight);
    const bool opposesHeld = (left && real->m_holdingRight) || (right && real->m_holdingLeft);

    const int platformer = layer->m_isPlatformer ? Mode::Platformer : 0;
    const int keys[] = {mode | platformer, click | Mode::FollowPlayer | platformer,
                        click | Mode::FollowOpposite | platformer};
    const bool shown[] = {look.categories[keys[0]].enabled, look.categories[keys[1]].enabled && followsHeld,
                          look.categories[keys[2]].enabled && opposesHeld};
    const auto pick = std::ranges::find(shown, true) - std::begin(shown);
    if (!config.m_bypassConfig && pick == 3) return {};
    const int colorKey = keys[pick == 3 ? 0 : pick];

    const GJGameState savedState = layer->m_gameState;
#ifndef GEODE_IS_MACOS
    
    
    EffectManagerState savedEffects;
    layer->m_effectManager->saveToState(savedEffects);
#endif

    const auto mirror = [](PlayerObject* copy, PlayerObject* source) {
        copy->copyAttributes(source);
        copy->m_maybeReducedEffects = true;
        grape::snap::PlayerState::read(source).writeTo(copy);
        copy->setPosition(source->m_position);
        copy->setRotation(source->getRotation());
    };
    mirror(fake, real);
    if (clickBothPlayers && otherReal) mirror(otherFake, otherReal);
    m_dead = {};

    const bool holding = real->m_isDart ? jumpHeld(real) : m_holding[self];
    const bool current = holding ? click == Mode::Hold : click == Mode::Release;
    std::array<float, 4> alternate{0.f, 1.f, 1.f, 1.f};
    auto& category = look.categories[colorKey];
    float* first = current ? category.colors.data() : alternate.data();
    float* second = current ? category.player2Colors.data() : alternate.data();
    if (config.m_colors) first = config.m_colors;
    if (config.m_player2Colors) second = config.m_player2Colors;

    const auto result = runPrediction(layer, fake, otherFake, mode, first, second, clickBothPlayers, config);

    fake->setVisible(false);
    otherFake->setVisible(false);
    for (auto& used : m_used) used.clear();
    layer->m_gameState = savedState;
#ifndef GEODE_IS_MACOS
    layer->m_effectManager->loadFromState(savedEffects);
#endif
    return result;
}

void Trajectory::rebuild(GJBaseGameLayer* layer) {
#ifdef GEODE_IS_MOBILE
    crash_log::breadcrumb("Trajectory prediction");
#endif
    m_drawing = true;
    m_node->clear();
    m_delta = GrapeEngine::get()->timeline().getPhysicsDt() * 60.0f;

    auto& look = GrapeSettings::get()->trajectory;
    const int platformer = layer->m_isPlatformer ? Mode::Platformer : 0;

    std::vector<int> modes{Mode::Hold, Mode::Swift, Mode::Release};
    if (layer->m_isPlatformer)
        for (int side : {Mode::Left, Mode::Right})
            for (int click : {Mode::Hold, Mode::Swift, Mode::Release}) modes.push_back(click | side);

    const auto predictFor = [&](bool p1, bool both) {
        PlayerObject* real = p1 ? layer->m_player1 : layer->m_player2;
        const float originX = real->getPositionX();
        float furthestX = originX;

        if (look.orbSkipEnabled && look.categories[Mode::Hold | platformer].enabled)
            simulate(layer, p1, Mode::Hold, both,
                     {.m_bypassConfig = true,
                      .m_colors = look.orbSkipColor.data(),
                      .m_player2Colors = look.orbSkipPlayer2Color.data()});

        for (int mode : modes) {
            const auto result =
                simulate(layer, p1, mode, both, {.m_autoClickOrbs = (mode & CLICK_MASK) == Mode::Hold});
            if (result.score > 0 && std::abs(result.position.x - originX) > std::abs(furthestX - originX))
                furthestX = result.position.x;
        }

        if (real->m_isDart && look.straightEnabled) {
            const CCPoint origin = real->getPosition();
            const auto& color = p1 ? look.straightColor : look.straightPlayer2Color;
            m_node->drawSegment(origin, {furthestX, origin.y}, lineWidth(layer), rgba(color.data()));
        }
    };

    if (layer->m_player1) predictFor(true, !layer->m_levelSettings->m_twoPlayerMode);
    if (layer->m_player2 && layer->m_gameState.m_isDualMode && layer->m_levelSettings->m_twoPlayerMode)
        predictFor(false, false);

    m_drawing = false;
#ifdef GEODE_IS_MOBILE
    crash_log::breadcrumb("Trajectory idle");
#endif
}

void Trajectory::update(GJBaseGameLayer* layer) {
    if (!layer || layer != m_layer) return;
    for (PlayerObject* fake : m_fake) fake->setVisible(false);
    if (auto* editor = LevelEditorLayer::get(); editor && editor->m_playbackMode == PlaybackMode::Not) return;

    const bool enabled = m_state->m_enabled->inner();
    m_node->setVisible(enabled);
    if (!enabled) return;

    const Snapshot now = snapshot(layer);
    if (!m_valid || !(now == m_shown)) {
        rebuild(layer);
        m_shown = now;
        m_valid = true;
    }

    if (!GrapeEngine::get()->timeline().m_layoutMode->inner() || LevelEditorLayer::get()) return;
    const auto rgb = [](const auto& channels) {
        return ccColor3B{static_cast<GLubyte>(channels[0] * 255), static_cast<GLubyte>(channels[1] * 255),
                         static_cast<GLubyte>(channels[2] * 255)};
    };
    const auto* settings = GrapeSettings::get();
    const ccColor3B background = rgb(settings->layoutBgColor);
    const ccColor3B ground = rgb(settings->layoutGroundColor);
    constexpr ccColor3B blue{40, 125, 255};
    const std::pair<int, ccColor3B> layout[] = {
        {1000, background}, {1001, ground}, {1009, ground}, {1013, blue}, {1014, blue},
    };
    for (const auto& [id, color] : layout)
        if (ColorAction* action = layer->m_effectManager->getColorAction(id)) action->m_color = color;
}

void Trajectory::drawHitbox(GJBaseGameLayer* layer, PlayerObject* player) {
    const float width = lineWidth(layer);
    const CCRect outer = inset(player->getObjectRect(), width);
    const CCRect inner = inset(player->getObjectRect(.3f, .3f), width);
    auto& boxes = GrapeSettings::get()->hitboxes.categories;
    using Type = GrapeSettings::HitboxSettings::Type;
    const auto color = [&](Type type) { return rgba(boxes[type].colors.data()); };

    const CCPoint middle{outer.getMidX(), outer.getMidY()};
    const float angle = -CC_DEGREES_TO_RADIANS(player->getRotation());
    std::array<CCPoint, 4> corners{
        CCPoint{outer.getMinX(), outer.getMinY()},
        CCPoint{outer.getMaxX(), outer.getMinY()},
        CCPoint{outer.getMaxX(), outer.getMaxY()},
        CCPoint{outer.getMinX(), outer.getMaxY()},
    };
    for (CCPoint& corner : corners) corner = corner.rotateByAngle(middle, angle);

    m_node->drawPolygon(corners.data(), corners.size(), CLEAR, width, color(Type::PlayerRotated));
    m_node->drawRect(outer, CLEAR, width, color(Type::Player));
    m_node->drawRect(inner, CLEAR, width, color(Type::PlayerInner));
}

void TrajectoryManager::init(GJBaseGameLayer* layer) {
    uninit();
    m_trajectory = Trajectory::create(layer);
    if (m_trajectory) m_trajectory->m_state = &m_state;
}

void TrajectoryManager::uninit(GJBaseGameLayer* layer) {
    if (!m_trajectory || (layer && !m_trajectory->isFor(layer))) return;
#ifdef GEODE_IS_MOBILE
    crash_log::breadcrumb("Trajectory teardown");
#endif
    delete std::exchange(m_trajectory, nullptr);
#ifdef GEODE_IS_MOBILE
    crash_log::breadcrumb("Trajectory removed");
#endif
}

void TrajectoryManager::update(GJBaseGameLayer* layer) {
    if (!m_trajectory && layer && layer->isRunning() && enabled()) init(layer);
    if (m_trajectory) m_trajectory->update(layer);
}
