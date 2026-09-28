// Stochastic FDN Modulation milestone validation harness.
//
// Diagnostic (not part of CTest).  Renders the canonical presets through the
// three modulation architectures (A legacy, B reduced periodic, C multiphase
// stochastic) and reports:
//   * per-line delay excursion at several modDepth values (48 kHz)
//   * pairwise cross-correlation of the per-line modulation trajectories
//   * long-tail macro metrics: peak, RMS windows, RT60, spectral drift, clicks
//   * Spectral Feedback Guard activity (min gain, % below 0.99 / 0.95)
//   * a tonal warble metric from late-tail instantaneous-frequency tracking
//   * freeze behaviour
//
// It only consumes the production/test access API of CloudGreyVerb.

#include "cloud_grey_verb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kSr = 48000.0f;
constexpr size_t kOrder = CGV_FDN_ORDER;

using Mode = CloudGreyVerb::ModulationMode;
using Preset = CloudGreyVerb::Preset;

const char* modeName(Mode mode) {
    switch (mode) {
        case Mode::Legacy: return "A: Legacy periodic";
        case Mode::ReducedPeriodic: return "B: Reduced periodic";
        case Mode::MultiphaseStochastic: return "C: Multiphase stoch";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Small DSP helpers
// ---------------------------------------------------------------------------

void fftRadix2(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * -1.0;
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[i + j];
                const std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

class Bandpass {
public:
    void init(float f0, float sampleRate, float q = 4.0f) {
        const float w0 = static_cast<float>(2.0 * kPi * f0 / sampleRate);
        const float cosw0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        b0_ = alpha / a0;
        b1_ = 0.0f;
        b2_ = -alpha / a0;
        a1_ = (-2.0f * cosw0) / a0;
        a2_ = (1.0f - alpha) / a0;
        reset();
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }
    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }
private:
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};

class Biquad {
public:
    void initLowpass(float freq, float sampleRate, float q = 0.7071f) {
        const float w0 = static_cast<float>(2.0 * kPi * freq / sampleRate);
        const float cosw0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        setCoeffs(((1.0f - cosw0) * 0.5f) / a0, (1.0f - cosw0) / a0,
                  ((1.0f - cosw0) * 0.5f) / a0, (-2.0f * cosw0) / a0, (1.0f - alpha) / a0);
    }
    void initHighpass(float freq, float sampleRate, float q = 0.7071f) {
        const float w0 = static_cast<float>(2.0 * kPi * freq / sampleRate);
        const float cosw0 = std::cos(w0);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float a0 = 1.0f + alpha;
        setCoeffs(((1.0f + cosw0) * 0.5f) / a0, (-(1.0f + cosw0)) / a0,
                  ((1.0f + cosw0) * 0.5f) / a0, (-2.0f * cosw0) / a0, (1.0f - alpha) / a0);
    }
    void setCoeffs(float b0, float b1, float b2, float a1, float a2) {
        b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
        reset();
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }
    float process(float x) {
        const float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }
private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};

struct BandSplitter {
    Biquad hpLow, lpLow, hpMid, lpMid, hpHigh, lpHigh;
    void init(float sampleRate) {
        hpLow.initHighpass(20.0f, sampleRate);   lpLow.initLowpass(250.0f, sampleRate);
        hpMid.initHighpass(250.0f, sampleRate);  lpMid.initLowpass(3000.0f, sampleRate);
        hpHigh.initHighpass(3000.0f, sampleRate); lpHigh.initLowpass(15000.0f, sampleRate);
    }
    void process(float in, float& low, float& mid, float& high) {
        low = lpLow.process(hpLow.process(in));
        mid = lpMid.process(hpMid.process(in));
        high = lpHigh.process(hpHigh.process(in));
    }
};

double pearson(const std::vector<float>& a, const std::vector<float>& b, size_t begin, size_t end) {
    const size_t n = end - begin;
    if (n < 2) return 0.0;
    double ma = 0.0, mb = 0.0;
    for (size_t i = begin; i < end; ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(n); mb /= static_cast<double>(n);
    double va = 0.0, vb = 0.0, cov = 0.0;
    for (size_t i = begin; i < end; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        va += da * da; vb += db * db; cov += da * db;
    }
    if (va < 1e-30 || vb < 1e-30) return 0.0;
    return cov / std::sqrt(va * vb);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

enum class Excite { Impulse, SineBurst, Pluck, Freeze, Silence };

struct RenderResult {
    std::vector<float> l, r;
    std::array<std::vector<float>, kOrder> offsetMs; // per-line applied modulation
    bool finite = true;
};

void fillExcitation(std::vector<float>& mono, Excite excite, float sampleRate, float f0) {
    const size_t n = mono.size();
    mono.assign(n, 0.0f);
    const size_t burst = static_cast<size_t>(0.150f * sampleRate);
    switch (excite) {
        case Excite::Impulse:
            mono[0] = 0.70710678f;
            break;
        case Excite::Silence:
            break;
        case Excite::SineBurst: {
            for (size_t i = 0; i < burst && i < n; ++i) {
                const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (burst - 1)));
                mono[i] = static_cast<float>(0.5 * w * std::sin(2.0 * kPi * f0 * i / sampleRate));
            }
            break;
        }
        case Excite::Pluck: {
            // Exponentially damped harmonic stack, plucked-string like.
            const size_t len = static_cast<size_t>(0.6f * sampleRate);
            for (size_t i = 0; i < len && i < n; ++i) {
                const double t = static_cast<double>(i) / sampleRate;
                const double env = std::exp(-9.0 * t);
                mono[i] = static_cast<float>(0.5 * env *
                    (std::sin(2.0 * kPi * 220.0 * t) + 0.35 * std::sin(2.0 * kPi * 440.0 * t) +
                     0.18 * std::sin(2.0 * kPi * 660.0 * t)));
            }
            break;
        }
        case Excite::Freeze: {
            cgv_dsp::FastPRNG prng; prng.seed(424242);
            const size_t gen = static_cast<size_t>(0.200f * sampleRate);
            for (size_t i = 0; i < gen && i < n; ++i) {
                const double t = static_cast<double>(i) / sampleRate;
                const double env = std::sin(kPi * t / 0.200);
                const double noise = prng.randFloat() * 2.0 - 1.0;
                mono[i] = static_cast<float>(env * 0.707 *
                    (0.5 * std::sin(2.0 * kPi * 440.0 * t) + 0.5 * noise));
            }
            break;
        }
    }
}

RenderResult render(Preset preset, Mode mode, float sampleRate, float seconds,
                    Excite excite, bool freeze, bool recordOffset, float modDepthOverride = -1.0f,
                    float f0 = 440.0f) {
    RenderResult out;
    const auto fp = CloudGreyVerb::getFactoryPreset(preset);
    auto p = fp.dsp;
    p.mix = 1.0f;
    p.clipOutput = false;
    if (modDepthOverride >= 0.0f) p.modDepth = modDepthOverride;
    if (freeze) p.freeze = 0.0f; // engage after excitation

    std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(sampleRate), 0.0f);
    CloudGreyVerb verb;
    verb.init(sampleRate, memory.data(), memory.size());
    verb.setParams(p);
    CloudGreyVerbComponentTestAccess::setModulationMode(verb, mode);
    verb.reset();

    const size_t count = static_cast<size_t>(seconds * sampleRate);
    std::vector<float> mono(count, 0.0f);
    fillExcitation(mono, excite, sampleRate, f0);

    out.l.assign(count, 0.0f);
    out.r.assign(count, 0.0f);
    if (recordOffset)
        for (auto& v : out.offsetMs) v.assign(count, 0.0f);

    const size_t freezeEngage = static_cast<size_t>(0.200f * sampleRate);
    for (size_t i = 0; i < count; ++i) {
        if (freeze && i == freezeEngage) { p.freeze = 1.0f; verb.setParams(p); }
        float l = 0.0f, r = 0.0f;
        verb.processSample(mono[i], mono[i], l, r);
        if (!std::isfinite(l) || !std::isfinite(r) ||
            std::abs(l) > 10.0f || std::abs(r) > 10.0f) out.finite = false;
        out.l[i] = l;
        out.r[i] = r;
        if (recordOffset)
            for (size_t line = 0; line < kOrder; ++line)
                out.offsetMs[line][i] = CloudGreyVerbComponentTestAccess::getLineDelayOffsetMs(verb, static_cast<int>(line));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Long-tail metrics
// ---------------------------------------------------------------------------

struct TailMetrics {
    double peak = 0.0;
    double rmsOverall = 0.0;
    double rms_1_4 = 0.0;
    double rms_16_20 = 0.0;
    double rt60 = 0.0;
    double meanSpectralDrift = 0.0;
    double maxSpectralDrift = 0.0;
    double centroid10 = 0.0;
    double centroid18 = 0.0;
    int clicks = 0;
    int growthWarns = 0;
    float minGain[3] = {1.0f, 1.0f, 1.0f};
    float pct99[3] = {0.0f, 0.0f, 0.0f};
    float pct95[3] = {0.0f, 0.0f, 0.0f};
};

double spectralCentroid(const std::vector<float>& l, const std::vector<float>& r,
                        size_t center, float sampleRate) {
    constexpr size_t N = 2048;
    if (center < N / 2 || center + N / 2 > l.size()) return 0.0;
    std::vector<std::complex<double>> buf(N);
    for (size_t i = 0; i < N; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (N - 1)));
        buf[i] = 0.5 * (static_cast<double>(l[center - N / 2 + i]) +
                        static_cast<double>(r[center - N / 2 + i])) * w;
    }
    fftRadix2(buf);
    double sumFreqMag = 0.0, sumMag = 0.0;
    for (size_t k = 1; k < N / 2; ++k) {
        const double mag = std::abs(buf[k]);
        sumFreqMag += static_cast<double>(k) * sampleRate / N * mag;
        sumMag += mag;
    }
    return sumMag > 1e-12 ? sumFreqMag / sumMag : 0.0;
}

TailMetrics analyzeTail(CloudGreyVerb::Preset preset, Mode mode, Excite excite, bool freeze,
                        float sampleRate = kSr, float seconds = 20.0f) {
    TailMetrics m;
    const auto fp = CloudGreyVerb::getFactoryPreset(preset);
    auto p = fp.dsp;
    p.mix = 1.0f;
    p.clipOutput = false;
    if (freeze) p.freeze = 0.0f;

    std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(sampleRate), 0.0f);
    CloudGreyVerb verb;
    verb.init(sampleRate, memory.data(), memory.size());
    verb.setParams(p);
    CloudGreyVerbComponentTestAccess::setModulationMode(verb, mode);
    verb.reset();
    CloudGreyVerbComponentTestAccess::enableGuardMetrics(verb, true);
    CloudGreyVerbComponentTestAccess::resetGuardMetrics(verb);

    const size_t count = static_cast<size_t>(seconds * sampleRate);
    std::vector<float> mono(count, 0.0f);
    fillExcitation(mono, excite, sampleRate, 440.0f);

    BandSplitter splitL, splitR;
    splitL.init(sampleRate); splitR.init(sampleRate);

    std::vector<double> energy(count, 0.0), lowE(count, 0.0), midE(count, 0.0), highE(count, 0.0);
    const size_t freezeEngage = static_cast<size_t>(0.200f * sampleRate);
    for (size_t i = 0; i < count; ++i) {
        if (freeze && i == freezeEngage) { p.freeze = 1.0f; verb.setParams(p); }
        float l = 0.0f, r = 0.0f;
        verb.processSample(mono[i], mono[i], l, r);
        const double e = static_cast<double>(l) * l + static_cast<double>(r) * r;
        energy[i] = e;
        m.peak = std::max({m.peak, std::abs(static_cast<double>(l)), std::abs(static_cast<double>(r))});
        float bl, bm, bh, bl2, bm2, bh2;
        splitL.process(l, bl, bm, bh);
        splitR.process(r, bl2, bm2, bh2);
        lowE[i] = static_cast<double>(bl) * bl + static_cast<double>(bl2) * bl2;
        midE[i] = static_cast<double>(bm) * bm + static_cast<double>(bm2) * bm2;
        highE[i] = static_cast<double>(bh) * bh + static_cast<double>(bh2) * bh2;
    }

    const auto& gm = CloudGreyVerbComponentTestAccess::getGuardMetrics(verb);
    for (int b = 0; b < 3; ++b) {
        m.minGain[b] = gm.minGain[b];
        m.pct99[b] = gm.pctBelow99(static_cast<size_t>(b));
        m.pct95[b] = gm.pctBelow95(static_cast<size_t>(b));
    }

    double sum = 0.0;
    std::vector<double> cum(count + 1, 0.0);
    for (size_t i = 0; i < count; ++i) { sum += energy[i]; cum[i + 1] = cum[i] + energy[i]; }
    m.rmsOverall = std::sqrt(sum / (2.0 * count));
    auto windowRms = [&](size_t a, size_t b) {
        b = std::min(b, count);
        if (a >= b) return 0.0;
        return std::sqrt((cum[b] - cum[a]) / (2.0 * (b - a)));
    };
    m.rms_1_4 = windowRms(static_cast<size_t>(1.0f * sampleRate), static_cast<size_t>(4.0f * sampleRate));
    m.rms_16_20 = windowRms(static_cast<size_t>(16.0f * sampleRate), count);

    // RT60 via Schroeder reverse integration.
    std::vector<double> decay(count, 0.0);
    double cumulative = 0.0;
    for (size_t i = count; i-- > 0;) { cumulative += energy[i]; decay[i] = cumulative; }
    if (cumulative > 0.0) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; size_t n = 0;
        for (size_t i = 0; i < count; ++i) {
            const double db = 10.0 * std::log10(std::max(1.0e-30, decay[i] / decay[0]));
            if (db <= -5.0 && db >= -25.0) {
                const double t = static_cast<double>(i) / sampleRate;
                sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
            }
        }
        const double d = static_cast<double>(n) * sxx - sx * sx;
        if (n > 100 && std::abs(d) > 1e-12) {
            const double slope = (static_cast<double>(n) * sxy - sx * sy) / d;
            if (slope < 0.0) m.rt60 = -60.0 / slope;
        }
    }

    // Spectral drift across 500 ms blocks after 1.5 s.
    const size_t block = static_cast<size_t>(0.500f * sampleRate);
    const size_t totalBlocks = count / block;
    std::vector<std::array<double, 3>> bands(totalBlocks, {0.0, 0.0, 0.0});
    std::vector<double> blockEnergy(totalBlocks, 0.0);
    for (size_t b = 0; b < totalBlocks; ++b) {
        const size_t a = b * block, e = std::min(count, a + block);
        double bl = 0, bm = 0, bh = 0;
        for (size_t i = a; i < e; ++i) { bl += lowE[i]; bm += midE[i]; bh += highE[i]; }
        blockEnergy[b] = cum[e] - cum[a];
        const double tot = bl + bm + bh + 1e-15;
        bands[b] = {bl / tot, bm / tot, bh / tot};
    }
    const size_t startBlock = static_cast<size_t>(1.500f * sampleRate) / block + 1;
    double driftSum = 0.0; int driftCount = 0;
    for (size_t b = startBlock; b < totalBlocks; ++b) {
        if (blockEnergy[b] > 1e-14 && blockEnergy[b - 1] > 1e-14) {
            const double d0 = bands[b][0] - bands[b - 1][0];
            const double d1 = bands[b][1] - bands[b - 1][1];
            const double d2 = bands[b][2] - bands[b - 1][2];
            const double d = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
            driftSum += d; ++driftCount;
            m.maxSpectralDrift = std::max(m.maxSpectralDrift, d);
        }
    }
    m.meanSpectralDrift = driftCount > 0 ? driftSum / driftCount : 0.0;

    for (size_t b = startBlock; b < totalBlocks; ++b) {
        if (blockEnergy[b - 1] > 1e-12 && blockEnergy[b] > 1e-12) {
            const double ratio = blockEnergy[b] / blockEnergy[b - 1];
            if (10.0 * std::log10(ratio) > 0.5) m.growthWarns++;
        }
    }

    return m;
}

void computeCentroids(const RenderResult& res, float sampleRate, double& c10, double& c18) {
    c10 = spectralCentroid(res.l, res.r, static_cast<size_t>(10.0f * sampleRate), sampleRate);
    c18 = spectralCentroid(res.l, res.r, static_cast<size_t>(18.0f * sampleRate), sampleRate);
}

// ---------------------------------------------------------------------------
// Warble metric: track the dominant mode near the excitation frequency in the
// late tail and measure how much / how periodically it wanders.
// ---------------------------------------------------------------------------

struct WarbleMetrics {
    double stdHz = 0.0;
    double stdCents = 0.0;
    double periodicity = 0.0;     // normalized autocorrelation peak in 0.1-3 Hz
    double dominantHz = 0.0;      // frequency of the tracked wander
    size_t points = 0;
};

WarbleMetrics analyzeWarble(const std::vector<float>& l, const std::vector<float>& r,
                            float sampleRate, float f0) {
    WarbleMetrics out;
    constexpr size_t N = 16384;
    const size_t hop = 4096;
    const size_t begin = static_cast<size_t>(0.5f * sampleRate);
    const size_t end = static_cast<size_t>(5.0f * sampleRate);
    if (end <= begin + N) return out;

    // Pre-filter the whole signal once so overlapping windows see a coherent
    // band-passed version of the tail.
    Bandpass bpL, bpR;
    bpL.init(f0, sampleRate, 4.0f);
    bpR.init(f0, sampleRate, 4.0f);
    std::vector<float> filt(l.size(), 0.0f);
    for (size_t i = 0; i < l.size(); ++i)
        filt[i] = 0.5f * (bpL.process(l[i]) + bpR.process(r[i]));

    std::vector<double> track;
    const size_t loBin = static_cast<size_t>(f0 * 0.88f * N / sampleRate);
    const size_t hiBin = static_cast<size_t>(f0 * 1.12f * N / sampleRate);
    for (size_t start = begin; start + N <= end; start += hop) {
        std::vector<std::complex<double>> buf(N);
        for (size_t i = 0; i < N; ++i) {
            const double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (N - 1)));
            buf[i] = static_cast<double>(filt[start + i]) * w;
        }
        fftRadix2(buf);
        size_t peak = loBin;
        double peakMag = 0.0;
        for (size_t k = loBin; k <= hiBin && k < N / 2; ++k) {
            const double mag = std::abs(buf[k]);
            if (mag > peakMag) { peakMag = mag; peak = k; }
        }
        if (peakMag < 1e-9) continue;
        // Parabolic interpolation around the peak bin.
        const double y0 = std::abs(buf[peak - 1]);
        const double y1 = std::abs(buf[peak]);
        const double y2 = std::abs(buf[peak + 1]);
        double delta = 0.0;
        const double denom = (y0 - 2.0 * y1 + y2);
        if (std::abs(denom) > 1e-18) delta = 0.5 * (y0 - y2) / denom;
        if (delta > 0.5) delta = 0.5;
        if (delta < -0.5) delta = -0.5;
        track.push_back((static_cast<double>(peak) + delta) * sampleRate / N);
    }
    out.points = track.size();
    if (track.size() < 8) return out;

    double mean = 0.0;
    for (double v : track) mean += v;
    mean /= static_cast<double>(track.size());
    double var = 0.0;
    for (double v : track) var += (v - mean) * (v - mean);
    var /= static_cast<double>(track.size());
    out.stdHz = std::sqrt(var);
    if (mean > 0.0) out.stdCents = 1200.0 * std::log2((mean + out.stdHz) / mean);

    // Autocorrelation of the mean-removed track; track sample rate = sr / hop.
    const double trackRate = sampleRate / static_cast<double>(hop);
    const size_t n = track.size();
    double best = 0.0, bestHz = 0.0;
    for (size_t lag = 1; lag < n / 2; ++lag) {
        const double hz = trackRate / static_cast<double>(lag);
        if (hz < 0.05 || hz > 3.0) continue;
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i + lag < n; ++i) {
            num += (track[i] - mean) * (track[i + lag] - mean);
            den += (track[i] - mean) * (track[i] - mean);
        }
        if (den < 1e-18) continue;
        const double r = num / den;
        if (r > best) { best = r; bestHz = hz; }
    }
    out.periodicity = best;
    out.dominantHz = bestHz;
    return out;
}

// ---------------------------------------------------------------------------
// Reporting helpers
// ---------------------------------------------------------------------------

void printSeparator() { std::cout << "-----------------------------------------------------------------\n"; }

} // namespace

int main() {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "=================================================================\n";
    std::cout << "   STOCHASTIC FDN MODULATION VALIDATION - 48 kHz                  \n";
    std::cout << "=================================================================\n\n";

    const std::array<Mode, 3> modes = {Mode::Legacy, Mode::ReducedPeriodic, Mode::MultiphaseStochastic};

    // --- 1. Excursion audit (per-line, ms) ---------------------------------
    std::cout << "### 1. MAX PER-LINE DELAY EXCURSION (ms), 30 s render, BrightCloud\n\n";
    std::cout << "| modDepth | Mode | line0 | line1 | line2 | line3 | global |\n";
    std::cout << "|:---------|:-----|:------|:------|:------|:------|:-------|\n";
    for (float depth : {0.25f, 0.50f, 0.75f, 1.00f}) {
        for (Mode mode : modes) {
            auto res = render(Preset::BrightCloud, mode, kSr, 30.0f, Excite::Silence, false, true, depth);
            double g = 0.0;
            double lineMax[kOrder];
            for (size_t i = 0; i < kOrder; ++i) {
                double mx = 0.0;
                for (float v : res.offsetMs[i]) mx = std::max(mx, std::abs(static_cast<double>(v)));
                lineMax[i] = mx;
                g = std::max(g, mx);
            }
            std::cout << "| " << std::setprecision(2) << depth << " | " << std::setw(20) << modeName(mode) << " | ";
            for (size_t i = 0; i < kOrder; ++i)
                std::cout << std::setprecision(3) << std::setw(5) << lineMax[i] << " | ";
            std::cout << std::setprecision(3) << g << " |\n";
        }
    }
    std::cout << "\n";

    // --- 2. Cross-correlation of the per-line modulation signals -----------
    std::cout << "### 2. PER-LINE MODULATION CROSS-CORRELATION (Pearson, 30 s, modDepth=0.5)\n\n";
    std::cout << "| Preset | Mode | pairs | max |corr| | mean |corr| | worst pair |\n";
    std::cout << "|:-------|:-----|:------|:-----------|:------------|:-----------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (Mode mode : modes) {
            auto res = render(preset, mode, kSr, 30.0f, Excite::Silence, false, true, 0.5f);
            double maxC = 0.0, sumC = 0.0; int pairs = 0; std::string worst = "-";
            for (size_t i = 0; i < kOrder; ++i) {
                for (size_t j = i + 1; j < kOrder; ++j) {
                    const double c = std::abs(pearson(res.offsetMs[i], res.offsetMs[j], 0, res.offsetMs[i].size()));
                    sumC += c; ++pairs;
                    if (c > maxC) { maxC = c; worst = "L" + std::to_string(i) + "-L" + std::to_string(j); }
                }
            }
            std::cout << "| " << std::setw(16) << CloudGreyVerb::getFactoryPreset(preset).name
                      << " | " << std::setw(20) << modeName(mode) << " | " << std::setw(5) << pairs
                      << " | " << std::setprecision(3) << std::setw(10) << maxC
                      << " | " << std::setw(11) << (pairs ? sumC / pairs : 0.0)
                      << " | " << std::setw(10) << worst << " |\n";
        }
    }
    std::cout << "\n";

    // --- 3. Warble metric ---------------------------------------------------
    std::cout << "### 3. WARBLE METRIC (late-tail peak frequency tracking, 0.5-5 s)\n\n";
    std::cout << "| Preset | f0 | Mode | std (Hz) | std (cents) | periodicity | dom (Hz) |\n";
    std::cout << "|:-------|:---|:-----|:---------|:------------|:------------|:---------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (float f0 : {440.0f, 1000.0f}) {
            for (Mode mode : modes) {
                auto res = render(preset, mode, kSr, 10.0f, Excite::SineBurst, false, false, -1.0f, f0);
                auto w = analyzeWarble(res.l, res.r, kSr, f0);
                std::cout << "| " << std::setw(16) << CloudGreyVerb::getFactoryPreset(preset).name
                          << " | " << std::setw(4) << static_cast<int>(f0)
                          << " | " << std::setw(20) << modeName(mode)
                          << " | " << std::setprecision(3) << std::setw(8) << w.stdHz
                          << " | " << std::setw(11) << w.stdCents
                          << " | " << std::setw(11) << w.periodicity
                          << " | " << std::setw(8) << w.dominantHz << " |\n";
            }
        }
    }
    std::cout << "\n";

    // --- 4. Long-tail A/B/C (impulse, 20 s) --------------------------------
    std::cout << "### 4. LONG TAIL - IMPULSE 20 s\n\n";
    std::cout << "| Preset | Mode | Peak | RMS | RT60 (s) | MeanDrift | Cent10 | Cent18 | Clicks | Growth |\n";
    std::cout << "|:-------|:-----|:-----|:----|:---------|:----------|:-------|:-------|:-------|:-------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (Mode mode : modes) {
            auto res = render(preset, mode, kSr, 20.0f, Excite::Impulse, false, false);
            auto m = analyzeTail(preset, mode, Excite::Impulse, false);
            double c10 = 0.0, c18 = 0.0;
            computeCentroids(res, kSr, c10, c18);
            std::cout << "| " << std::setw(16) << CloudGreyVerb::getFactoryPreset(preset).name
                      << " | " << std::setw(20) << modeName(mode)
                      << " | " << std::scientific << std::setprecision(3) << m.peak
                      << " | " << m.rmsOverall
                      << " | " << std::fixed << std::setprecision(2) << m.rt60
                      << " | " << std::setprecision(4) << m.meanSpectralDrift
                      << " | " << std::setprecision(0) << c10
                      << " | " << c18
                      << " | " << m.clicks
                      << " | " << m.growthWarns << " |\n";
        }
    }
    std::cout << "\n";

    // --- 4b. Long-tail A/B/C (pluck burst, 10 s) ---------------------------
    std::cout << "### 4b. LONG TAIL - PLUCK / HARP BURST 10 s\n\n";
    std::cout << "| Preset | Mode | Peak | RMS | Clicks | Growth |\n";
    std::cout << "|:-------|:-----|:-----|:----|:-------|:-------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (Mode mode : modes) {
            auto m = analyzeTail(preset, mode, Excite::Pluck, false, kSr, 10.0f);
            std::cout << "| " << std::setw(16) << CloudGreyVerb::getFactoryPreset(preset).name
                      << " | " << std::setw(20) << modeName(mode)
                      << " | " << std::scientific << std::setprecision(3) << m.peak
                      << " | " << m.rmsOverall
                      << " | " << std::fixed << m.clicks
                      << " | " << m.growthWarns << " |\n";
        }
    }
    std::cout << "\n";

    // --- 5. Spectral Guard activity (normal use, impulse 20 s) -------------
    std::cout << "### 5. SPECTRAL GUARD ACTIVITY - IMPULSE 20 s\n\n";
    std::cout << "| Preset | Mode | MinGain L/M/H | %<0.99 L/M/H | %<0.95 L/M/H |\n";
    std::cout << "|:-------|:-----|:--------------|:-------------|:-------------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (Mode mode : modes) {
            auto m = analyzeTail(preset, mode, Excite::Impulse, false);
            std::printf("| %-16s | %-20s | %.4f / %.4f / %.4f | %.2f%% / %.2f%% / %.2f%% | %.2f%% / %.2f%% / %.2f%% |\n",
                        CloudGreyVerb::getFactoryPreset(preset).name, modeName(mode),
                        m.minGain[0], m.minGain[1], m.minGain[2],
                        m.pct99[0], m.pct99[1], m.pct99[2], m.pct95[0], m.pct95[1], m.pct95[2]);
        }
    }
    std::cout << "\n";

    // --- 6. Freeze ----------------------------------------------------------
    std::cout << "### 6. FREEZE 20 s (200 ms excitation + freeze)\n\n";
    std::cout << "| Preset | Mode | Peak | RMS 1-4 | RMS 16-20 | MeanDrift | Growth | Guard %<0.99 M | MinGain M |\n";
    std::cout << "|:-------|:-----|:-----|:--------|:----------|:----------|:-------|:---------------|:----------|\n";
    for (Preset preset : {Preset::BrightCloud, Preset::DarkLongCloud, Preset::GreyholeDelayVerb}) {
        for (Mode mode : modes) {
            auto m = analyzeTail(preset, mode, Excite::Freeze, true);
            std::cout << "| " << std::setw(16) << CloudGreyVerb::getFactoryPreset(preset).name
                      << " | " << std::setw(20) << modeName(mode)
                      << " | " << std::scientific << std::setprecision(3) << m.peak
                      << " | " << m.rms_1_4
                      << " | " << m.rms_16_20
                      << " | " << std::fixed << std::setprecision(4) << m.meanSpectralDrift
                      << " | " << m.growthWarns
                      << " | " << std::setprecision(2) << m.pct99[1]
                      << " | " << std::setprecision(4) << m.minGain[1] << " |\n";
        }
    }
    std::cout << "\n";

    std::cout << "Done.\n";
    return 0;
}
