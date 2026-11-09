#pragma once

#include <cmath>
#include <cstdint>

namespace grape::variance {

struct Traits {
    int m_objectId = 0;
    float m_startX = 0.0f;
    float m_startY = 0.0f;
    int m_zLayer = 0;
    int m_groupCount = 0;
    bool m_black = false;
    float m_startRotationX = 0.0f;
};

inline constexpr uint64_t BUCKETS = 1777;

inline int32_t wrappingProduct(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b));
}

inline short tag(uint64_t seed, const Traits& object, int group, int index) {
    const int32_t spin = wrappingProduct(
        static_cast<int32_t>(std::fabs(object.m_startRotationX) * 6.7 + 1.0), index + 67);
    const int32_t factors[] = {
        object.m_objectId + 5541,
        static_cast<int32_t>(std::fabs(object.m_startX) + 1.0),
        static_cast<int32_t>(std::fabs(object.m_startY) + 1.0),
        object.m_zLayer + 2137,
        object.m_groupCount + 6969,
        (object.m_black ? 1 : 0) * 420 + 14,
        spin,
        group + 2137,
    };
    uint64_t mixed = seed;
    for (int32_t factor : factors) mixed *= static_cast<uint64_t>(static_cast<int64_t>(factor));
    return static_cast<short>(mixed % BUCKETS);
}

template <class Object>
short tagFor(uint64_t seed, const Object* object, int group, int index) {
    return tag(seed,
               {
                   .m_objectId = object->m_objectID,
                   .m_startX = object->m_startPosition.x,
                   .m_startY = object->m_startPosition.y,
                   .m_zLayer = static_cast<int>(object->m_zLayer),
                   .m_groupCount = object->m_groupCount,
                   .m_black = object->m_isObjectBlack,
                   .m_startRotationX = object->m_startRotationX,
               },
               group, index);
}

}
