#include "offline_metrics.hpp"
#include <cmath>
#include <iostream>
#include <vector>

namespace { int failures = 0; void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } } }
int main() {
    constexpr double sr = 48000.0; constexpr std::size_t n = 48000;
    const double frequencies[] = {60, 440, 2000, 8000}; const int expected[] = {0, 2, 3, 4};
    for (int t = 0; t < 4; ++t) {
        std::vector<float> x(n); for (std::size_t i = 0; i < n; ++i) x[i] = std::sin(2.0 * 3.141592653589793 * frequencies[t] * i / sr);
        const auto s = OfflineMetrics::welchStereo(x, x, sr, {0, n});
        int dominant = 0; for (int b = 1; b < 5; ++b) if (s.bandRms[b] > s.bandRms[dominant]) dominant = b;
        check(dominant == expected[t], "sine must dominate its named band");
        std::cout << frequencies[t] << "Hz: band " << dominant << ", centroid=" << s.centroidHz << "Hz, windows=" << s.windows << '\n';
    }
    std::vector<float> l(n), r(n); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = std::sin(2.0 * 3.141592653589793 * 440 * i / sr);
    auto same = OfflineMetrics::stereoMetrics(l, r, {0, n}); check(std::abs(same.monoDeltaDb) < 1e-10 && same.sideRms < 1e-12, "L=R mono convention");
    for (std::size_t i = 0; i < n; ++i) r[i] = -l[i];
    auto opposite = OfflineMetrics::stereoMetrics(l, r, {0, n}); check(opposite.monoDeltaDb < -500.0, "L=-R must cancel in mono");
    std::vector<float> impulse(n); impulse[100] = 1.0f; const auto regions = OfflineMetrics::findRegions(impulse, sr);
    check(regions.active.begin == 100 && regions.active.end == 101, "impulse active region");
    check(regions.tail.begin > regions.active.end && regions.tail.end == n, "tail region must follow guarded active region");
    return failures ? 1 : 0;
}
