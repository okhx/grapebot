#pragma once

#include <Geode/Geode.hpp>

#include <algorithm>

namespace grape::assist {

template <class Callback>
void forEachNearbyObject(GJBaseGameLayer* layer, Callback&& callback) {
    const int left = std::max(0, layer->m_leftSectionIndex);
    const int right = std::min(
        layer->m_rightSectionIndex,
        static_cast<int>(layer->m_sections.size()) - 1);

    for (int x = left; x <= right; ++x) {
        auto* column = layer->m_sections[x];
        if (!column || static_cast<size_t>(x) >= layer->m_sectionSizes.size() ||
            !layer->m_sectionSizes[x]) {
            continue;
        }

        const int bottom = std::max(0, layer->m_bottomSectionIndex);
        const int top = std::min({
            layer->m_topSectionIndex,
            static_cast<int>(column->size()) - 1,
            static_cast<int>(layer->m_sectionSizes[x]->size()) - 1});

        for (int y = bottom; y <= top; ++y) {
            auto* section = column->at(y);
            if (!section) continue;

            const int count = std::min(
                layer->m_sectionSizes[x]->at(y),
                static_cast<int>(section->size()));
            for (int i = 0; i < count; ++i) {
                auto* object = section->at(i);
                if (object && !object->m_isDisabled &&
                    !object->m_isGroupDisabled) {
                    callback(object);
                }
            }
        }
    }
}

}
