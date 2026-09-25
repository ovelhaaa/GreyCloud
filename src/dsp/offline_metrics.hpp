#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace OfflineMetrics {

constexpr std::size_t fftSize = 4096;
constexpr std::size_t hopSize = fftSize / 2;
constexpr double tailGuardSeconds = 0.075;

struct Region { std::size_t begin = 0, end = 0; };
struct Spectrum {
    double centroidHz = 0.0;
    std::array<double, 5> bandRms{};
    std::size_t windows = 0;
};
struct Stereo {
    double energyRms = 0.0, midRms = 0.0, sideRms = 0.0;
    double sideMidRatio = 0.0, monoDeltaDb = 0.0;
};
struct Regions { Region full, active, tail; };

Spectrum welchStereo(const std::vector<float>& left, const std::vector<float>& right,
                     double sampleRate, Region region);
Stereo stereoMetrics(const std::vector<float>& left, const std::vector<float>& right,
                     Region region);
Regions findRegions(const std::vector<float>& input, double sampleRate,
                    double relativeThresholdDb = -60.0);
double dbRatio(double numerator, double denominator);

} // namespace OfflineMetrics
