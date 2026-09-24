#include "offline_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace OfflineMetrics {
namespace {
constexpr double pi = 3.14159265358979323846;

void fft(std::array<std::complex<double>, fftSize>& x) {
    for (std::size_t i = 1, j = 0; i < fftSize; ++i) {
        std::size_t bit = fftSize >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    for (std::size_t length = 2; length <= fftSize; length <<= 1) {
        const auto step = std::polar(1.0, -2.0 * pi / static_cast<double>(length));
        for (std::size_t i = 0; i < fftSize; i += length) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto u = x[i + j], v = x[i + j + length / 2] * w;
                x[i + j] = u + v; x[i + j + length / 2] = u - v; w *= step;
            }
        }
    }
}

Region clamp(Region r, std::size_t size) {
    r.begin = std::min(r.begin, size); r.end = std::min(std::max(r.end, r.begin), size); return r;
}
} // namespace

double dbRatio(double numerator, double denominator) {
    return 20.0 * std::log10(std::max(1.0e-30, numerator) / std::max(1.0e-30, denominator));
}

Spectrum welchStereo(const std::vector<float>& left, const std::vector<float>& right,
                     double sampleRate, Region requested) {
    Spectrum result;
    const auto size = std::min(left.size(), right.size());
    const auto region = clamp(requested, size);
    if (region.end == region.begin) return result;
    std::array<double, fftSize / 2 + 1> meanPower{};
    std::array<std::complex<double>, fftSize> bins{};
    for (std::size_t start = region.begin; start < region.end; start += hopSize) {
        const auto count = std::min(fftSize, region.end - start);
        if (count < 2) break;
        double windowPower = 0.0;
        std::array<double, fftSize / 2 + 1> stereoPower{};
        for (int channel = 0; channel < 2; ++channel) {
            bins.fill({});
            const auto& signal = channel == 0 ? left : right;
            double thisWindowPower = 0.0;
            for (std::size_t i = 0; i < count; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * pi * i / static_cast<double>(count - 1));
                bins[i] = static_cast<double>(signal[start + i]) * w;
                thisWindowPower += w * w;
            }
            if (channel == 0) windowPower = thisWindowPower;
            fft(bins);
            for (std::size_t k = 0; k <= fftSize / 2; ++k) {
                const double oneSided = (k == 0 || k == fftSize / 2) ? 1.0 : 2.0;
                stereoPower[k] += 0.5 * oneSided * std::norm(bins[k]) / std::max(1.0, thisWindowPower * fftSize);
            }
        }
        (void)windowPower;
        for (std::size_t k = 0; k < meanPower.size(); ++k) meanPower[k] += stereoPower[k];
        ++result.windows;
        if (count < fftSize) break;
    }
    if (!result.windows) return result;
    const double limits[] = {20, 80, 200, 1000, 4000, 16000};
    double total = 0.0, weighted = 0.0;
    for (std::size_t k = 1; k < meanPower.size(); ++k) {
        const double power = meanPower[k] / result.windows;
        const double hz = k * sampleRate / fftSize;
        if (hz >= 20.0 && hz < std::min(16000.0, sampleRate * 0.5)) { total += power; weighted += hz * power; }
        for (int b = 0; b < 5; ++b)
            if (hz >= limits[b] && hz < limits[b + 1]) { result.bandRms[b] += power; break; }
    }
    result.centroidHz = total > 0.0 ? weighted / total : 0.0;
    for (auto& band : result.bandRms) band = std::sqrt(band);
    return result;
}

Stereo stereoMetrics(const std::vector<float>& left, const std::vector<float>& right, Region requested) {
    Stereo m; const auto region = clamp(requested, std::min(left.size(), right.size()));
    double energy = 0.0, mid = 0.0, side = 0.0;
    for (auto i = region.begin; i < region.end; ++i) {
        const double l = left[i], r = right[i], md = 0.5 * (l + r), sd = 0.5 * (l - r);
        energy += 0.5 * (l * l + r * r); mid += md * md; side += sd * sd;
    }
    const double n = std::max<std::size_t>(1, region.end - region.begin);
    m.energyRms = std::sqrt(energy / n); m.midRms = std::sqrt(mid / n); m.sideRms = std::sqrt(side / n);
    m.sideMidRatio = m.sideRms / std::max(1.0e-30, m.midRms);
    m.monoDeltaDb = dbRatio(m.midRms, m.energyRms);
    return m;
}

Regions findRegions(const std::vector<float>& input, double sampleRate, double thresholdDb) {
    Regions r{{0, input.size()}, {0, 0}, {input.size(), input.size()}};
    double peak = 0.0; for (float x : input) peak = std::max(peak, std::abs(static_cast<double>(x)));
    if (peak == 0.0) return r;
    const double threshold = peak * std::pow(10.0, thresholdDb / 20.0);
    auto first = input.size(), last = std::size_t{0};
    for (std::size_t i = 0; i < input.size(); ++i) if (std::abs(input[i]) > threshold) { first = std::min(first, i); last = i + 1; }
    if (first == input.size()) return r;
    r.active = {first, last};
    const auto guard = static_cast<std::size_t>(std::llround(sampleRate * tailGuardSeconds));
    r.tail = {std::min(input.size(), last + guard), input.size()};
    return r;
}
} // namespace OfflineMetrics
