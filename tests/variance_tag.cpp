#include "engine/variance_tag.hpp"

#include <cstdio>

using grape::variance::Traits;
using grape::variance::tag;

int main() {
    struct Case {
        uint64_t m_seed;
        Traits m_object;
        int m_group;
        int m_index;
        short m_expected;
    };
    const Case cases[] = {
        {0x9e3779b97f4a7c15ull, {1, 15.f, 105.f, 0, 1, false, 0.f}, 5, 0, 567},
        {12345ull, {1329, -4321.5f, 90.f, -3, 3, true, 45.f}, 9999, 17, 1523},
        {0xffffffffffffffffull, {8, 30000.f, -30000.f, 5, 9, false, 719.f}, 1, 499, 1629},
        {42ull, {4500, 0.25f, 0.75f, 1, 0, true, -360.f}, 0, 3, 417},
    };
    int failures = 0;
    for (const Case& c : cases) {
        const short got = tag(c.m_seed, c.m_object, c.m_group, c.m_index);
        if (got != c.m_expected) {
            std::printf("FAILED: seed %llu gave %d, expected %d\n",
                        static_cast<unsigned long long>(c.m_seed), got, c.m_expected);
            ++failures;
        }
        if (got < 0 || got >= static_cast<short>(grape::variance::BUCKETS)) ++failures;
    }
    if (failures == 0) std::printf("variance_tag: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
