#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <string>

namespace grape::ui {

constexpr float NOTIFICATION_SLIDE_IN = .28f;
constexpr float NOTIFICATION_SLIDE_OUT = .22f;
constexpr float NOTIFICATION_DEFAULT_LIFE = 6.0f;
constexpr std::size_t NOTIFICATION_MAX_VISIBLE = 4;

constexpr float easeOutCubic(float t) {
    const float inverse = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inverse * inverse * inverse;
}

constexpr float easeInCubic(float t) {
    const float value = std::clamp(t, 0.0f, 1.0f);
    return value * value * value;
}

constexpr float notificationSlide(float age, float life) {
    if (age <= 0.0f) return 1.0f;
    if (age < NOTIFICATION_SLIDE_IN)
        return 1.0f - easeOutCubic(age / NOTIFICATION_SLIDE_IN);
    const float leaving = life - NOTIFICATION_SLIDE_OUT;
    if (age >= life) return 1.0f;
    if (age > leaving && NOTIFICATION_SLIDE_OUT > 0.0f)
        return easeInCubic((age - leaving) / NOTIFICATION_SLIDE_OUT);
    return 0.0f;
}

static_assert(notificationSlide(0.0f, 5.0f) == 1.0f);
static_assert(notificationSlide(NOTIFICATION_SLIDE_IN, 5.0f) == 0.0f);
static_assert(notificationSlide(2.0f, 5.0f) == 0.0f);
static_assert(notificationSlide(5.0f, 5.0f) == 1.0f);

struct Notice {
    std::string m_title;
    std::string m_body;
    float m_accent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float m_life = NOTIFICATION_DEFAULT_LIFE;
    float m_age = 0.0f;
    unsigned m_id = 0;
    float m_bounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    constexpr bool expired() const { return m_age >= m_life; }
    constexpr float slide() const { return notificationSlide(m_age, m_life); }
    constexpr float opacity() const { return 1.0f - slide(); }
};

class NotificationCenter {
   public:
    static NotificationCenter& get() {
        static NotificationCenter center;
        return center;
    }

    unsigned push(std::string title, std::string body, const float accent[4],
                  float life) {
        Notice notice;
        notice.m_title = std::move(title);
        notice.m_body = std::move(body);
        if (accent)
            for (int i = 0; i < 4; ++i) notice.m_accent[i] = accent[i];
        notice.m_life = life > 0.0f ? life : NOTIFICATION_DEFAULT_LIFE;
        notice.m_id = ++m_nextId;
        m_notices.push_back(std::move(notice));
        retireOverflow();
        return m_nextId;
    }

    unsigned push(std::string title, std::string body) {
        return push(std::move(title), std::move(body), nullptr,
                    NOTIFICATION_DEFAULT_LIFE);
    }

    Notice* find(unsigned id) {
        for (auto& notice : m_notices)
            if (notice.m_id == id) return &notice;
        return nullptr;
    }

    bool dismiss(unsigned id) {
        Notice* notice = find(id);
        if (!notice) return false;
        beginExit(*notice);
        return true;
    }

    bool dismissAt(float x, float y) {
        for (auto& notice : m_notices) {
            const float* box = notice.m_bounds;
            if (notice.opacity() <= 0.0f || x < box[0] || y < box[1] ||
                x > box[2] || y > box[3])
                continue;
            beginExit(notice);
            return true;
        }
        return false;
    }

    void clear() { m_notices.clear(); }

    void update(float delta) {
        if (!(delta > 0.0f)) return;
        const float step = std::min(delta, .1f);
        for (auto& notice : m_notices) notice.m_age += step;
        while (!m_notices.empty() && m_notices.front().expired())
            m_notices.pop_front();
        for (auto it = m_notices.begin(); it != m_notices.end();)
            it = it->expired() ? m_notices.erase(it) : std::next(it);
    }

    const std::deque<Notice>& notices() const { return m_notices; }
    std::deque<Notice>& notices() { return m_notices; }
    bool empty() const { return m_notices.empty(); }

   private:
    NotificationCenter() = default;

    static void beginExit(Notice& notice) {
        const float remaining = notice.m_life - notice.m_age;
        if (remaining <= NOTIFICATION_SLIDE_OUT) return;
        notice.m_life = notice.m_age + NOTIFICATION_SLIDE_OUT;
    }

    void retireOverflow() {
        if (m_notices.size() <= NOTIFICATION_MAX_VISIBLE) return;
        std::size_t excess = m_notices.size() - NOTIFICATION_MAX_VISIBLE;
        for (auto& notice : m_notices) {
            if (!excess) break;
            beginExit(notice);
            --excess;
        }
    }

    std::deque<Notice> m_notices;
    unsigned m_nextId = 0;
};

}
