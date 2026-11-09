#include "assist/trailsnap.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace grape::trailsnap;

static int g_failures = 0;

static void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

namespace {

Shape moved(const Shape& shape, Vec by) {
    Shape out;
    for (const Vec& v : shape) out.push_back({v.m_x + by.m_x, v.m_y + by.m_y});
    return out;
}

double separation(const Shape& shape, const Box& box) {
    double best = -std::numeric_limits<double>::infinity();
    const auto project = [&](Vec axis) {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -lo;
        for (const Vec& v : shape) {
            const double d = v.m_x * axis.m_x + v.m_y * axis.m_y;
            lo = std::min(lo, d);
            hi = std::max(hi, d);
        }
        double blo = std::numeric_limits<double>::infinity();
        double bhi = -blo;
        for (const Vec& v : boxShape(box)) {
            const double d = v.m_x * axis.m_x + v.m_y * axis.m_y;
            blo = std::min(blo, d);
            bhi = std::max(bhi, d);
        }
        best = std::max(best, std::max(blo - hi, lo - bhi));
    };
    project({1.0, 0.0});
    project({0.0, 1.0});
    for (std::size_t i = 0; i < shape.size(); ++i) {
        const Vec a = shape[i];
        const Vec b = shape[(i + 1) % shape.size()];
        const double length = std::hypot(b.m_x - a.m_x, b.m_y - a.m_y);
        if (length < 1e-12) continue;
        project({(b.m_y - a.m_y) / length, (a.m_x - b.m_x) / length});
    }
    return best;
}

Box grown(const Box& box) {
    return {box.m_minX - CLEARANCE, box.m_minY - CLEARANCE,
            box.m_maxX + CLEARANCE, box.m_maxY + CLEARANCE};
}

double nearestGap(const Shape& shape, const std::vector<Box>& trail) {
    double gap = std::numeric_limits<double>::infinity();
    for (const Box& box : trail)
        gap = std::min(gap, separation(shape, grown(box)));
    return gap;
}

int touching(const Shape& shape, const std::vector<Box>& trail) {
    int count = 0;
    for (const Box& box : trail) {
        if (std::abs(separation(shape, grown(box))) < 1e-6) ++count;
    }
    return count;
}

std::optional<double> bruteForceDistance(const Box& shape,
                                         const std::vector<Box>& trail) {
    std::vector<Box> blocked;
    std::vector<double> xs{0.0};
    std::vector<double> ys{0.0};
    for (const Box& b : trail) {
        const Box f{b.m_minX - CLEARANCE - shape.m_maxX,
                    b.m_minY - CLEARANCE - shape.m_maxY,
                    b.m_maxX + CLEARANCE - shape.m_minX,
                    b.m_maxY + CLEARANCE - shape.m_minY};
        blocked.push_back(f);
        xs.push_back(f.m_minX);
        xs.push_back(f.m_maxX);
        ys.push_back(f.m_minY);
        ys.push_back(f.m_maxY);
    }

    std::optional<double> twice;
    std::optional<double> once;
    for (double x : xs) {
        for (double y : ys) {
            const double distance = std::hypot(x, y);
            if (distance > REACH) continue;
            bool inside = false;
            int contacts = 0;
            for (const Box& f : blocked) {
                const bool within = x >= f.m_minX - 1e-9 &&
                                    x <= f.m_maxX + 1e-9 &&
                                    y >= f.m_minY - 1e-9 &&
                                    y <= f.m_maxY + 1e-9;
                if (!within) continue;
                const bool interior = x > f.m_minX + 1e-9 &&
                                      x < f.m_maxX - 1e-9 &&
                                      y > f.m_minY + 1e-9 &&
                                      y < f.m_maxY - 1e-9;
                if (interior) {
                    inside = true;
                    break;
                }
                ++contacts;
            }
            if (inside || contacts == 0) continue;
            auto& slot = contacts >= 2 ? twice : once;
            if (!slot || distance < *slot) slot = distance;
        }
    }
    return twice ? twice : once;
}

std::vector<Box> jumpArc(double startX, double groundY, double step,
                         double size, int frames) {
    std::vector<Box> trail;
    const double gravity = 0.9;
    double y = groundY;
    double vy = 11.0;
    for (int i = 0; i < frames; ++i) {
        const double x = startX + step * i;
        trail.push_back({x, y, x + size, y + size});
        y += vy;
        vy -= gravity;
        if (y < groundY) {
            y = groundY;
            vy = 11.0;
        }
    }
    return trail;
}

void testStraightUp() {
    const std::vector<Box> trail{{0.0, 100.0, 30.0, 130.0}};
    const Shape spike = boxShape({10.0, 60.0, 16.0, 72.0});
    const auto offset = snapOffset(spike, trail);
    check(offset.has_value(), "lone box: snaps");
    if (!offset) return;
    check(std::abs(offset->m_x) < 1e-9, "lone box: no sideways drift");
    check(std::abs(offset->m_y - (100.0 - CLEARANCE - 72.0)) < 1e-9,
          "lone box: rises until clear of the trail bottom");
}

void testPushOut() {
    const std::vector<Box> trail{{0.0, 100.0, 30.0, 130.0}};
    const Shape spike = boxShape({10.0, 95.0, 16.0, 107.0});
    const auto offset = snapOffset(spike, trail);
    check(offset.has_value(), "overlap: snaps");
    if (!offset) return;
    check(std::abs(offset->m_y - (100.0 - CLEARANCE - 107.0)) < 1e-9,
          "overlap: pushed out the short way");
    check(nearestGap(moved(spike, *offset), trail) >= -1e-9,
          "overlap: result is clear");
}

void testStaircaseWedge() {
    std::vector<Box> trail;
    for (int i = 0; i < 10; ++i) {
        trail.push_back({10.0 * i, 100.0 - 5.0 * i, 10.0 * i + 30.0,
                         130.0 - 5.0 * i});
    }
    const Box hitbox{40.0, 40.0, 46.0, 52.0};
    const Shape spike = boxShape(hitbox);
    const auto offset = snapOffset(spike, trail);
    check(offset.has_value(), "staircase: snaps");
    if (!offset) return;
    const Shape placed = moved(spike, *offset);
    check(nearestGap(placed, trail) >= -1e-9,
          "staircase: result is clear of every trail box");
    check(touching(placed, trail) >= 2,
          "staircase: wedged against two trail boxes");
    const auto expected = bruteForceDistance(hitbox, trail);
    check(expected && std::abs(std::hypot(offset->m_x, offset->m_y) -
                               *expected) < 1e-7,
          "staircase: nearest wedge");
}

void testIdempotent() {
    const auto trail = jumpArc(0.0, 100.0, 1.3, 30.0, 400);
    const Shape spike = boxShape({120.0, 90.0, 126.0, 102.0});
    const auto first = snapOffset(spike, trail);
    check(first.has_value(), "idempotent: snaps");
    if (!first) return;
    const auto second = snapOffset(moved(spike, *first), trail);
    check(second && std::hypot(second->m_x, second->m_y) < 1e-6,
          "idempotent: second snap stays put");
}

void testOutOfReach() {
    const std::vector<Box> trail{{0.0, 1000.0, 30.0, 1030.0}};
    const Shape spike = boxShape({10.0, 60.0, 16.0, 72.0});
    check(!snapOffset(spike, trail).has_value(),
          "far trail: object left alone");
    check(!snapOffset(spike, std::vector<Box>{}).has_value(),
          "empty trail: object left alone");
}

void testRandomBoxesAgainstBruteForce() {
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    int compared = 0;
    for (int round = 0; round < 400; ++round) {
        std::vector<Box> trail;
        double x = 0.0;
        double y = 100.0;
        const double size = 7.5 + unit(rng) * 30.0;
        const int frames = 5 + static_cast<int>(unit(rng) * 60);
        for (int i = 0; i < frames; ++i) {
            trail.push_back({x, y, x + size, y + size});
            x += 0.5 + unit(rng) * 9.0;
            y += (unit(rng) - 0.5) * 20.0;
        }
        const double w = 2.0 + unit(rng) * 30.0;
        const double h = 2.0 + unit(rng) * 30.0;
        const double sx = unit(rng) * x;
        const double sy = y - 60.0 + unit(rng) * 120.0;
        const Box hitbox{sx, sy, sx + w, sy + h};
        const Shape shape = boxShape(hitbox);

        const auto offset = snapOffset(shape, trail);
        const auto expected = bruteForceDistance(hitbox, trail);
        check(offset.has_value() == expected.has_value(),
              "random boxes: same verdict on whether to move #" +
                  std::to_string(round));
        if (!offset || !expected) continue;
        ++compared;
        const double got = std::hypot(offset->m_x, offset->m_y);
        check(std::abs(got - *expected) < 1e-6,
              "random boxes: nearest placement #" + std::to_string(round) +
                  " got " + std::to_string(got) + " expected " +
                  std::to_string(*expected));
        check(nearestGap(moved(shape, *offset), trail) >= -1e-7,
              "random boxes: clear of trail #" + std::to_string(round));
    }
    check(compared > 300, "random boxes: most rounds produced a snap");
}

void testRandomPolygonsStayClear() {
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    int wedged = 0;
    int snapped = 0;
    for (int round = 0; round < 300; ++round) {
        const auto trail =
            jumpArc(0.0, 100.0, 0.8 + unit(rng) * 4.0, 30.0, 300);
        const double cx = 20.0 + unit(rng) * 200.0;
        const double cy = 60.0 + unit(rng) * 120.0;
        Shape shape;
        switch (round % 3) {
            case 0:
                shape = {{cx, cy}, {cx + 30.0, cy}, {cx, cy + 30.0}};
                break;
            case 1:
                shape = circleShape({cx, cy}, 4.0 + unit(rng) * 30.0);
                break;
            default: {
                const double a = unit(rng) * std::numbers::pi;
                const double c = std::cos(a);
                const double s = std::sin(a);
                for (Vec v : Shape{{-3, -6}, {3, -6}, {3, 6}, {-3, 6}})
                    shape.push_back({cx + v.m_x * c - v.m_y * s,
                                     cy + v.m_x * s + v.m_y * c});
            }
        }
        const auto offset = snapOffset(shape, trail);
        if (!offset) continue;
        ++snapped;
        const Shape placed = moved(shape, *offset);
        const double gap = nearestGap(placed, trail);
        check(gap >= -1e-7,
              "polygons: clear of trail #" + std::to_string(round) +
                  " gap " + std::to_string(gap));
        check(gap <= 1e-6,
              "polygons: actually touching #" + std::to_string(round) +
                  " gap " + std::to_string(gap));
        if (touching(placed, trail) >= 2) ++wedged;
    }
    check(snapped > 200, "polygons: most rounds produced a snap");
    check(wedged > snapped / 2, "polygons: mostly wedged between two boxes");
}

double polygonSeparation(const Shape& a, const Shape& b) {
    double best = -std::numeric_limits<double>::infinity();
    for (const Shape* owner : {&a, &b}) {
        for (std::size_t i = 0; i < owner->size(); ++i) {
            const Vec p = (*owner)[i];
            const Vec q = (*owner)[(i + 1) % owner->size()];
            const double length = std::hypot(q.m_x - p.m_x, q.m_y - p.m_y);
            if (length < 1e-12) continue;
            const Vec axis{(q.m_y - p.m_y) / length, (p.m_x - q.m_x) / length};
            double alo = std::numeric_limits<double>::infinity();
            double ahi = -alo;
            double blo = alo;
            double bhi = -alo;
            for (const Vec& v : a) {
                const double d = v.m_x * axis.m_x + v.m_y * axis.m_y;
                alo = std::min(alo, d);
                ahi = std::max(ahi, d);
            }
            for (const Vec& v : b) {
                const double d = v.m_x * axis.m_x + v.m_y * axis.m_y;
                blo = std::min(blo, d);
                bhi = std::max(bhi, d);
            }
            best = std::max(best, std::max(blo - ahi, alo - bhi));
        }
    }
    return best;
}

Shape grownQuad(const Quad& quad) {
    std::vector<Vec> points;
    for (const Vec& corner : quad) {
        for (const Vec& pad : boxShape({-CLEARANCE, -CLEARANCE, CLEARANCE,
                                        CLEARANCE}))
            points.push_back({corner.m_x + pad.m_x, corner.m_y + pad.m_y});
    }
    return convexHull(std::move(points));
}

std::vector<Quad> spinningArc(double step, double spin, int frames) {
    std::vector<Quad> trail;
    const double gravity = 0.9;
    double y = 115.0;
    double vy = 11.0;
    double angle = 0.0;
    for (int i = 0; i < frames; ++i) {
        const double cx = 15.0 + step * i;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        Quad quad;
        const Vec local[4] = {{-15, -15}, {15, -15}, {15, 15}, {-15, 15}};
        for (int k = 0; k < 4; ++k) {
            quad[k] = {cx + local[k].m_x * c - local[k].m_y * s,
                       y + local[k].m_x * s + local[k].m_y * c};
        }
        trail.push_back(quad);
        y += vy;
        vy -= gravity;
        angle -= spin;
        if (y < 115.0) {
            y = 115.0;
            vy = 11.0;
        }
    }
    return trail;
}

void testSawsAgainstRotatedTrail() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    int snapped = 0;
    int wedged = 0;
    int stable = 0;
    for (int round = 0; round < 60; ++round) {
        const auto trail = spinningArc(0.8 + unit(rng) * 4.0,
                                       0.02 + unit(rng) * 0.2, 300);
        std::vector<Shape> grown;
        for (const Quad& quad : trail) grown.push_back(grownQuad(quad));

        const Shape saw = circleShape({20.0 + unit(rng) * 200.0,
                                       60.0 + unit(rng) * 120.0},
                                      4.0 + unit(rng) * 30.0);
        const auto offset = snapOffset(saw, std::span<const Quad>(trail));
        if (!offset) continue;
        ++snapped;
        const Shape placed = moved(saw, *offset);
        double gap = std::numeric_limits<double>::infinity();
        int contacts = 0;
        for (const Shape& g : grown) {
            const double sep = polygonSeparation(placed, g);
            gap = std::min(gap, sep);
            if (std::abs(sep) < 1e-6) ++contacts;
        }
        check(gap >= -1e-7, "rotated trail: saw clear of trail #" +
                                std::to_string(round) + " gap " +
                                std::to_string(gap));
        check(gap <= 1e-6, "rotated trail: saw touching trail #" +
                               std::to_string(round) + " gap " +
                               std::to_string(gap));
        if (contacts >= 2) ++wedged;
        const auto again = snapOffset(placed, std::span<const Quad>(trail));
        if (again && std::hypot(again->m_x, again->m_y) < 1e-6) ++stable;
    }
    check(snapped > 45, "rotated trail: most saws snapped");
    check(wedged > snapped / 2, "rotated trail: mostly wedged");
    check(stable == snapped, "rotated trail: second snap stays put");
}

bool sawKills(Vec center, double radius, const Box& box, SawRule rule) {
    if (rule == SawRule::Circle) {
        const double half = (box.m_maxX - box.m_minX) * 0.5;
        return std::hypot(center.m_x - (box.m_minX + box.m_maxX) * 0.5,
                          center.m_y - (box.m_minY + box.m_maxY) * 0.5) <=
               radius + half;
    }
    if (center.m_x >= box.m_minX && center.m_x <= box.m_maxX &&
        center.m_y >= box.m_minY && center.m_y <= box.m_maxY)
        return true;
    for (const Vec& corner : boxShape(box))
        if (std::hypot(corner.m_x - center.m_x, corner.m_y - center.m_y) < radius)
            return true;
    return false;
}

void testSawCircleRule() {
    const std::vector<Box> trail{{0.0, 100.0, 30.0, 130.0}};
    const auto offset = sawSnapOffset({15.0, 40.0}, 20.0, trail, SawRule::Circle);
    check(offset.has_value(), "saw circle rule: snaps");
    if (!offset) return;
    check(std::abs(offset->m_x) < 1e-6, "saw circle rule: no sideways drift");
    const double gap = 115.0 - (40.0 + offset->m_y);
    check(gap >= 35.0 && gap <= 35.4, "saw circle rule: touches the player circle");
}

void testSawCornerRule() {
    const std::vector<Box> trail{{0.0, 100.0, 30.0, 130.0}};
    const auto offset =
        sawSnapOffset({15.0, 40.0}, 20.0, trail, SawRule::CornersOrCenter);
    check(offset.has_value(), "saw corner rule: snaps");
    if (!offset) return;
    const Vec placed{15.0 + offset->m_x, 40.0 + offset->m_y};
    check(!sawKills(placed, 20.0, trail[0], SawRule::CornersOrCenter),
          "saw corner rule: player survives");
    check(placed.m_y > 86.0 && placed.m_y < 100.0 - std::sqrt(175.0),
          "saw corner rule: sinks between the corners like the game allows");
}

void testSawsStaySafe() {
    const auto trail = jumpArc(0.0, 100.0, 1.3, 30.0, 400);
    std::mt19937 random(7);
    std::uniform_real_distribution<double> x(40.0, 480.0);
    std::uniform_real_distribution<double> y(20.0, 260.0);
    std::uniform_real_distribution<double> r(6.0, 45.0);
    int snapped = 0;
    int safe = 0;
    for (int i = 0; i < 200; ++i) {
        const SawRule rule = i % 2 ? SawRule::Circle : SawRule::CornersOrCenter;
        const Vec center{x(random), y(random)};
        const double radius = r(random);
        const auto offset = sawSnapOffset(center, radius, trail, rule);
        if (!offset) continue;
        ++snapped;
        const Vec placed{center.m_x + offset->m_x, center.m_y + offset->m_y};
        bool kills = false;
        for (const Box& box : trail) kills = kills || sawKills(placed, radius, box, rule);
        if (!kills) ++safe;
    }
    check(snapped > 100, "saw rules: most saws snapped");
    check(safe == snapped, "saw rules: no snapped saw kills the player");
}

void testLongTrailIsFast() {
    auto trail = jumpArc(0.0, 100.0, 1.3, 30.0, 8000);
    const auto second = jumpArc(0.0, 100.0, 1.3, 9.0, 8000);
    trail.insert(trail.end(), second.begin(), second.end());
    const auto start = std::chrono::steady_clock::now();
    int snapped = 0;
    for (int i = 0; i < 50; ++i) {
        const double x = 200.0 + i * 180.0;
        if (snapOffset(circleShape({x, 80.0}, 20.0), trail)) ++snapped;
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    std::printf("50 saws against %zu trail boxes: %.1f ms\n", trail.size(),
                ms);
    check(snapped == 50, "long trail: every saw snapped");
    check(ms < 2000.0, "long trail: fast enough for a button press");
}

}

int main() {
    testStraightUp();
    testPushOut();
    testStaircaseWedge();
    testIdempotent();
    testOutOfReach();
    testRandomBoxesAgainstBruteForce();
    testRandomPolygonsStayClear();
    testSawsAgainstRotatedTrail();
    testSawCircleRule();
    testSawCornerRule();
    testSawsStaySafe();
    testLongTrailIsFast();
    if (g_failures == 0) std::printf("trailsnap: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
