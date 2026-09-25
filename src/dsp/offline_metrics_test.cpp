#include "offline_metrics.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace { int failures = 0; void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } } }
int main() {
    constexpr double sr = 48000.0; constexpr std::size_t n = 48000;
    constexpr double expectedSineRms = 0.7071067811865476;
    constexpr double rmsTolerance = 0.005;
    constexpr double centroidToleranceHz = 12.0;
    std::cout << std::setprecision(10);
    const double frequencies[] = {60, 440, 2000, 8000}; const int expected[] = {0, 2, 3, 4};
    for (int t = 0; t < 4; ++t) {
        std::vector<float> x(n); for (std::size_t i = 0; i < n; ++i) x[i] = std::sin(2.0 * 3.141592653589793 * frequencies[t] * i / sr);
        const auto s = OfflineMetrics::welchStereo(x, x, sr, {0, n});
        int dominant = 0; for (int b = 1; b < 5; ++b) if (s.bandRms[b] > s.bandRms[dominant]) dominant = b;
        check(dominant == expected[t], "sine must dominate its named band");
        if (frequencies[t] == 440.0) {
            check(std::abs(s.bandRms[dominant] - expectedSineRms) < rmsTolerance,
                  "Welch band RMS calibration");
            check(std::abs(s.centroidHz - frequencies[t]) < centroidToleranceHz,
                  "Welch spectral centroid calibration");
            std::cout << "440Hz calibration: expected_rms=" << expectedSineRms
                      << ", measured_rms=" << s.bandRms[dominant]
                      << ", error_percent=" << 100.0 * std::abs(s.bandRms[dominant] - expectedSineRms) / expectedSineRms
                      << ", rms_tolerance=" << rmsTolerance << ", centroid_tolerance_hz=" << centroidToleranceHz << '\n';
        }
        std::cout << frequencies[t] << "Hz: band " << dominant << ", band_rms=" << s.bandRms[dominant]
                  << ", centroid=" << s.centroidHz << "Hz, windows=" << s.windows << '\n';
    }
    std::vector<float> l(n), r(n); for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = std::sin(2.0 * 3.141592653589793 * 440 * i / sr);
    auto same = OfflineMetrics::stereoMetrics(l, r, {0, n}); check(std::abs(same.monoDeltaDb) < 1e-10 && same.sideRms < 1e-12, "L=R mono convention");
    for (std::size_t i = 0; i < n; ++i) r[i] = -l[i];
    auto opposite = OfflineMetrics::stereoMetrics(l, r, {0, n}); check(opposite.monoDeltaDb < -500.0, "L=-R must cancel in mono");
    std::vector<float> impulse(n); impulse[100] = 1.0f; const auto regions = OfflineMetrics::findRegions(impulse, sr);
    check(regions.active.begin == 100 && regions.active.end == 101, "impulse active region");
    check(regions.tail.begin > regions.active.end && regions.tail.end == n, "tail region must follow guarded active region");
    std::vector<float> silence(n, 0.0f); const auto silentRegions = OfflineMetrics::findRegions(silence, sr);
    check(silentRegions.full.begin == 0 && silentRegions.full.end == n, "silence full region");
    check(silentRegions.active.begin == 0 && silentRegions.active.end == 0, "silence has no active region");
    check(silentRegions.tail.begin == n && silentRegions.tail.end == n, "silence has no tail region");
    const auto silentSpectrum = OfflineMetrics::welchStereo(silence, silence, sr, silentRegions.full);
    check(silentSpectrum.centroidHz == 0.0, "silence spectral centroid");
    for (double band : silentSpectrum.bandRms) check(band == 0.0, "silence spectral band");
    return failures ? 1 : 0;
}
