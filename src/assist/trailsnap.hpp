#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

namespace grape::trailsnap {

struct Vec {
    double m_x = 0.0;
    double m_y = 0.0;
};

struct Box {
    double m_minX = 0.0;
    double m_minY = 0.0;
    double m_maxX = 0.0;
    double m_maxY = 0.0;
};

using Shape = std::vector<Vec>;
using Quad = std::array<Vec, 4>;

inline constexpr double CLEARANCE = 0.05;
inline constexpr double REACH = 120.0;
inline constexpr double EPSILON = 1e-6;
inline constexpr int CIRCLE_SIDES = 32;

inline Shape boxShape(const Box& box) {
    return {{box.m_minX, box.m_minY},
            {box.m_maxX, box.m_minY},
            {box.m_maxX, box.m_maxY},
            {box.m_minX, box.m_maxY}};
}

inline Quad boxQuad(const Box& box) {
    return {Vec{box.m_minX, box.m_minY}, Vec{box.m_maxX, box.m_minY},
            Vec{box.m_maxX, box.m_maxY}, Vec{box.m_minX, box.m_maxY}};
}

inline Shape circleShape(Vec center, double radius, int sides = CIRCLE_SIDES) {
    Shape shape;
    const double outer = radius / std::cos(std::numbers::pi / sides);
    for (int i = 0; i < sides; ++i) {
        const double angle = 2.0 * std::numbers::pi * i / sides;
        shape.push_back({center.m_x + outer * std::cos(angle),
                         center.m_y + outer * std::sin(angle)});
    }
    return shape;
}

inline Box boundsOf(std::span<const Vec> points) {
    Box box{std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity()};
    for (const Vec& p : points) {
        box.m_minX = std::min(box.m_minX, p.m_x);
        box.m_minY = std::min(box.m_minY, p.m_y);
        box.m_maxX = std::max(box.m_maxX, p.m_x);
        box.m_maxY = std::max(box.m_maxY, p.m_y);
    }
    return box;
}

inline double turn(Vec o, Vec a, Vec b) {
    return (a.m_x - o.m_x) * (b.m_y - o.m_y) -
           (a.m_y - o.m_y) * (b.m_x - o.m_x);
}

inline std::vector<Vec> convexHull(std::vector<Vec> points) {
    std::ranges::sort(points, [](Vec a, Vec b) {
        return a.m_x < b.m_x || (a.m_x == b.m_x && a.m_y < b.m_y);
    });
    const auto repeated = std::ranges::unique(points, [](Vec a, Vec b) {
        return a.m_x == b.m_x && a.m_y == b.m_y;
    });
    points.erase(repeated.begin(), repeated.end());
    if (points.size() < 3) return points;

    std::vector<Vec> hull(points.size() * 2);
    std::size_t k = 0;
    for (const Vec& p : points) {
        while (k >= 2 && turn(hull[k - 2], hull[k - 1], p) <= 0.0) --k;
        hull[k++] = p;
    }
    const std::size_t lower = k + 1;
    for (std::size_t i = points.size() - 1; i-- > 0;) {
        while (k >= lower && turn(hull[k - 2], hull[k - 1], points[i]) <= 0.0)
            --k;
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return hull;
}

struct Region {
    std::vector<Vec> m_vertices;
    std::vector<Vec> m_normals;
    std::vector<double> m_offsets;
    Box m_bounds;

    double depth(Vec p) const {
        double outside = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < m_normals.size(); ++i) {
            outside = std::max(outside, m_normals[i].m_x * p.m_x +
                                            m_normals[i].m_y * p.m_y -
                                            m_offsets[i]);
        }
        return outside;
    }

    bool mayContain(Vec p) const {
        return p.m_x >= m_bounds.m_minX - EPSILON &&
               p.m_x <= m_bounds.m_maxX + EPSILON &&
               p.m_y >= m_bounds.m_minY - EPSILON &&
               p.m_y <= m_bounds.m_maxY + EPSILON;
    }
};

inline std::optional<Region> blockedOffsets(const Quad& trail,
                                            std::span<const Vec> shape,
                                            double clearance) {
    std::vector<Vec> spread;
    spread.reserve(trail.size() * 4);
    for (const Vec& corner : trail) {
        for (const Vec& pad : boxShape({-clearance, -clearance, clearance,
                                        clearance}))
            spread.push_back({corner.m_x + pad.m_x, corner.m_y + pad.m_y});
    }
    const std::vector<Vec> grown = convexHull(std::move(spread));

    std::vector<Vec> sums;
    sums.reserve(grown.size() * shape.size());
    for (const Vec& corner : grown) {
        for (const Vec& v : shape)
            sums.push_back({corner.m_x - v.m_x, corner.m_y - v.m_y});
    }

    Region region;
    region.m_vertices = convexHull(std::move(sums));
    const std::size_t count = region.m_vertices.size();
    if (count < 3) return std::nullopt;

    region.m_bounds = boundsOf(region.m_vertices);
    for (std::size_t i = 0; i < count; ++i) {
        const Vec a = region.m_vertices[i];
        const Vec b = region.m_vertices[(i + 1) % count];
        const double length = std::hypot(b.m_x - a.m_x, b.m_y - a.m_y);
        if (length < EPSILON) continue;
        const Vec normal{(b.m_y - a.m_y) / length, (a.m_x - b.m_x) / length};
        region.m_normals.push_back(normal);
        region.m_offsets.push_back(normal.m_x * a.m_x + normal.m_y * a.m_y);
    }
    return region;
}

inline Vec closestToOrigin(Vec a, Vec b) {
    const double dx = b.m_x - a.m_x;
    const double dy = b.m_y - a.m_y;
    const double lengthSq = dx * dx + dy * dy;
    if (lengthSq <= 0.0) return a;
    const double t =
        std::clamp(-(a.m_x * dx + a.m_y * dy) / lengthSq, 0.0, 1.0);
    return {a.m_x + dx * t, a.m_y + dy * t};
}

inline void crossings(Vec a, Vec b, Vec c, Vec d, std::vector<Vec>& out) {
    constexpr double slack = 1e-9;
    if (std::max(a.m_x, b.m_x) < std::min(c.m_x, d.m_x) - EPSILON ||
        std::max(c.m_x, d.m_x) < std::min(a.m_x, b.m_x) - EPSILON ||
        std::max(a.m_y, b.m_y) < std::min(c.m_y, d.m_y) - EPSILON ||
        std::max(c.m_y, d.m_y) < std::min(a.m_y, b.m_y) - EPSILON)
        return;
    const Vec r{b.m_x - a.m_x, b.m_y - a.m_y};
    const Vec s{d.m_x - c.m_x, d.m_y - c.m_y};
    const double denom = r.m_x * s.m_y - r.m_y * s.m_x;
    const double lengths =
        (r.m_x * r.m_x + r.m_y * r.m_y) * (s.m_x * s.m_x + s.m_y * s.m_y);
    if (denom * denom <= lengths * 1e-24) return;
    const Vec q{c.m_x - a.m_x, c.m_y - a.m_y};
    const double t = (q.m_x * s.m_y - q.m_y * s.m_x) / denom;
    const double u = (q.m_x * r.m_y - q.m_y * r.m_x) / denom;
    if (t < -slack || t > 1.0 + slack || u < -slack || u > 1.0 + slack) return;
    out.push_back({a.m_x + r.m_x * t, a.m_y + r.m_y * t});
}

inline bool boundsTouch(const Box& a, const Box& b) {
    return a.m_minX <= b.m_maxX + EPSILON && b.m_minX <= a.m_maxX + EPSILON &&
           a.m_minY <= b.m_maxY + EPSILON && b.m_minY <= a.m_maxY + EPSILON;
}

inline double distanceFromOrigin(const Box& box) {
    const double dx = std::max({box.m_minX, 0.0, -box.m_maxX});
    const double dy = std::max({box.m_minY, 0.0, -box.m_maxY});
    return std::hypot(dx, dy);
}

inline double boundaryDistance(const Region& region) {
    double nearest = std::numeric_limits<double>::infinity();
    const auto& v = region.m_vertices;
    for (std::size_t e = 0; e < v.size(); ++e) {
        const Vec p = closestToOrigin(v[e], v[(e + 1) % v.size()]);
        nearest = std::min(nearest, std::hypot(p.m_x, p.m_y));
    }
    return nearest;
}

inline bool withinReach(const Quad& quad, const Box& extent, double clearance,
                        double reach) {
    const Box box = boundsOf(quad);
    const Box sweep{box.m_minX - clearance - extent.m_maxX,
                    box.m_minY - clearance - extent.m_maxY,
                    box.m_maxX + clearance - extent.m_minX,
                    box.m_maxY + clearance - extent.m_minY};
    return distanceFromOrigin(sweep) <= reach;
}

inline std::vector<Quad> distinctNearby(std::span<const Quad> trail,
                                        const Box& extent, double clearance,
                                        double reach) {
    std::vector<Quad> nearby;
    for (const Quad& quad : trail)
        if (withinReach(quad, extent, clearance, reach)) nearby.push_back(quad);
    const auto flat = [](const Quad& q) {
        return std::array{q[0].m_x, q[0].m_y, q[1].m_x, q[1].m_y,
                          q[2].m_x, q[2].m_y, q[3].m_x, q[3].m_y};
    };
    std::ranges::sort(nearby, {}, flat);
    const auto duplicates = std::ranges::unique(
        nearby, [&](const Quad& a, const Quad& b) { return flat(a) == flat(b); });
    nearby.erase(duplicates.begin(), duplicates.end());
    return nearby;
}

inline std::optional<Vec> nearestFreeOffset(std::vector<Region> regions,
                                            double reach = REACH) {
    if (regions.empty()) return std::nullopt;
    std::ranges::sort(regions, {},
                      [](const Region& r) { return r.m_bounds.m_minX; });

    double widest = 0.0;
    std::vector<double> nearness;
    nearness.reserve(regions.size());
    for (const Region& r : regions) {
        widest = std::max(widest, r.m_bounds.m_maxX - r.m_bounds.m_minX);
        nearness.push_back(boundaryDistance(r));
    }

    struct Source {
        double m_key;
        std::size_t m_first;
        std::size_t m_second;
    };
    std::vector<Source> sources;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (nearness[i] <= reach) sources.push_back({nearness[i], i, i});
        for (std::size_t j = i + 1; j < regions.size() &&
                                    regions[j].m_bounds.m_minX <=
                                        regions[i].m_bounds.m_maxX + EPSILON;
             ++j) {
            const double key = std::max(nearness[i], nearness[j]);
            if (key <= reach &&
                boundsTouch(regions[i].m_bounds, regions[j].m_bounds))
                sources.push_back({key, i, j});
        }
    }
    std::ranges::sort(sources, {}, &Source::m_key);

    const auto lengthSq = [](Vec v) { return v.m_x * v.m_x + v.m_y * v.m_y; };
    const auto farther = [&](Vec a, Vec b) {
        const double la = lengthSq(a);
        const double lb = lengthSq(b);
        if (la != lb) return la > lb;
        if (a.m_y != b.m_y) return a.m_y > b.m_y;
        return a.m_x > b.m_x;
    };
    std::vector<Vec> ready;
    std::vector<Vec> found;
    const auto expand = [&](const Source& source) {
        found.clear();
        const auto& a = regions[source.m_first].m_vertices;
        if (source.m_first == source.m_second) {
            for (std::size_t e = 0; e < a.size(); ++e) {
                found.push_back(a[e]);
                found.push_back(closestToOrigin(a[e], a[(e + 1) % a.size()]));
            }
        } else {
            const auto& b = regions[source.m_second].m_vertices;
            for (std::size_t e = 0; e < a.size(); ++e) {
                for (std::size_t f = 0; f < b.size(); ++f) {
                    crossings(a[e], a[(e + 1) % a.size()], b[f],
                              b[(f + 1) % b.size()], found);
                }
            }
        }
        for (const Vec& p : found) {
            ready.push_back(p);
            std::ranges::push_heap(ready, farther);
        }
    };

    const auto contactsAt = [&](Vec c) {
        const auto first = std::ranges::lower_bound(
            regions, c.m_x - widest - EPSILON, {},
            [](const Region& r) { return r.m_bounds.m_minX; });
        int contacts = 0;
        for (auto it = first; it != regions.end() &&
                              it->m_bounds.m_minX <= c.m_x + EPSILON;
             ++it) {
            if (!it->mayContain(c)) continue;
            const double depth = it->depth(c);
            if (depth < -EPSILON) return -1;
            if (depth <= EPSILON) ++contacts;
        }
        return contacts;
    };

    std::optional<Vec> single;
    std::size_t next = 0;
    while (true) {
        if (next < sources.size() &&
            (ready.empty() ||
             sources[next].m_key * sources[next].m_key <=
                 lengthSq(ready.front()))) {
            expand(sources[next++]);
            continue;
        }
        if (ready.empty()) break;
        std::ranges::pop_heap(ready, farther);
        const Vec c = ready.back();
        ready.pop_back();
        if (lengthSq(c) > reach * reach) break;

        const int contacts = contactsAt(c);
        if (contacts >= 2) return c;
        if (contacts == 1 && !single) single = c;
    }
    return single;
}

inline std::optional<Vec> snapOffset(std::span<const Vec> shape,
                                     std::span<const Quad> trail,
                                     double clearance = CLEARANCE,
                                     double reach = REACH) {
    if (shape.empty()) return std::nullopt;
    std::vector<Region> regions;
    for (const Quad& quad :
         distinctNearby(trail, boundsOf(shape), clearance, reach)) {
        if (auto region = blockedOffsets(quad, shape, clearance))
            regions.push_back(std::move(*region));
    }
    return nearestFreeOffset(std::move(regions), reach);
}

inline Quad pointQuad(Vec p) { return {p, p, p, p}; }

enum class SawRule { CornersOrCenter, Circle };

inline std::optional<Vec> sawSnapOffset(Vec center, double radius,
                                        std::span<const Box> playerBoxes,
                                        SawRule rule,
                                        double clearance = CLEARANCE,
                                        double reach = REACH) {
    if (radius <= 0.0) return std::nullopt;
    std::vector<Quad> quads;
    quads.reserve(playerBoxes.size());
    for (const Box& box : playerBoxes) quads.push_back(boxQuad(box));
    const double widest = [&] {
        double most = 0.0;
        for (const Box& box : playerBoxes) most = std::max(most, box.m_maxX - box.m_minX);
        return most;
    }();
    const Shape saw = circleShape(center, radius + widest);
    const Shape dot{center};

    std::vector<Region> regions;
    const auto block = [&](const Quad& quad, std::span<const Vec> shape) {
        if (auto region = blockedOffsets(quad, shape, clearance))
            regions.push_back(std::move(*region));
    };
    for (const Quad& quad : distinctNearby(quads, boundsOf(saw), clearance, reach)) {
        const Box box = boundsOf(quad);
        if (rule == SawRule::Circle) {
            const double half = (box.m_maxX - box.m_minX) * 0.5;
            block(pointQuad({(box.m_minX + box.m_maxX) * 0.5,
                             (box.m_minY + box.m_maxY) * 0.5}),
                  circleShape(center, radius + half));
            continue;
        }
        block(quad, dot);
        const Shape round = circleShape(center, radius);
        for (const Vec& corner : quad) block(pointQuad(corner), round);
    }
    return nearestFreeOffset(std::move(regions), reach);
}

inline std::optional<Vec> snapOffset(std::span<const Vec> shape,
                                     std::span<const Box> trail,
                                     double clearance = CLEARANCE,
                                     double reach = REACH) {
    std::vector<Quad> quads;
    quads.reserve(trail.size());
    for (const Box& box : trail) quads.push_back(boxQuad(box));
    return snapOffset(shape, std::span<const Quad>(quads), clearance, reach);
}

}
