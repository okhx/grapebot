#pragma once

#include <algorithm>

namespace grape::assist {

struct Prediction {
    int m_holdScore = 0;
    int m_releaseScore = 0;
    int m_horizon = 1;
};

struct Proximity {
    float m_hazard = 1e9f;
    float m_block = 1e9f;
    float m_ring = 1e9f;
    float m_lowerClearance = 1e9f;
    float m_upperClearance = 1e9f;
};

struct Situation {
    bool m_holding = false;
    bool m_groundJump = false;
    bool m_flying = false;
    bool m_upsideDown = false;
    int m_reactionFrames = 12;
    float m_hazardWindow = 8.0f;
    float m_blockWindow = 60.0f;
};

enum class Choice {
    Keep,
    Hold,
    Release,
};

struct Decision {
    Choice m_choice = Choice::Keep;
    const char* m_reason = "keep";
};

constexpr int SWITCH_MARGIN = 2;

constexpr Decision chooseInput(Prediction prediction, Proximity nearby,
                               Situation state) {
    const int horizon = std::max(1, prediction.m_horizon);
    const int currentScore =
        state.m_holding ? prediction.m_holdScore : prediction.m_releaseScore;
    const int otherScore =
        state.m_holding ? prediction.m_releaseScore : prediction.m_holdScore;

    const bool safe = currentScore >= horizon;

    if (state.m_flying) {
        const bool lower = nearby.m_lowerClearance <= 12.0f &&
                           nearby.m_lowerClearance + 2.0f <
                               nearby.m_upperClearance;
        const bool upper = nearby.m_upperClearance <= 12.0f &&
                           nearby.m_upperClearance + 2.0f <
                               nearby.m_lowerClearance;
        if (lower)
            return {state.m_upsideDown ? Choice::Release : Choice::Hold,
                    "clearance-low"};
        if (upper)
            return {state.m_upsideDown ? Choice::Hold : Choice::Release,
                    "clearance-high"};
    }

    if (safe) return {Choice::Keep, "safe"};

    if (currentScore <= state.m_reactionFrames &&
        (otherScore >= horizon || otherScore >= currentScore + SWITCH_MARGIN)) {
        return {state.m_holding ? Choice::Release : Choice::Hold, "escape"};
    }

    if (!state.m_holding) {
        if (nearby.m_ring <= 45.0f &&
            prediction.m_holdScore >= prediction.m_releaseScore + SWITCH_MARGIN)
            return {Choice::Hold, "orb"};

        const bool obstacle = nearby.m_hazard <= state.m_hazardWindow ||
                              nearby.m_block <= state.m_blockWindow;
        if (state.m_groundJump && obstacle &&
            prediction.m_holdScore >= prediction.m_releaseScore + SWITCH_MARGIN)
            return {Choice::Hold, "commit-jump"};
    }

    return {Choice::Keep, "keep"};
}

}
