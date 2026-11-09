#ifndef GRAPE_VINE_PLAYHEAD_HPP
#define GRAPE_VINE_PLAYHEAD_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "format.hpp"

namespace grape::vine {

class Playhead {
    const std::vector<Event>* m_events = nullptr;
    size_t m_index = 0;

   public:
    void attach(const std::vector<Event>& events) { m_events = &events; }

    size_t position() const { return m_index; }
    void setPosition(size_t index) { m_index = index; }
    bool exhausted() const { return !m_events || m_index >= m_events->size(); }
    size_t remaining() const {
        return m_events ? m_events->size() - std::min(m_index, m_events->size())
                        : 0;
    }

    const Event* peek() const {
        if (exhausted()) return nullptr;
        return &(*m_events)[m_index];
    }

    void advance() {
        if (!exhausted()) ++m_index;
    }

    const Event* peekDue(uint64_t frame) const {
        const Event* event = peek();
        return event && event->m_frame <= frame ? event : nullptr;
    }

    std::optional<uint64_t> framesUntilNext(uint64_t frame) const {
        const Event* event = peek();
        if (!event || event->m_frame < frame) return std::nullopt;
        return event->m_frame - frame;
    }

    void skipAt(uint64_t frame, bool inclusive) {
        while (const Event* event = peek()) {
            const bool drop = inclusive ? event->m_frame <= frame
                                        : event->m_frame == frame;
            if (!drop) break;
            advance();
        }
    }

    void skipResetsAt(uint64_t frame) {
        while (const Event* event = peek()) {
            if (event->m_frame != frame) break;
            if (event->m_type != EventKind::Restart &&
                event->m_type != EventKind::RestartFull &&
                event->m_type != EventKind::Death) {
                break;
            }
            advance();
        }
    }

    void seek(uint64_t frame) {
        if (!m_events) {
            m_index = 0;
            return;
        }
        m_index = static_cast<size_t>(
            std::lower_bound(m_events->begin(), m_events->end(), frame,
                             [](const Event& event, uint64_t target) {
                                 return event.m_frame < target;
                             }) -
            m_events->begin());
    }

    void seekAfter(uint64_t frame) {
        if (!m_events) {
            m_index = 0;
            return;
        }
        m_index = static_cast<size_t>(
            std::upper_bound(m_events->begin(), m_events->end(), frame,
                             [](uint64_t target, const Event& event) {
                                 return target < event.m_frame;
                             }) -
            m_events->begin());
    }
};

}

#endif
