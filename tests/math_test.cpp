#include "city/generator.hpp"
#include "core/math.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;

TEST_CASE("Vec2 basics") {
    REQUIRE(length({3.0f, 4.0f}) == Approx(5.0f));
    REQUIRE(dot({1, 0}, {0, 1}) == 0.0f);
    REQUIRE(cross({1, 0}, {0, 1}) == 1.0f);
    REQUIRE(length(normalize({0.0f, 0.0f})) == 0.0f); // no NaN on zero vector
    REQUIRE(length(normalize({10.0f, 0.0f})) == Approx(1.0f));

    const Vec2 r = rotate({1.0f, 0.0f}, citygen::kPi * 0.5f);
    REQUIRE(r.x == Approx(0.0f).margin(1e-5f));
    REQUIRE(r.y == Approx(1.0f));
}

TEST_CASE("shoelace area is signed by winding") {
    const std::vector<Vec2> ccw{{0, 0}, {4, 0}, {4, 3}, {0, 3}};
    REQUIRE(citygen::shoelace(ccw) == Approx(12.0f));
    const std::vector<Vec2> cw(ccw.rbegin(), ccw.rend());
    REQUIRE(citygen::shoelace(cw) == Approx(-12.0f));
}

TEST_CASE("PointGrid nearest matches brute force") {
    citygen::Rng      rng(7);
    const float       size = 1000.0f;
    std::vector<Vec2> pts;
    for (int i = 0; i < 200; ++i) pts.push_back({rng.uni(0, size), rng.uni(0, size)});

    citygen::PointGrid grid;
    grid.build(pts, size, 50.0f);

    for (int q = 0; q < 100; ++q) {
        const Vec2 p{rng.uni(0, size), rng.uni(0, size)};
        uint32_t   best = 0;
        for (uint32_t i = 1; i < pts.size(); ++i)
            if (length2(pts[i] - p) < length2(pts[best] - p)) best = i;
        REQUIRE(grid.nearest(pts, p) == best);
    }
}

TEST_CASE("noise is deterministic and in range") {
    for (int i = 0; i < 100; ++i) {
        const Vec2  p{float(i) * 0.37f, float(i) * 0.91f};
        const float a = citygen::fbm(p, 5u);
        REQUIRE(a == citygen::fbm(p, 5u));
        REQUIRE(a >= 0.0f);
        REQUIRE(a <= 1.0f);
    }
}
