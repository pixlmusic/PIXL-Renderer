// CPU reference for the shader's cell-boundary/min-depth contract, not a GPU benchmark.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

struct Pyramid {
    std::array<std::vector<float>, 5> depth;
    int width, height;
    Pyramid(int w, int h) : width(w), height(h) {
        for (int mip = 0; mip < 5; ++mip)
            depth[mip].resize(static_cast<size_t>(w >> mip) * (h >> mip), 10000.f);
    }
    void build() {
        for (int mip = 1; mip < 5; ++mip) {
            int w = width >> mip, h = height >> mip, previousW = width >> (mip - 1);
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                float minimum = 10000.f;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                    minimum = std::min(minimum, depth[mip - 1][(y * 2 + dy) * previousW + x * 2 + dx]);
                depth[mip][y * w + x] = minimum;
            }
        }
    }
};
struct Hit { int x = -1, y = -1, tests = 0; };
bool clip(float x, float y, float dx, float dy, float& enter, float& leave) {
    enter = 0.f; leave = 1.f;
    std::array<float, 2> start{x, y}, delta{dx, dy};
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(delta[axis]) < 1e-8f) {
            if (start[axis] < 0.001f || start[axis] > 0.999f) return false;
        } else {
            float a = (0.001f - start[axis]) / delta[axis];
            float b = (0.999f - start[axis]) / delta[axis];
            enter = std::max(enter, std::min(a, b));
            leave = std::min(leave, std::max(a, b));
        }
    }
    return enter < leave;
}
Hit trace(const Pyramid& p, float x, float y, float dx, float dy, float z0, float z1, bool hierarchy) {
    float s = 0.f;
    float epsilon = 1e-4f / std::max({std::abs(dx), std::abs(dy), 1.f});
    int mip = hierarchy ? 4 : 0;
    Hit result;
    auto depthAt = [&](float t) { return 1.f / ((1.f - t) / z0 + t / z1); };
    while (s < 1.f && result.tests < 2048) {
        ++result.tests;
        int span = 1 << mip, w = p.width >> mip, h = p.height >> mip;
        float cellSize = static_cast<float>(span);
        int cx = static_cast<int>(std::floor((x + dx * std::min(s + epsilon, 1.f)) / cellSize));
        int cy = static_cast<int>(std::floor((y + dy * std::min(s + epsilon, 1.f)) / cellSize));
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) {
            if (mip == 0) break;
            --mip; continue;
        }
        float exit = 1.f;
        if (std::abs(dx) > 1e-8f) exit = std::min(exit, (static_cast<float>((cx + (dx >= 0.f ? 1 : 0)) * span) - x) / dx);
        if (std::abs(dy) > 1e-8f) exit = std::min(exit, (static_cast<float>((cy + (dy >= 0.f ? 1 : 0)) * span) - y) / dy);
        exit = std::max(exit, s);
        float surface = p.depth[mip][cy * w + cx];
        float a = depthAt(s), b = depthAt(exit);
        constexpr float thickness = 0.001f;
        if (std::max(a, b) + thickness >= surface && mip > 0) { --mip; continue; }
        if (mip == 0 && std::min(a, b) <= surface + thickness && std::max(a, b) >= surface - thickness) {
            result.x = cx; result.y = cy; return result;
        }
        s = exit + epsilon;
        if (hierarchy) mip = std::min(mip + 1, 4);
    }
    return result;
}
int main() {
    float enter, leave;
    assert(clip(0.5f, 0.5f, 2.f, 0.f, enter, leave) && leave < 0.25f);
    assert(clip(0.5f, 0.5f, -2.f, 0.f, enter, leave) && leave < 0.25f);
    assert(clip(0.5f, 0.5f, 0.f, 2.f, enter, leave) && leave < 0.25f);
    assert(clip(0.5f, 0.5f, 0.f, -2.f, enter, leave) && leave < 0.25f);
    assert(!clip(-0.2f, 0.5f, -1.f, 0.f, enter, leave));
    assert(!clip(0.5f, 1.2f, 1.f, 0.f, enter, leave));
    assert(clip(-0.5f, 0.5f, 2.f, 0.f, enter, leave) && enter > 0.25f);
    assert(clip(0.5f, 0.5f, 0.f, 0.f, enter, leave));
    // Reciprocal interpolation must agree with perspective projection of the
    // same view-space segment, including a ray travelling toward the camera.
    for (float endZ : {40.f, 400.f}) {
        for (int i = 0; i <= 100; ++i) {
            float t = static_cast<float>(i) / 100.f;
            float z = 100.f * (1.f - t) + endZ * t;
            float screenFraction = t * endZ / z;
            float recovered = 1.f / ((1.f - screenFraction) / 100.f + screenFraction / endZ);
            assert(std::abs(recovered - z) < 0.002f);
        }
    }
    int comparisons = 0;
    for (int width : {256, 259}) for (float scale : {1.f, 2.f / 3.f, 0.5f}) {
        Pyramid p(width, 131);
        int activeWidth = static_cast<int>(static_cast<float>(width) * scale);
        // Thin foreground silhouette, sloping background and a depth edge.
        for (int y = 0; y < p.height; ++y) for (int x = 0; x < activeWidth; ++x)
            p.depth[0][y * width + x] = x == activeWidth / 2 ? 130.f : 210.f + static_cast<float>(x) * 0.04f;
        p.build();
        for (int direction : {-1, 1}) for (float endZ : {45.f, 180.f, 350.f}) for (int row = 3; row < 120; row += 7) {
            float startX = direction > 0 ? 0.5f : static_cast<float>(activeWidth) - 0.5f;
            float dx = static_cast<float>(direction * (activeWidth - 1));
            Hit a = trace(p, startX, static_cast<float>(row) + 0.5f, dx, 0.f, 90.f, endZ, true);
            Hit b = trace(p, startX, static_cast<float>(row) + 0.5f, dx, 0.f, 90.f, endZ, false);
            assert(a.x == b.x && a.y == b.y);
            ++comparisons;
        }
        // NPOT trailing columns descend instead of sampling another cell.
        auto edge = trace(p, static_cast<float>(width) - 0.5f, 130.5f, 0.f, 0.f, 50.f, 500.f, true);
        assert(edge.tests <= 5);
        for (int direction : {-1, 1}) for (float endZ : {45.f, 180.f, 350.f}) {
            float sx = direction > 0 ? 0.5f : static_cast<float>(activeWidth) - 0.5f;
            float sy = direction > 0 ? 0.5f : 125.5f;
            float dx = static_cast<float>(direction * (activeWidth - 1)), dy = static_cast<float>(direction) * 125.f;
            auto a = trace(p, sx, sy, dx, dy, 90.f, endZ, true);
            auto b = trace(p, sx, sy, dx, dy, 90.f, endZ, false);
            assert(a.x == b.x && a.y == b.y);
            ++comparisons;
        }
    }
    Pyramid empty(256, 128);
    empty.build();
    Hit skip = trace(empty, 0.5f, 20.5f, 254.f, 0.f, 50.f, 200.f, true);
    Hit linear = trace(empty, 0.5f, 20.5f, 254.f, 0.f, 50.f, 200.f, false);
    assert(skip.x == -1 && skip.tests < linear.tests / 4);
    // Zero projected length still supports intersections along view depth.
    empty.depth[0][20 * 256 + 30] = 100.f;
    empty.build();
    assert(trace(empty, 30.5f, 20.5f, 0.f, 0.f, 50.f, 200.f, true).x == 30);
    std::cout << "PASS: " << comparisons << " Hi-Z/reference comparisons; reciprocal depth, DRS, NPOT, thin surfaces, negative directions and empty skips\n";
}
