#ifndef GRAPE_PRACTICE_HPP
#define GRAPE_PRACTICE_HPP

#include <Geode/binding/CheckpointGameObject.hpp>

#include <config/config.hpp>
#include <shared/value/value.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "bounded_history.hpp"
#include "frame_state.hpp"
#include "platformer_trail.hpp"

class PlayLayer;

namespace grape::snap {

bool deathBlocksCheckpoint(PlayLayer* layer);

struct ReleaseRespawn {
    void operator()(FrameState& state) const;
};

struct HeldDirections {
    bool m_left = false;
    bool m_right = false;
};

class PracticeKeeper {
   public:
    using History = BoundedHistory<FrameState, ReleaseRespawn>;
    using Trail = PlatformerTrail<CheckpointObject*, CheckpointGameObject*>;
    using LoadCheckpoint = std::function<void(CheckpointObject*)>;

    ConfigValuePtr<uint32_t> m_historyLimit =
        ConfigValue<uint32_t>::create("practice_fix.max_stored_frames",
                                      &GrapeSettings::get()->stepsToSave);

    History m_history;
    std::vector<FrameState> m_saved;
    Trail m_platformer;
    FrameState* m_forcedState = nullptr;
    std::size_t m_minimumHistory = 0;

    bool m_pendingRestore = false;
    bool m_normalDeath = false;
    bool m_backstep = false;
    bool m_shouldLoadPlatformer = false;

    FrameState snapshot(CheckpointObject* respawn, uint64_t attemptStart);
    void restore(FrameState& state);

    void stash(CheckpointObject* respawn, uint64_t attemptStart);
    bool canStepBack();
    void stepBack(const LoadCheckpoint& load);
    void dropNewest() { m_history.dropNewest(); }
    void wipeHistory() { m_history.clear(); }
    void resetTo(const FrameState& state);

    void remember(CheckpointObject* respawn, uint64_t attemptStart);
    void forgetLatest();
    void restoreLatest();
    void wipe();

    void loadInto(CheckpointObject* requested, const LoadCheckpoint& load);
    CheckpointObject* respawnPoint();

    void unstackPlatformer(bool markersWereLoaded = false);
    void reseedRandom(uint64_t attemptSeed);
    void trackRandomTrigger(uint64_t* state, int uniqueId);
    void noteBroken(GameObject* object) { m_brokenObjects.push_back(object); }

    void trackPlatformerInputs(std::vector<PlayerButtonCommand>& inputs);
    const HeldDirections& held(bool player2) const { return m_held[player2 ? 1 : 0]; }

   private:
    struct RandomTrigger {
        uint64_t* m_state;
        int m_uniqueId;
    };

    std::size_t historyCapacity();

    std::vector<RandomTrigger> m_randomTriggers;
    std::vector<GameObject*> m_brokenObjects;
    std::array<HeldDirections, 2> m_held{};
};

}

using grape::snap::PracticeKeeper;

#endif
