#pragma once

#include <algorithm>

namespace grape::render {

struct FadePlan {
    float m_fadeIn = 0.0f;
    float m_fadeOut = 0.0f;
    float m_afterEnd = 0.0f;

    float tail() const { return std::max(m_afterEnd, m_fadeOut); }

    float amountAt(float elapsed, float sinceEnd) const {
        float amount = 0.0f;

        if (m_fadeIn > 0.0f && elapsed < m_fadeIn)
            amount = std::max(amount, 1.0f - std::max(elapsed, 0.0f) / m_fadeIn);

        if (m_fadeOut > 0.0f && sinceEnd > 0.0f) {
            const float remaining = std::max(tail() - sinceEnd, 0.0f);
            if (remaining < m_fadeOut)
                amount = std::max(amount, 1.0f - remaining / m_fadeOut);
        }

        return std::clamp(amount, 0.0f, 1.0f);
    }
};

}
