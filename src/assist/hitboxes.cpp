#include "hitboxes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/UndoObject.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

#include "assist/nearby_objects.hpp"
#include "config/config.hpp"
#include "engine/engine.hpp"
#include "trailsnap.hpp"
#include "trajectory/trajectory.hpp"

namespace {

using Kind = GrapeSettings::HitboxSettings::Type;
using cocos2d::ccColor4F;
using cocos2d::CCPoint;
using cocos2d::CCRect;

constexpr float INNER_BOX_SCALE = .3f;
constexpr int LIVE_LAYER_LIFT = 10000;
constexpr int TRAIL_LAYER_LIFT = 9998;
constexpr std::size_t SHORTEST_TRAIL = 50;
constexpr std::size_t LONGEST_TRAIL = 8000;

constexpr auto MODIFIERS_SHOWN_WITHOUT_TOUCH =
    std::to_array<int>({200, 201, 202, 203, 1334, 1816, 3643});

struct Pen {
    bool on = false;
    ccColor4F line{};
    ccColor4F fill{};
};

Pen penFor(GrapeSettings::HitboxSettings& settings, Kind kind, bool second) {
    const auto& category = settings.categories[kind];
    const auto& rgba = second ? category.player2Colors : category.colors;
    const ccColor4F line{rgba[0], rgba[1], rgba[2], rgba[3]};
    ccColor4F fill = line;
    fill.a *= static_cast<float>(category.fillOpacity);
    return {category.enabled, line, fill};
}

struct PlayerPens {
    Pen outer;
    Pen inner;
    Pen turned;
    Pen round;
};

struct Palette {
    float thickness = 0.0f;
    Pen solid;
    Pen passable;
    Pen hazard;
    Pen trigger;
    Pen triggerTouched;
    std::array<PlayerPens, 2> players;
    bool markHolding = false;
    ccColor4F holding{};

    static Palette from(GJBaseGameLayer* layer) {
        auto& settings = GrapeSettings::get()->hitboxes;
        Palette palette;
        palette.thickness = static_cast<float>(settings.width) /
                            std::max(layer->m_gameState.m_cameraZoom, .01f);
        palette.solid = penFor(settings, Kind::Solid, false);
        palette.passable = penFor(settings, Kind::Passable, false);
        palette.hazard = penFor(settings, Kind::Hazard, false);
        palette.trigger = penFor(settings, Kind::Interactable, false);
        palette.triggerTouched = penFor(settings, Kind::InteractableActive, false);
        for (int second = 0; second < 2; ++second) {
            palette.players[second] = {
                penFor(settings, Kind::Player, second),
                penFor(settings, Kind::PlayerInner, second),
                penFor(settings, Kind::PlayerRotated, second),
                penFor(settings, Kind::PlayerCircle, second),
            };
        }
        const auto& hold = settings.holdingTrailColor;
        palette.markHolding = settings.holdingTrailEnabled;
        palette.holding = {hold[0], hold[1], hold[2], hold[3]};
        return palette;
    }

    Pen holdingVersion(const Pen& pen) const {
        return {pen.on, holding, {holding.r, holding.g, holding.b, holding.a * pen.fill.a}};
    }
};

CCRect inset(const CCRect& rect, float by) {
    if (by == 0.0f) return rect;
    return {rect.origin.x + by, rect.origin.y + by, rect.size.width - by * 2.0f,
            rect.size.height - by * 2.0f};
}

int roundSegments(float radius, float perUnit, float fewest, float most) {
    return static_cast<int>(std::clamp(radius * perUnit, fewest, most));
}

struct Outline {
    enum class Form { Box, Polygon, Round };

    Form form = Form::Box;
    CCRect box;
    std::array<CCPoint, 4> points{};
    int corners = 0;
    CCPoint center;
    float radius = 0.0f;

    static Outline boxed(const CCRect& rect) {
        Outline outline;
        outline.box = rect;
        return outline;
    }

    template <std::size_t N>
    static Outline polygon(const std::array<CCPoint, N>& points) {
        Outline outline;
        outline.form = Form::Polygon;
        outline.corners = static_cast<int>(N);
        std::copy(points.begin(), points.end(), outline.points.begin());
        return outline;
    }

    static Outline round(CCPoint center, float radius) {
        Outline outline;
        outline.form = Form::Round;
        outline.center = center;
        outline.radius = radius;
        return outline;
    }
};

void paint(cocos2d::CCDrawNode* node, const Outline& outline, const Pen& pen,
           float thickness, int roundDetail) {
    switch (outline.form) {
        case Outline::Form::Box:
            node->drawRect(inset(outline.box, thickness), pen.fill, thickness,
                           pen.line);
            break;
        case Outline::Form::Polygon:
            node->drawPolygon(const_cast<CCPoint*>(outline.points.data()),
                              outline.corners, pen.fill, thickness, pen.line);
            break;
        case Outline::Form::Round:
            if (outline.radius > 0.0f)
                node->drawCircle(outline.center, outline.radius, pen.fill, thickness,
                                 pen.line, roundDetail);
            break;
    }
}

class KeepRectCache {
   public:
    explicit KeepRectCache(GameObject* object)
        : m_object(object),
          m_dirty(object->m_isObjectRectDirty),
          m_offset(object->m_boxOffsetCalculated) {}
    ~KeepRectCache() {
        m_object->m_isObjectRectDirty = m_dirty;
        m_object->m_boxOffsetCalculated = m_offset;
    }
    KeepRectCache(const KeepRectCache&) = delete;
    KeepRectCache& operator=(const KeepRectCache&) = delete;

   private:
    GameObject* m_object;
    bool m_dirty;
    bool m_offset;
};

std::array<CCPoint, 3> slopeCorners(GameObject* object) {
    const auto rect = object->getObjectRect();
    const CCPoint lowLeft{rect.getMinX(), rect.getMinY()};
    const CCPoint highLeft{rect.getMinX(), rect.getMaxY()};
    const CCPoint lowRight{rect.getMaxX(), rect.getMinY()};
    const CCPoint highRight{rect.getMaxX(), rect.getMaxY()};
    switch (object->m_slopeDirection) {
        case 0:
        case 7:
            return {lowLeft, highRight, lowRight};
        case 1:
        case 5:
            return {highRight, highLeft, lowRight};
        case 3:
        case 6:
            return {lowLeft, highLeft, highRight};
        default:
            return {lowLeft, highLeft, lowRight};
    }
}

float hazardRadius(GameObject* object) {
    return object->m_objectRadius * std::max(object->m_scaleX, object->m_scaleY);
}

Outline objectOutline(GameObject* object, bool refreshTurnedBox = false) {
    const KeepRectCache keep(object);
    if (object->m_objectType == GameObjectType::Slope)
        return Outline::polygon(slopeCorners(object));
    const bool hazard = object->m_objectType == GameObjectType::Hazard ||
                        object->m_objectType == GameObjectType::AnimatedHazard;
    if (hazard && hazardRadius(object) > 0.0f)
        return Outline::round(object->getPosition(), hazardRadius(object));
    if (object->m_objectType != GameObjectType::Solid && object->m_orientedBox) {
        auto* turned = refreshTurnedBox ? object->getOrientedBox() : object->m_orientedBox;
        if (turned) return Outline::polygon(turned->m_corners);
    }
    return Outline::boxed(object->getObjectRect());
}

bool isPlayerLike(GJBaseGameLayer* layer, GameObject* object) {
    return object == layer->m_player1 || object == layer->m_player2 ||
           GrapeEngine::get()->trajectory().isFakePlayer(
               static_cast<PlayerObject*>(object));
}

bool usedUp(GJBaseGameLayer* layer, GameObject* object) {
    const bool byFirst = object->hasBeenActivatedByPlayer(layer->m_player1);
    if (!layer->m_gameState.m_isDualMode) return byFirst;
    return byFirst && object->hasBeenActivatedByPlayer(layer->m_player2);
}

bool touchedByPlayer(GJBaseGameLayer* layer, GameObject* object) {
    
    
    for (auto* player : {layer->m_player1, layer->m_player2})
        if (player && player->m_touchingRings &&
            player->m_touchingRings->containsObject(object))
            return true;
    const KeepRectCache keep(object);
    const auto rect = object->getObjectRect();
    for (auto* player : {layer->m_player1, layer->m_player2})
        if (player && rect.intersectsRect(player->getObjectRect())) return true;
    return false;
}

const Pen* pickPen(GJBaseGameLayer* layer, GameObject* object, const Palette& palette) {
    if (!object->m_isActivated || object->m_objectType == GameObjectType::Decoration ||
        isPlayerLike(layer, object))
        return nullptr;

    switch (object->m_objectType) {
        case GameObjectType::Solid:
        case GameObjectType::Slope: {
            const Pen& pen = object->m_isPassable ? palette.passable : palette.solid;
            return pen.on ? &pen : nullptr;
        }
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard:
            return object != layer->m_anticheatSpike && palette.hazard.on
                ? &palette.hazard : nullptr;
        default:
            break;
    }

    if (!palette.trigger.on || usedUp(layer, object) ||
        object == layer->m_player1CollisionBlock ||
        object == layer->m_player2CollisionBlock)
        return nullptr;
    if (object->m_objectType == GameObjectType::Modifier &&
        std::ranges::find(MODIFIERS_SHOWN_WITHOUT_TOUCH, object->m_objectID) ==
            MODIFIERS_SHOWN_WITHOUT_TOUCH.end() &&
        !static_cast<EffectGameObject*>(object)->m_isTouchTriggered)
        return nullptr;
    return touchedByPlayer(layer, object) ? &palette.triggerTouched : &palette.trigger;
}

void paintObject(cocos2d::CCDrawNode* node, GameObject* object, const Pen& pen,
                 const Palette& palette) {
    const Outline outline = objectOutline(object);
    paint(node, outline, pen, palette.thickness,
          roundSegments(outline.radius, 1.5f, 8.0f, 48.0f));
}

void paintPlayer(cocos2d::CCDrawNode* node, PlayerObject* player, const Palette& palette,
                 bool second) {
    if (!player) return;
    const auto boxes = PlayerBoxes::of(player);
    const auto& pens = palette.players[second ? 1 : 0];
    const float thickness = palette.thickness;
    if (pens.turned.on && boxes.turnedValid)
        paint(node, Outline::polygon(boxes.turned), pens.turned, thickness, 0);
    if (pens.round.on) {
        Pen hollow = pens.round;
        hollow.fill = {0.0f, 0.0f, 0.0f, 0.0f};
        const float radius = boxes.outer.size.width * .5f;
        paint(node, Outline::round(player->getPosition(), radius), hollow, thickness,
              roundSegments(radius, 1.5f, 8.0f, 48.0f));
    }
    if (pens.outer.on) paint(node, Outline::boxed(boxes.outer), pens.outer, thickness, 0);
    if (pens.inner.on) paint(node, Outline::boxed(boxes.inner), pens.inner, thickness, 0);
}

cocos2d::CCDrawNode* makeLayer(cocos2d::CCNode* parent, int z, const char* id) {
    auto* node = cocos2d::CCDrawNode::create();
    if (!node) return nullptr;
    node->retain();
    node->setID(id);
    node->m_bUseArea = false;
    node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    parent->addChild(node, z);
    return node;
}

void dropLayer(cocos2d::CCDrawNode*& node) {
    if (!node) return;
    if (node->getParent()) node->removeFromParentAndCleanup(true);
    node->release();
    node = nullptr;
}

}

PlayerBoxes PlayerBoxes::of(PlayerObject* player) {
    PlayerBoxes boxes;
    boxes.outer = player->getObjectRect();
    boxes.inner = player->getObjectRect(INNER_BOX_SCALE, INNER_BOX_SCALE);
    if (auto* oriented = player->getOrientedBox()) {
        boxes.turned = oriented->m_corners;
        boxes.turnedValid = !boxes.turned[0].equals(boxes.turned[2]);
    }
    boxes.holding = player->m_jumpBuffered || player->m_holdingButtons[1];
    return boxes;
}

bool PlayerBoxes::sameSpot(const PlayerBoxes& other) const {
    return holding == other.holding && outer.origin.equals(other.outer.origin) &&
           outer.size.equals(other.outer.size);
}

void BoxTrail::straighten() {
    std::rotate(m_samples.begin(),
                m_samples.begin() + static_cast<std::ptrdiff_t>(m_oldest),
                m_samples.end());
    m_oldest = 0;
}

void BoxTrail::push(const PlayerBoxes& boxes, std::size_t limit) {
    const std::size_t count = m_samples.size();
    if (count && m_samples[(m_oldest + count - 1) % count].sameSpot(boxes)) return;
    if (count && (count > limit || (count < limit && m_oldest))) straighten();
    if (m_samples.size() > limit)
        m_samples.erase(m_samples.begin(),
                        m_samples.begin() +
                            static_cast<std::ptrdiff_t>(m_samples.size() - limit));
    if (m_samples.size() < limit) {
        m_samples.push_back(boxes);
        return;
    }
    m_samples[m_oldest] = boxes;
    m_oldest = (m_oldest + 1) % m_samples.size();
}

void BoxTrail::clear() {
    m_samples.clear();
    m_oldest = 0;
}

void Hitboxes::init(GJBaseGameLayer* layer) {
    if (m_initialized) destroy();
    clearDeath();
    if (!layer || !layer->m_aboveShaderObjectLayer || !layer->m_uiLayer) return;

    auto* parent = layer->m_aboveShaderObjectLayer;
    const int base = layer->m_uiLayer->getZOrder();
    m_liveLayer = makeLayer(parent, base + LIVE_LAYER_LIFT, "hitbox-node"_spr);
    m_trailLayer = makeLayer(parent, base + TRAIL_LAYER_LIFT, "hitbox-trail-node"_spr);
    m_layer = layer;
    m_trailStale = true;
    m_initialized = true;
}

void Hitboxes::draw(GJBaseGameLayer* layer) {
    if (!m_initialized || !layer || !m_liveLayer || !m_trailLayer) return;
    paintTrail(layer);
    paintLive(layer);
}

void Hitboxes::paintTrail(GJBaseGameLayer* layer) {
    if (!m_trailEnabled->inner() || isolating()) {
        m_trailLayer->clear();
        m_trailStale = true;
        return;
    }
    if (!m_trailStale) return;
    m_trailStale = false;
    m_trailLayer->clear();

    const Palette palette = Palette::from(layer);
    const float thickness = palette.thickness;
    const float scale = std::max(layer->m_objectLayer->getScale(), .01f);
    const float spacing = .5f / scale;

    const auto sweep = [&](const BoxTrail& trail, bool second, bool innerPass) {
        const auto& pens = palette.players[second ? 1 : 0];
        std::optional<CCPoint> previous;
        trail.each([&](const PlayerBoxes& boxes) {
            const CCPoint middle{boxes.outer.getMidX(), boxes.outer.getMidY()};
            if (previous && std::fabs(previous->x - middle.x) < spacing &&
                std::fabs(previous->y - middle.y) < spacing)
                return;
            previous = middle;

            if (innerPass) {
                if (pens.inner.on)
                    paint(m_trailLayer, Outline::boxed(boxes.inner), pens.inner,
                          thickness, 0);
                return;
            }
            const bool marked = palette.markHolding && boxes.holding;
            const auto styled = [&](const Pen& pen) {
                return marked ? palette.holdingVersion(pen) : pen;
            };
            if (pens.outer.on)
                paint(m_trailLayer, Outline::boxed(boxes.outer), styled(pens.outer),
                      thickness, 0);
            if (pens.turned.on && boxes.turnedValid)
                paint(m_trailLayer, Outline::polygon(boxes.turned), styled(pens.turned),
                      thickness, 0);
            if (pens.round.on) {
                const float radius = inset(boxes.outer, thickness).size.width * .5f;
                paint(m_trailLayer, Outline::round(middle, radius), styled(pens.round),
                      thickness, roundSegments(radius, .75f, 6.0f, 20.0f));
            }
        });
    };

    for (const bool innerPass : {false, true}) {
        sweep(m_trails[0], false, innerPass);
        sweep(m_trails[1], true, innerPass);
    }
}

void Hitboxes::paintLive(GJBaseGameLayer* layer) {
    m_liveLayer->clear();
    if (!m_enabled->inner()) return;

    const Palette palette = Palette::from(layer);
    if (isolating()) {
        if (m_deathObject && !isPlayerLike(layer, m_deathObject)) {
            const Pen* pen = pickPen(layer, m_deathObject, palette);
            paintObject(m_liveLayer, m_deathObject, pen ? *pen : palette.hazard, palette);
        }
        return;
    }

    grape::assist::forEachNearbyObject(layer, [&](GameObject* object) {
        if (const Pen* pen = pickPen(layer, object, palette))
            paintObject(m_liveLayer, object, *pen, palette);
    });
    paintPlayer(m_liveLayer, layer->m_player1, palette, false);
    if (layer->m_gameState.m_isDualMode)
        paintPlayer(m_liveLayer, layer->m_player2, palette, true);
}

void Hitboxes::saveToTrail(GJBaseGameLayer* layer) {
    if (layer->m_levelEndAnimationStarted || !m_trailEnabled->inner()) return;

    auto& settings = GrapeSettings::get()->hitboxes;
    const auto limit = std::clamp<std::size_t>(
        static_cast<std::size_t>(std::max(settings.trailMaxLength, 0)), SHORTEST_TRAIL,
        LONGEST_TRAIL);
    if (layer->m_player1) m_trails[0].push(PlayerBoxes::of(layer->m_player1), limit);
    if (layer->m_player2) m_trails[1].push(PlayerBoxes::of(layer->m_player2), limit);

    if (++m_stepsSinceTrailPaint >= std::max(1, settings.trailRebuildInterval)) {
        m_stepsSinceTrailPaint = 0;
        m_trailStale = true;
    }
}

void Hitboxes::clearTrail() {
    for (auto& trail : m_trails) trail.clear();
    m_stepsSinceTrailPaint = 0;
    m_trailStale = true;
}

bool Hitboxes::isolating() const {
    return m_isolateWanted && m_deathPlayer && m_deathPlayer->m_isDead;
}

void Hitboxes::showDeath(PlayerObject* player, GameObject* object) {
    if (!player) return;
    m_deathPlayer = player;
    m_deathObject = object;
    if (isolating() && m_initialized && m_layer) draw(m_layer);
}

void Hitboxes::clearDeath() {
    if (m_deathPlayer) m_trailStale = true;
    m_deathPlayer = nullptr;
    m_deathObject = nullptr;
}

void Hitboxes::destroy(GJBaseGameLayer* layer) {
    if (layer && m_layer != layer) return;
    clearDeath();
    if (!m_initialized) return;
    dropLayer(m_liveLayer);
    dropLayer(m_trailLayer);
    m_layer = nullptr;
    m_trailStale = true;
    m_initialized = false;
}

namespace {

constexpr double TRAIL_SNAP_LIFT = 30.0;
constexpr double SMALL_SPIKE_LIFT = 15.0;
constexpr float SMALL_SPIKE_MAX_HEIGHT = 22.5f;

bool isSmallSpike(GameObject* object) {
    const bool hazard = object->m_objectType == GameObjectType::Hazard ||
                        object->m_objectType == GameObjectType::AnimatedHazard;
    return hazard && object->m_objectRadius <= 0.0f &&
           object->boundingBox().size.height <= SMALL_SPIKE_MAX_HEIGHT;
}

#ifdef GEODE_IS_MOBILE
constexpr double PLATFORM_SNAP_DROP = 30.0;
#else
constexpr double PLATFORM_SNAP_DROP = 0.0;
#endif

double trailSnapLift(GameObject* object) {
    if (isSmallSpike(object)) return SMALL_SPIKE_LIFT - PLATFORM_SNAP_DROP / 2.0;
    return TRAIL_SNAP_LIFT - PLATFORM_SNAP_DROP;
}

enum class SnapTrail { Outer, Inner };

struct SnapTarget {
    grape::trailsnap::Shape m_shape;
    SnapTrail m_trail = SnapTrail::Outer;
    grape::trailsnap::Vec m_sawCenter;
    double m_sawRadius = 0.0;
};

grape::trailsnap::SawRule sawRule(LevelEditorLayer* editor) {
    const bool fixed = editor && editor->m_levelSettings &&
                       editor->m_levelSettings->m_fixRadiusCollision;
    return fixed ? grape::trailsnap::SawRule::Circle
                 : grape::trailsnap::SawRule::CornersOrCenter;
}

grape::trailsnap::Box snapBox(const CCRect& rect) {
    return {rect.getMinX(), rect.getMinY(), rect.getMaxX(), rect.getMaxY()};
}

grape::trailsnap::Quad snapQuad(const CCRect& rect) {
    return grape::trailsnap::boxQuad(snapBox(rect));
}

grape::trailsnap::Shape shapeOf(const Outline& outline) {
    switch (outline.form) {
        case Outline::Form::Round:
            return grape::trailsnap::circleShape({outline.center.x, outline.center.y},
                                                 outline.radius);
        case Outline::Form::Polygon: {
            grape::trailsnap::Shape shape;
            for (int i = 0; i < outline.corners; ++i)
                shape.push_back({outline.points[i].x, outline.points[i].y});
            return shape;
        }
        default:
            return grape::trailsnap::boxShape(snapBox(outline.box));
    }
}

std::optional<SnapTarget> snapTarget(GameObject* object) {
    switch (object->m_objectType) {
        case GameObjectType::Hazard:
        case GameObjectType::AnimatedHazard: {
            const Outline outline = objectOutline(object, true);
            if (outline.form == Outline::Form::Round)
                return SnapTarget{{}, SnapTrail::Outer,
                                  {outline.center.x, outline.center.y}, outline.radius};
            return SnapTarget{shapeOf(outline), SnapTrail::Outer, {}, 0.0};
        }
        case GameObjectType::Solid:
            return SnapTarget{shapeOf(objectOutline(object, true)), SnapTrail::Inner, {}, 0.0};
        case GameObjectType::Slope:
            return SnapTarget{shapeOf(objectOutline(object, true)), SnapTrail::Outer, {}, 0.0};
        default:
            return std::nullopt;
    }
}

void snapNotice(std::string const& text, geode::NotificationIcon icon) {
    geode::Notification::create(text, icon)->show();
}

}

void Hitboxes::snapToTrail() {
    auto* editor = LevelEditorLayer::get();
    auto* ui = editor ? editor->m_editorUI : nullptr;
    if (!ui) {
        snapNotice("Open a level in the editor to snap to the trail",
                   geode::NotificationIcon::Warning);
        return;
    }
    if (editor->m_playbackMode != PlaybackMode::Not) {
        snapNotice("Stop the playtest before snapping",
                   geode::NotificationIcon::Warning);
        return;
    }
    if (m_trails[0].empty() && m_trails[1].empty()) {
        snapNotice("No hitbox trail yet: enable Show Trail and playtest",
                   geode::NotificationIcon::Warning);
        return;
    }

    std::vector<GameObject*> objects;
    auto* selected = ui->getSelectedObjects();
    for (auto* object : geode::cocos::CCArrayExt<GameObject*>(
             selected ? selected : cocos2d::CCArray::create()))
        objects.push_back(object);

    const SnapResult result = snapObjects(ui, objects, true);
    if (result.m_moved == 0) {
        if (result.m_unreachable > 0)
            snapNotice("Too far from the hitbox trail to snap",
                       geode::NotificationIcon::Warning);
        else if (result.m_settled > 0)
            snapNotice("Already snapped to the hitbox trail",
                       geode::NotificationIcon::Info);
        else
            snapNotice("Select hazards, solids or slopes to snap",
                       geode::NotificationIcon::Warning);
        return;
    }

    auto text = fmt::format("Snapped {} object{} to the hitbox trail", result.m_moved,
                            result.m_moved == 1 ? "" : "s");
    if (result.m_unreachable > 0)
        text += fmt::format(", {} too far away", result.m_unreachable);
    snapNotice(text, geode::NotificationIcon::Success);
}

void Hitboxes::snapPlaced(GameObject* object) {
    if (!object || !GrapeSettings::get()->hitboxes.snapPlacedToTrail) return;
    auto* editor = LevelEditorLayer::get();
    auto* ui = editor ? editor->m_editorUI : nullptr;
    if (!ui || editor->m_playbackMode != PlaybackMode::Not) return;
    if (m_trails[0].empty() && m_trails[1].empty()) return;
    snapObjects(ui, {object}, false);
}

Hitboxes::SnapResult Hitboxes::snapObjects(EditorUI* ui,
                                           const std::vector<GameObject*>& objects,
                                           bool recordUndo) {
    SnapResult result;
    std::vector<grape::trailsnap::Quad> outer;
    std::vector<grape::trailsnap::Quad> inner;
    std::vector<grape::trailsnap::Box> outerBoxes;
    for (const auto& trail : m_trails) {
        trail.each([&](const PlayerBoxes& boxes) {
            outerBoxes.push_back(snapBox(boxes.outer));
            outer.push_back(snapQuad(boxes.outer));
            inner.push_back(snapQuad(boxes.inner));
        });
    }
    const auto trailFor = [&](SnapTrail trail) -> const auto& {
        switch (trail) {
            case SnapTrail::Inner: return inner;
            default: return outer;
        }
    };

    const auto rule = sawRule(LevelEditorLayer::get());
    std::vector<std::pair<GameObject*, CCPoint>> moves;
    for (auto* object : objects) {
        const auto target = object ? snapTarget(object) : std::nullopt;
        if (!target) continue;
        ++result.m_eligible;
        const auto offset =
            target->m_sawRadius > 0.0
                ? grape::trailsnap::sawSnapOffset(target->m_sawCenter, target->m_sawRadius,
                                                  outerBoxes, rule)
                : grape::trailsnap::snapOffset(target->m_shape, trailFor(target->m_trail));
        if (!offset) {
            ++result.m_unreachable;
            continue;
        }
        const double liftedY = offset->m_y + trailSnapLift(object);
        if (std::hypot(offset->m_x, liftedY) < 1e-4) {
            ++result.m_settled;
        } else {
            moves.push_back({object, {static_cast<float>(offset->m_x),
                                      static_cast<float>(liftedY)}});
        }
    }
    if (moves.empty()) return result;

    if (recordUndo) {
        auto* moved = cocos2d::CCArray::create();
        for (const auto& [object, _] : moves) moved->addObject(object);
        if (auto* editor = LevelEditorLayer::get())
            editor->addToUndoList(
                UndoObject::createWithTransformObjects(moved, UndoCommand::Transform),
                false);
    }
    for (const auto& [object, offset] : moves) ui->moveObject(object, offset);
    ui->updateObjectInfoLabel();
    result.m_moved = static_cast<int>(moves.size());
    return result;
}

$execute {
    GrapeEngine::get()->hitboxes().m_trailSnap->handle([](bool& wanted) {
        if (wanted) GrapeEngine::get()->hitboxes().snapToTrail();
        wanted = false;
    });
}
