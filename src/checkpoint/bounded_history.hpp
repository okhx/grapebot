#pragma once

#include <cstddef>
#include <deque>
#include <utility>

namespace grape::snap {

template <class Entry, class Release>
class BoundedHistory {
   public:
    explicit BoundedHistory(Release release = {}) : m_release(std::move(release)) {}

    void push(Entry entry, std::size_t capacity) {
        while (!m_entries.empty() && m_entries.size() >= capacity) evictOldest();
        m_entries.push_back(std::move(entry));
    }

    void shrinkTo(std::size_t capacity) {
        while (m_entries.size() > capacity) evictOldest();
    }

    void dropNewest() {
        if (m_entries.empty()) return;
        m_release(m_entries.back());
        m_entries.pop_back();
    }

    void clear() {
        for (Entry& entry : m_entries) m_release(entry);
        m_entries.clear();
    }

    bool empty() const { return m_entries.empty(); }
    std::size_t size() const { return m_entries.size(); }
    Entry& newest() { return m_entries.back(); }
    const Entry& newest() const { return m_entries.back(); }

    auto rbegin() const { return m_entries.rbegin(); }
    auto rend() const { return m_entries.rend(); }

   private:
    void evictOldest() {
        m_release(m_entries.front());
        m_entries.pop_front();
    }

    std::deque<Entry> m_entries;
    Release m_release;
};

}
