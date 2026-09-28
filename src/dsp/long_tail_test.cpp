#include "cloud_grey_verb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

class Biquad {
public:
    void initLowpass(float freq, float sampleRate, float q = 0.7071f) {
        float w0 = static_cast<float>(2.0 * kPi * freq / sampleRate);
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * q);
        float b0 = (1.0f - cosw0) * 0.5f;
        float b1 = 1.0f - cosw0;
        float b2 = (1.0f - cosw0) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;
        setCoeffs(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
    }
    void initHighpass(float freq, float sampleRate, float q = 0.7071f) {
        float w0 = static_cast<float>(2.0 * kPi * freq / sampleRate);
        float cosw0 = std::cos(w0);
        float alpha = std::sin(w0) / (2.0f * q);
        float b0 = (1.0f + cosw0) * 0.5f;
        float b1 = -(1.0f + cosw0);
        float b2 = (1.0f + cosw0) * 0.5f;
        float a0 = 1.0f + alpha;
        float a1 = -2.0f * cosw0;
        float a2 = 1.0f - alpha;
        setCoeffs(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0);
    }
    void setCoeffs(float b0, float b1, float b2, float a1, float a2) {
        b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
        reset();
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }
    float process(float x) {
        float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }
private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f, y1_ = 0.0f, y2_ = 0.0f;
};

struct BandSplitter {
    Biquad lpLow, hpLow;     // 20 Hz - 250 Hz
    Biquad hpMid, lpMid;     // 250 Hz - 3000 Hz
    Biquad hpHigh, lpHigh;   // 3000 Hz - 15000 Hz

    void init(float sampleRate) {
        hpLow.initHighpass(20.0f, sampleRate);
        lpLow.initLowpass(250.0f, sampleRate);

        hpMid.initHighpass(250.0f, sampleRate);
        lpMid.initLowpass(3000.0f, sampleRate);

        hpHigh.initHighpass(3000.0f, sampleRate);
        lpHigh.initLowpass(15000.0f, sampleRate);
    }

    void reset() {
        hpLow.reset(); lpLow.reset();
        hpMid.reset(); lpMid.reset();
        hpHigh.reset(); lpHigh.reset();
    }

    void process(float in, float& low, float& mid, float& high) {
        low = lpLow.process(hpLow.process(in));
        mid = lpMid.process(hpMid.process(in));
        high = lpHigh.process(hpHigh.process(in));
    }
};

void fftRadix2(std::vector<std::complex<double>>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * kPi / len * -1.0;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<double> u = a[i + j];
                std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

double computeSpectralCentroid(const std::vector<float>& l, const std::vector<float>& r,
                               size_t centerSample, float sampleRate) {
    constexpr size_t N = 2048;
    if (centerSample < N / 2 || centerSample + N / 2 > l.size()) return 0.0;
    size_t startSample = centerSample - N / 2;
    std::vector<std::complex<double>> buf(N);
    for (size_t i = 0; i < N; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * kPi * i / (N - 1))); // Hann
        double mono = 0.5 * (static_cast<double>(l[startSample + i]) + static_cast<double>(r[startSample + i]));
        buf[i] = mono * w;
    }
    fftRadix2(buf);
    double sumFreqMag = 0.0;
    double sumMag = 0.0;
    for (size_t k = 1; k < N / 2; ++k) {
        double mag = std::abs(buf[k]);
        double freq = static_cast<double>(k) * sampleRate / N;
        sumFreqMag += freq * mag;
        sumMag += mag;
    }
    return sumMag > 1e-12 ? (sumFreqMag / sumMag) : 0.0;
}

struct LongTailMetrics {
    std::string presetName;
    std::string condition;
    bool modulation = true;
    bool nonlinearities = true;

    double peak = 0.0;
    double rmsOverall = 0.0;
    double crestFactor = 0.0;

    // Windowed RMS
    double rms_0_1s = 0.0;
    double rms_1_4s = 0.0;
    double rms_4_8s = 0.0;
    double rms_8_12s = 0.0;
    double rms_12_16s = 0.0;
    double rms_16_20s = 0.0;

    // Energy percentages per band across whole tail (0-20s)
    double lowPctOverall = 0.0;
    double midPctOverall = 0.0;
    double highPctOverall = 0.0;

    // Energy percentages in early vs late tail
    double lowPct_1_4s = 0.0, midPct_1_4s = 0.0, highPct_1_4s = 0.0;
    double lowPct_8_12s = 0.0, midPct_8_12s = 0.0, highPct_8_12s = 0.0;
    double lowPct_16_20s = 0.0, midPct_16_20s = 0.0, highPct_16_20s = 0.0;

    // Spectral centroid at different stages of tail (Hz)
    double centroid_0_5s = 0.0;
    double centroid_2s = 0.0;
    double centroid_6s = 0.0;
    double centroid_10s = 0.0;
    double centroid_14s = 0.0;
    double centroid_18s = 0.0;

    // Spectral drift (500 ms windows)
    double meanSpectralDrift = 0.0;
    double maxSpectralDrift = 0.0;

    // Instabilities
    int unexpectedGrowths = 0;
    double maxGrowthDb = 0.0;
    double maxGrowthTimeS = 0.0;

    int clickCount = 0;
    double maxClickDelta = 0.0;

    double rt60Seconds = 0.0;
    bool stable = true;

    // Spectral Guard Activity (Test-only instrumentation)
    float minGainLow = 1.0f;
    float minGainMid = 1.0f;
    float minGainHigh = 1.0f;
    float maxEnergyLow = 0.0f;
    float maxEnergyMid = 0.0f;
    float maxEnergyHigh = 0.0f;
    float pctBelow99Low = 0.0f;
    float pctBelow99Mid = 0.0f;
    float pctBelow99High = 0.0f;
    float pctBelow95Low = 0.0f;
    float pctBelow95Mid = 0.0f;
    float pctBelow95High = 0.0f;
};

LongTailMetrics analyzeLongTail(CloudGreyVerb::Preset preset, bool mod, bool nonlin,
                               CloudGreyVerb::FeedbackArchitecture arch = CloudGreyVerb::FeedbackArchitecture::CurrentTapeClip,
                               float sampleRate = 48000.0f, float durationSeconds = 20.0f,
                               bool freezeMode = false) {
    LongTailMetrics m;
    const auto fp = CloudGreyVerb::getFactoryPreset(preset);
    m.presetName = fp.name;
    m.modulation = mod;
    m.nonlinearities = nonlin;

    std::string condStr = "";
    if (arch == CloudGreyVerb::FeedbackArchitecture::CurrentTapeClip) condStr += "[Arch A: Current] ";
    else if (arch == CloudGreyVerb::FeedbackArchitecture::SpectralGuardOnly) condStr += "[Arch B: SpecGuard] ";
    else if (arch == CloudGreyVerb::FeedbackArchitecture::WeakSaturationSpectralGuard) condStr += "[Arch C: WeakSat+Guard] ";
    condStr += (mod ? "Mod:ON " : "Mod:OFF ");
    condStr += (nonlin ? "Nonlin:ON" : "Nonlin:OFF");
    if (freezeMode) condStr += " (Freeze)";
    m.condition = condStr;

    auto p = fp.dsp;
    p.mix = 1.0f;
    p.clipOutput = false;
    if (freezeMode) {
        p.freeze = 0.0f; // start normal, engage freeze after excitation
    }

    std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(sampleRate), 0.0f);
    CloudGreyVerb verb;
    verb.init(sampleRate, memory.data(), memory.size());
    verb.setParams(p);
    CloudGreyVerbComponentTestAccess::setFeedbackArchitecture(verb, arch);
    CloudGreyVerbComponentTestAccess::setModulationEnabled(verb, mod);
    CloudGreyVerbComponentTestAccess::setNonlinearitiesEnabled(verb, nonlin);
    verb.reset();
    CloudGreyVerbComponentTestAccess::enableGuardMetrics(verb, true);
    CloudGreyVerbComponentTestAccess::resetGuardMetrics(verb);

    const size_t count = static_cast<size_t>(durationSeconds * sampleRate);
    std::vector<float> l(count, 0.0f);
    std::vector<float> r(count, 0.0f);
    std::vector<double> energy(count, 0.0);

    // Band signals
    BandSplitter splitL, splitR;
    splitL.init(sampleRate);
    splitR.init(sampleRate);

    std::vector<float> lowL(count, 0.0f), lowR(count, 0.0f);
    std::vector<float> midL(count, 0.0f), midR(count, 0.0f);
    std::vector<float> highL(count, 0.0f), highR(count, 0.0f);

    double sumEnergy = 0.0;
    double sumLowEnergy = 0.0;
    double sumMidEnergy = 0.0;
    double sumHighEnergy = 0.0;

    const size_t freezeEngageSample = static_cast<size_t>(0.200f * sampleRate);
    cgv_dsp::FastPRNG excitationPrng;
    excitationPrng.seed(424242);

    for (size_t i = 0; i < count; ++i) {
        float in = 0.0f;
        if (!freezeMode) {
            in = (i == 0) ? 0.70710678f : 0.0f; // Unit impulse
        } else {
            // For freeze: inject 200 ms of dense excitation, then engage freeze
            if (i < freezeEngageSample) {
                float t = static_cast<float>(i) / sampleRate;
                float env = std::sin(static_cast<float>(kPi) * t / 0.200f);
                float noise = (excitationPrng.randFloat() * 2.0f - 1.0f);
                in = env * 0.707f * (0.5f * std::sin(2.0f * static_cast<float>(kPi) * 440.0f * t) + 0.5f * noise);
            } else if (i == freezeEngageSample) {
                p.freeze = 1.0f;
                verb.setParams(p);
            }
        }
        float outL = 0.0f, outR = 0.0f;
        verb.processSample(in, in, outL, outR);

        if (!std::isfinite(outL) || !std::isfinite(outR) || std::abs(outL) > 10.0f || std::abs(outR) > 10.0f) {
            m.stable = false;
        }

        l[i] = outL;
        r[i] = outR;
        double e = static_cast<double>(outL) * outL + static_cast<double>(outR) * outR;
        energy[i] = e;
        sumEnergy += e;
        m.peak = std::max({m.peak, static_cast<double>(std::abs(outL)), static_cast<double>(std::abs(outR))});

        float bLowL, bMidL, bHighL;
        float bLowR, bMidR, bHighR;
        splitL.process(outL, bLowL, bMidL, bHighL);
        splitR.process(outR, bLowR, bMidR, bHighR);

        lowL[i] = bLowL; lowR[i] = bLowR;
        midL[i] = bMidL; midR[i] = bMidR;
        highL[i] = bHighL; highR[i] = bHighR;

        sumLowEnergy += static_cast<double>(bLowL) * bLowL + static_cast<double>(bLowR) * bLowR;
        sumMidEnergy += static_cast<double>(bMidL) * bMidL + static_cast<double>(bMidR) * bMidR;
        sumHighEnergy += static_cast<double>(bHighL) * bHighL + static_cast<double>(bHighR) * bHighR;
    }

    const auto& gm = CloudGreyVerbComponentTestAccess::getGuardMetrics(verb);
    m.minGainLow = gm.minGain[0];
    m.minGainMid = gm.minGain[1];
    m.minGainHigh = gm.minGain[2];
    m.maxEnergyLow = gm.maxEnergy[0];
    m.maxEnergyMid = gm.maxEnergy[1];
    m.maxEnergyHigh = gm.maxEnergy[2];
    m.pctBelow99Low = gm.pctBelow99(0);
    m.pctBelow99Mid = gm.pctBelow99(1);
    m.pctBelow99High = gm.pctBelow99(2);
    m.pctBelow95Low = gm.pctBelow95(0);
    m.pctBelow95Mid = gm.pctBelow95(1);
    m.pctBelow95High = gm.pctBelow95(2);

    m.rmsOverall = std::sqrt(sumEnergy / (2.0 * count));
    m.crestFactor = (m.rmsOverall > 1e-12) ? (m.peak / m.rmsOverall) : 0.0;

    double bandTotal = sumLowEnergy + sumMidEnergy + sumHighEnergy + 1e-15;
    m.lowPctOverall = 100.0 * sumLowEnergy / bandTotal;
    m.midPctOverall = 100.0 * sumMidEnergy / bandTotal;
    m.highPctOverall = 100.0 * sumHighEnergy / bandTotal;

    // Cumulative sums for O(1) window queries
    std::vector<double> cumEnergy(count + 1, 0.0);
    std::vector<double> cumLow(count + 1, 0.0);
    std::vector<double> cumMid(count + 1, 0.0);
    std::vector<double> cumHigh(count + 1, 0.0);
    for (size_t i = 0; i < count; ++i) {
        cumEnergy[i + 1] = cumEnergy[i] + energy[i];
        cumLow[i + 1] = cumLow[i] + (static_cast<double>(lowL[i]) * lowL[i] + static_cast<double>(lowR[i]) * lowR[i]);
        cumMid[i + 1] = cumMid[i] + (static_cast<double>(midL[i]) * midL[i] + static_cast<double>(midR[i]) * midR[i]);
        cumHigh[i + 1] = cumHigh[i] + (static_cast<double>(highL[i]) * highL[i] + static_cast<double>(highR[i]) * highR[i]);
    }

    auto calcWindowRms = [&](size_t startSample, size_t endSample) {
        endSample = std::min(endSample, count);
        if (startSample >= endSample) return 0.0;
        double winSum = cumEnergy[endSample] - cumEnergy[startSample];
        return std::sqrt(winSum / (2.0 * (endSample - startSample)));
    };

    auto calcBandPctWindow = [&](size_t startSample, size_t endSample, double& lowP, double& midP, double& highP) {
        endSample = std::min(endSample, count);
        if (startSample >= endSample) { lowP = midP = highP = 0.0; return; }
        double eL = cumLow[endSample] - cumLow[startSample];
        double eM = cumMid[endSample] - cumMid[startSample];
        double eH = cumHigh[endSample] - cumHigh[startSample];
        double tot = eL + eM + eH + 1e-15;
        lowP = 100.0 * eL / tot;
        midP = 100.0 * eM / tot;
        highP = 100.0 * eH / tot;
    };

    m.rms_0_1s   = calcWindowRms(0, static_cast<size_t>(1.0f * sampleRate));
    m.rms_1_4s   = calcWindowRms(static_cast<size_t>(1.0f * sampleRate), static_cast<size_t>(4.0f * sampleRate));
    m.rms_4_8s   = calcWindowRms(static_cast<size_t>(4.0f * sampleRate), static_cast<size_t>(8.0f * sampleRate));
    m.rms_8_12s  = calcWindowRms(static_cast<size_t>(8.0f * sampleRate), static_cast<size_t>(12.0f * sampleRate));
    m.rms_12_16s = calcWindowRms(static_cast<size_t>(12.0f * sampleRate), static_cast<size_t>(16.0f * sampleRate));
    m.rms_16_20s = calcWindowRms(static_cast<size_t>(16.0f * sampleRate), static_cast<size_t>(20.0f * sampleRate));

    calcBandPctWindow(static_cast<size_t>(1.0f * sampleRate), static_cast<size_t>(4.0f * sampleRate),
                      m.lowPct_1_4s, m.midPct_1_4s, m.highPct_1_4s);
    calcBandPctWindow(static_cast<size_t>(8.0f * sampleRate), static_cast<size_t>(12.0f * sampleRate),
                      m.lowPct_8_12s, m.midPct_8_12s, m.highPct_8_12s);
    calcBandPctWindow(static_cast<size_t>(16.0f * sampleRate), static_cast<size_t>(20.0f * sampleRate),
                      m.lowPct_16_20s, m.midPct_16_20s, m.highPct_16_20s);

    // Spectral centroid at time points
    m.centroid_0_5s = computeSpectralCentroid(l, r, static_cast<size_t>(0.5f * sampleRate), sampleRate);
    m.centroid_2s   = computeSpectralCentroid(l, r, static_cast<size_t>(2.0f * sampleRate), sampleRate);
    m.centroid_6s   = computeSpectralCentroid(l, r, static_cast<size_t>(6.0f * sampleRate), sampleRate);
    m.centroid_10s  = computeSpectralCentroid(l, r, static_cast<size_t>(10.0f * sampleRate), sampleRate);
    m.centroid_14s  = computeSpectralCentroid(l, r, static_cast<size_t>(14.0f * sampleRate), sampleRate);
    m.centroid_18s  = computeSpectralCentroid(l, r, static_cast<size_t>(18.0f * sampleRate), sampleRate);

    // 1. Detect unexpected energy growth in 500ms blocks after 1.5s
    const size_t blockSize = static_cast<size_t>(0.500f * sampleRate);
    const size_t startBlock = static_cast<size_t>(1.500f * sampleRate) / blockSize;
    const size_t totalBlocks = count / blockSize;

    std::vector<double> blockEnergy(totalBlocks, 0.0);
    std::vector<std::array<double, 3>> blockBands(totalBlocks, {0.0, 0.0, 0.0});

    for (size_t b = 0; b < totalBlocks; ++b) {
        size_t bStart = b * blockSize;
        size_t bEnd = std::min(count, bStart + blockSize);
        double bSum = cumEnergy[bEnd] - cumEnergy[bStart];
        double bLow = cumLow[bEnd] - cumLow[bStart];
        double bMid = cumMid[bEnd] - cumMid[bStart];
        double bHigh = cumHigh[bEnd] - cumHigh[bStart];
        blockEnergy[b] = bSum;
        double bBandTot = bLow + bMid + bHigh + 1e-15;
        blockBands[b] = { bLow / bBandTot, bMid / bBandTot, bHigh / bBandTot };
    }

    for (size_t b = startBlock + 1; b < totalBlocks; ++b) {
        if (blockEnergy[b-1] > 1e-12 && blockEnergy[b] > 1e-12) {
            double ratio = blockEnergy[b] / blockEnergy[b-1];
            double growthDb = 10.0 * std::log10(ratio);
            if (growthDb > 0.5) {
                m.unexpectedGrowths++;
                if (growthDb > m.maxGrowthDb) {
                    m.maxGrowthDb = growthDb;
                    m.maxGrowthTimeS = static_cast<double>(b * blockSize) / sampleRate;
                }
            }
        }
    }

    // Spectral drift across 500 ms windows
    double sumDrift = 0.0;
    int driftCount = 0;
    for (size_t b = startBlock + 1; b < totalBlocks; ++b) {
        if (blockEnergy[b] > 1e-14 && blockEnergy[b-1] > 1e-14) {
            double d0 = blockBands[b][0] - blockBands[b-1][0];
            double d1 = blockBands[b][1] - blockBands[b-1][1];
            double d2 = blockBands[b][2] - blockBands[b-1][2];
            double drift = std::sqrt(d0 * d0 + d1 * d1 + d2 * d2);
            sumDrift += drift;
            driftCount++;
            m.maxSpectralDrift = std::max(m.maxSpectralDrift, drift);
        }
    }
    m.meanSpectralDrift = driftCount > 0 ? (sumDrift / driftCount) : 0.0;

    // 2. Click / Burst detection
    const size_t clickWindow = static_cast<size_t>(0.050f * sampleRate); // 50ms local window
    for (size_t i = static_cast<size_t>(0.100f * sampleRate); i < count - 1; ++i) {
        float dl = std::abs(l[i+1] - l[i]);
        float dr = std::abs(r[i+1] - r[i]);
        float delta = std::max(dl, dr);

        size_t wStart = (i >= clickWindow / 2) ? (i - clickWindow / 2) : 0;
        size_t wEnd = std::min(count, i + clickWindow / 2);
        double localE = cumEnergy[wEnd] - cumEnergy[wStart];
        double localRms = std::sqrt(localE / (2.0 * (wEnd - wStart) + 1e-12));

        if (localRms > 1e-6 && delta > 15.0f * localRms) {
            m.clickCount++;
            m.maxClickDelta = std::max(m.maxClickDelta, static_cast<double>(delta));
        }
    }

    // 3. RT60 via Schroeder reverse integration
    std::vector<double> decay(count, 0.0);
    double cumulative = 0.0;
    for (size_t i = count; i-- > 0;) {
        cumulative += energy[i];
        decay[i] = cumulative;
    }
    if (cumulative > 0.0) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        size_t n = 0;
        for (size_t i = 0; i < count; ++i) {
            double db = 10.0 * std::log10(std::max(1.0e-30, decay[i] / decay[0]));
            if (db <= -5.0 && db >= -25.0) {
                double t = static_cast<double>(i) / sampleRate;
                sx += t; sy += db; sxx += t * t; sxy += t * db; ++n;
            }
        }
        double d = static_cast<double>(n) * sxx - sx * sx;
        if (n > 100 && std::abs(d) > 1.0e-12) {
            double slope = (static_cast<double>(n) * sxy - sx * sy) / d;
            if (slope < 0.0) m.rt60Seconds = -60.0 / slope;
        }
    }

    return m;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "      NIMBUS LONG TAIL FORENSIC AUDIT (20s: Mod & Nonlin)        \n";
    std::cout << "=================================================================\n\n";

    std::vector<CloudGreyVerb::Preset> presets = {
        CloudGreyVerb::Preset::BrightCloud,
        CloudGreyVerb::Preset::DarkLongCloud,
        CloudGreyVerb::Preset::GreyholeDelayVerb
    };

    std::vector<LongTailMetrics> results;

    // 1. Diagnostic of the 4 conditions on current architecture (A)
    for (auto preset : presets) {
        for (bool mod : {true, false}) {
            for (bool nonlin : {true, false}) {
                auto m = analyzeLongTail(preset, mod, nonlin, CloudGreyVerb::FeedbackArchitecture::CurrentTapeClip, 48000.0f, 20.0f);
                results.push_back(m);
            }
        }
    }

    std::cout << "### TABELA 1: CARACTERÍSTICAS MACRO DA CAUDA (20 SEGUNDOS)\n\n";
    std::cout << "| Preset | Condição | Peak | RMS Overall | Crest Factor | Clicks | Growth Warns | Max Grw (dB) | RT60 (s) |\n";
    std::cout << "|:-------|:---------|:-----|:------------|:-------------|:-------|:-------------|:-------------|:---------|\n";
    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(20) << r.condition
                  << " | " << std::fixed << std::setprecision(4) << r.peak
                  << " | " << std::scientific << std::setprecision(2) << r.rmsOverall
                  << " | " << std::fixed << std::setprecision(1) << r.crestFactor
                  << " | " << std::setw(6) << r.clickCount
                  << " | " << std::setw(12) << r.unexpectedGrowths
                  << " | " << std::setw(10) << std::fixed << std::setprecision(2) << (r.unexpectedGrowths > 0 ? r.maxGrowthDb : 0.0)
                  << " | " << std::fixed << std::setprecision(2) << r.rt60Seconds << " s |\n";
    }
    std::cout << "\n";

    std::cout << "### TABELA 2: EVOLUÇÃO RMS POR JANELAS DE TEMPO\n\n";
    std::cout << "| Preset | Condição | RMS 0-1s | RMS 1-4s | RMS 4-8s | RMS 8-12s | RMS 12-16s | RMS 16-20s |\n";
    std::cout << "|:-------|:---------|:---------|:---------|:---------|:----------|:-----------|:-----------|\n";
    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(20) << r.condition
                  << " | " << std::scientific << std::setprecision(2) << r.rms_0_1s
                  << " | " << r.rms_1_4s
                  << " | " << r.rms_4_8s
                  << " | " << r.rms_8_12s
                  << " | " << r.rms_12_16s
                  << " | " << r.rms_16_20s << " |\n";
    }
    std::cout << "\n";

    std::cout << "### TABELA 3: ENERGIA POR BANDAS (Low: 20-250Hz | Mid: 250-3kHz | High: 3-15kHz)\n\n";
    std::cout << "| Preset | Condição | Low Tot | Mid Tot | High Tot | Low 1-4s | High 1-4s | Low 8-12s | High 8-12s | Low 16-20s | High 16-20s |\n";
    std::cout << "|:-------|:---------|:--------|:--------|:---------|:---------|:----------|:----------|:-----------|:-----------|:------------|\n";
    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(20) << r.condition
                  << " | " << std::fixed << std::setprecision(1) << r.lowPctOverall << "%"
                  << " | " << r.midPctOverall << "%"
                  << " | " << r.highPctOverall << "%"
                  << " | " << r.lowPct_1_4s << "%"
                  << " | " << r.highPct_1_4s << "%"
                  << " | " << r.lowPct_8_12s << "%"
                  << " | " << r.highPct_8_12s << "%"
                  << " | " << r.lowPct_16_20s << "%"
                  << " | " << r.highPct_16_20s << "% |\n";
    }
    std::cout << "\n";

    std::cout << "### TABELA 4: CENTROIDE ESPECTRAL (Hz) E SPECTRAL DRIFT (500ms)\n\n";
    std::cout << "| Preset | Condição | Cent 0.5s | Cent 2s | Cent 6s | Cent 10s | Cent 14s | Cent 18s | Mean Drift | Max Drift |\n";
    std::cout << "|:-------|:---------|:----------|:--------|:--------|:---------|:---------|:---------|:-----------|:----------|\n";
    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(20) << r.condition
                  << " | " << std::fixed << std::setprecision(0) << r.centroid_0_5s << " Hz"
                  << " | " << r.centroid_2s << " Hz"
                  << " | " << r.centroid_6s << " Hz"
                  << " | " << r.centroid_10s << " Hz"
                  << " | " << r.centroid_14s << " Hz"
                  << " | " << r.centroid_18s << " Hz"
                  << " | " << std::setprecision(4) << r.meanSpectralDrift
                  << " | " << std::setprecision(4) << r.maxSpectralDrift << " |\n";
    }
    std::cout << "\n";

    std::cout << "=================================================================\n";
    std::cout << "          NIMBUS FREEZE FORENSIC AUDIT (20s Sustain)             \n";
    std::cout << "=================================================================\n\n";

    std::vector<LongTailMetrics> freezeResults;
    for (auto preset : presets) {
        for (bool mod : {true, false}) {
            for (bool nonlin : {true, false}) {
                auto m = analyzeLongTail(preset, mod, nonlin, CloudGreyVerb::FeedbackArchitecture::CurrentTapeClip, 48000.0f, 20.0f, true);
                freezeResults.push_back(m);
            }
        }
    }

    std::cout << "### TABELA 5: COMPORTAMENTO EM FREEZE (SUSTAIN DE 20s)\n\n";
    std::cout << "| Preset | Condição | Peak | RMS Overall | RMS 1-4s | RMS 16-20s | Low % (16-20s) | High % (16-20s) | Cent 18s | Mean Drift | Growth Warns |\n";
    std::cout << "|:-------|:---------|:-----|:------------|:---------|:-----------|:---------------|:----------------|:---------|:-----------|:-------------|\n";
    for (const auto& r : freezeResults) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(25) << r.condition
                  << " | " << std::fixed << std::setprecision(4) << r.peak
                  << " | " << std::scientific << std::setprecision(2) << r.rmsOverall
                  << " | " << r.rms_1_4s
                  << " | " << r.rms_16_20s
                  << " | " << std::fixed << std::setprecision(1) << r.lowPct_16_20s << "%"
                  << " | " << r.highPct_16_20s << "%"
                  << " | " << std::fixed << std::setprecision(0) << r.centroid_18s << " Hz"
                  << " | " << std::setprecision(4) << r.meanSpectralDrift
                  << " | " << std::setw(12) << r.unexpectedGrowths << " |\n";
    }
    std::cout << "\n";


    std::cout << "=================================================================\n";
    std::cout << "    COMPARAÇÃO DAS TRÊS ARQUITETURAS DE FEEDBACK (A, B, C)      \n";
    std::cout << "=================================================================\n\n";
    std::cout << "A: Current [feedback -> tapeClip -> loop]\n";
    std::cout << "B: Spectral Guard only [feedback -> spectral guard -> loop]\n";
    std::cout << "C: Weak Saturation + Spectral Guard [feedback -> gentle sat -> spectral guard -> loop]\n\n";

    std::vector<CloudGreyVerb::FeedbackArchitecture> archs = {
        CloudGreyVerb::FeedbackArchitecture::CurrentTapeClip,
        CloudGreyVerb::FeedbackArchitecture::SpectralGuardOnly,
        CloudGreyVerb::FeedbackArchitecture::WeakSaturationSpectralGuard
    };

    std::vector<LongTailMetrics> archResults;
    for (auto preset : presets) {
        for (auto arch : archs) {
            auto m = analyzeLongTail(preset, true, true, arch, 48000.0f, 20.0f, false);
            archResults.push_back(m);
        }
    }

    std::cout << "### TABELA 6: COMPARAÇÃO A/B/C - MÉTRICAS MACRO DA CAUDA (20s Impulse)\n\n";
    std::cout << "| Preset | Arquitetura | Peak | RMS Overall | Crest Factor | Clicks | Growth Warns | Max Grw (dB) | Mean Drift | RT60 (s) |\n";
    std::cout << "|:-------|:------------|:-----|:------------|:-------------|:-------|:-------------|:-------------|:-----------|:---------|\n";
    for (const auto& r : archResults) {
        std::string archName = "";
        if (r.condition.find("Arch A") != std::string::npos) archName = "A: Current TapeClip";
        else if (r.condition.find("Arch B") != std::string::npos) archName = "B: SpectralGuard Only";
        else if (r.condition.find("Arch C") != std::string::npos) archName = "C: WeakSat + SpecGuard";

        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(23) << archName
                  << " | " << std::fixed << std::setprecision(4) << r.peak
                  << " | " << std::scientific << std::setprecision(2) << r.rmsOverall
                  << " | " << std::fixed << std::setprecision(1) << r.crestFactor
                  << " | " << std::setw(6) << r.clickCount
                  << " | " << std::setw(12) << r.unexpectedGrowths
                  << " | " << std::setw(10) << std::fixed << std::setprecision(2) << (r.unexpectedGrowths > 0 ? r.maxGrowthDb : 0.0)
                  << " | " << std::setprecision(4) << r.meanSpectralDrift
                  << " | " << std::fixed << std::setprecision(2) << r.rt60Seconds << " s |\n";
    }
    std::cout << "### TABELA 6b: ATIVIDADE DO SPECTRAL GUARD - USO NORMAL (20s Impulse)\n\n";
    std::cout << "| Preset | Arquitetura | Min Gain (L / M / H) | Max Energy (L / M / H) | Samples < 0.99 (L / M / H) | Samples < 0.95 (L / M / H) |\n";
    std::cout << "|:-------|:------------|:---------------------|:-----------------------|:---------------------------|:---------------------------|\n";
    for (const auto& r : archResults) {
        std::string archName = "";
        if (r.condition.find("Arch A") != std::string::npos) archName = "A: Current TapeClip";
        else if (r.condition.find("Arch B") != std::string::npos) archName = "B: SpectralGuard Only";
        else if (r.condition.find("Arch C") != std::string::npos) archName = "C: WeakSat + SpecGuard";

        char minGainBuf[64];
        std::snprintf(minGainBuf, sizeof(minGainBuf), "%.4f / %.4f / %.4f", r.minGainLow, r.minGainMid, r.minGainHigh);
        char maxEnergyBuf[64];
        std::snprintf(maxEnergyBuf, sizeof(maxEnergyBuf), "%.5f / %.5f / %.5f", r.maxEnergyLow, r.maxEnergyMid, r.maxEnergyHigh);
        char s99Buf[64];
        std::snprintf(s99Buf, sizeof(s99Buf), "%.2f%% / %.2f%% / %.2f%%", r.pctBelow99Low, r.pctBelow99Mid, r.pctBelow99High);
        char s95Buf[64];
        std::snprintf(s95Buf, sizeof(s95Buf), "%.2f%% / %.2f%% / %.2f%%", r.pctBelow95Low, r.pctBelow95Mid, r.pctBelow95High);

        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(23) << archName
                  << " | " << std::setw(20) << minGainBuf
                  << " | " << std::setw(22) << maxEnergyBuf
                  << " | " << std::setw(26) << s99Buf
                  << " | " << std::setw(26) << s95Buf << " |\n";
    }
    std::cout << "\n";

    std::cout << "### TABELA 7: COMPARAÇÃO A/B/C - EVOLUÇÃO ESPECTRAL (Centroide Hz)\n\n";
    std::cout << "| Preset | Arquitetura | Cent 0.5s | Cent 2s | Cent 6s | Cent 10s | Cent 14s | Cent 18s |\n";
    std::cout << "|:-------|:------------|:----------|:--------|:--------|:---------|:---------|:---------|\n";
    for (const auto& r : archResults) {
        std::string archName = "";
        if (r.condition.find("Arch A") != std::string::npos) archName = "A: Current TapeClip";
        else if (r.condition.find("Arch B") != std::string::npos) archName = "B: SpectralGuard Only";
        else if (r.condition.find("Arch C") != std::string::npos) archName = "C: WeakSat + SpecGuard";

        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(23) << archName
                  << " | " << std::fixed << std::setprecision(0) << r.centroid_0_5s << " Hz"
                  << " | " << r.centroid_2s << " Hz"
                  << " | " << r.centroid_6s << " Hz"
                  << " | " << r.centroid_10s << " Hz"
                  << " | " << r.centroid_14s << " Hz"
                  << " | " << r.centroid_18s << " Hz |\n";
    }
    std::cout << "\n";

    // Freeze comparison across architectures A, B, C
    std::cout << "=================================================================\n";
    std::cout << "    COMPARAÇÃO A/B/C EM FREEZE / NEAR-INFINITE SUSTAIN (20s)     \n";
    std::cout << "=================================================================\n\n";

    std::vector<LongTailMetrics> archFreezeResults;
    for (auto preset : presets) {
        for (auto arch : archs) {
            auto m = analyzeLongTail(preset, true, true, arch, 48000.0f, 20.0f, true);
            archFreezeResults.push_back(m);
        }
    }

    std::cout << "### TABELA 8: COMPARAÇÃO A/B/C EM FREEZE (SUSTAIN DE 20s)\n\n";
    std::cout << "| Preset | Arquitetura | Peak | RMS Overall | RMS 1-4s | RMS 16-20s | Low % (16-20s) | High % (16-20s) | Cent 18s | Mean Drift | Growth Warns |\n";
    std::cout << "|:-------|:------------|:-----|:------------|:---------|:-----------|:---------------|:----------------|:---------|:-----------|:-------------|\n";
    for (const auto& r : archFreezeResults) {
        std::string archName = "";
        if (r.condition.find("Arch A") != std::string::npos) archName = "A: Current TapeClip";
        else if (r.condition.find("Arch B") != std::string::npos) archName = "B: SpectralGuard Only";
        else if (r.condition.find("Arch C") != std::string::npos) archName = "C: WeakSat + SpecGuard";

        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(23) << archName
                  << " | " << std::fixed << std::setprecision(4) << r.peak
                  << " | " << std::scientific << std::setprecision(2) << r.rmsOverall
                  << " | " << r.rms_1_4s
                  << " | " << r.rms_16_20s
                  << " | " << std::fixed << std::setprecision(1) << r.lowPct_16_20s << "%"
                  << " | " << r.highPct_16_20s << "%"
                  << " | " << std::fixed << std::setprecision(0) << r.centroid_18s << " Hz"
                  << " | " << std::setprecision(4) << r.meanSpectralDrift
                  << " | " << std::setw(12) << r.unexpectedGrowths << " |\n";
    }
    std::cout << "\n";

    std::cout << "### TABELA 8b: ATIVIDADE DO SPECTRAL GUARD - FREEZE SUSTAIN (20s)\n\n";
    std::cout << "| Preset | Arquitetura | Min Gain (L / M / H) | Max Energy (L / M / H) | Samples < 0.99 (L / M / H) | Samples < 0.95 (L / M / H) |\n";
    std::cout << "|:-------|:------------|:---------------------|:-----------------------|:---------------------------|:---------------------------|\n";
    for (const auto& r : archFreezeResults) {
        std::string archName = "";
        if (r.condition.find("Arch A") != std::string::npos) archName = "A: Current TapeClip";
        else if (r.condition.find("Arch B") != std::string::npos) archName = "B: SpectralGuard Only";
        else if (r.condition.find("Arch C") != std::string::npos) archName = "C: WeakSat + SpecGuard";

        char minGainBuf[64];
        std::snprintf(minGainBuf, sizeof(minGainBuf), "%.4f / %.4f / %.4f", r.minGainLow, r.minGainMid, r.minGainHigh);
        char maxEnergyBuf[64];
        std::snprintf(maxEnergyBuf, sizeof(maxEnergyBuf), "%.5f / %.5f / %.5f", r.maxEnergyLow, r.maxEnergyMid, r.maxEnergyHigh);
        char s99Buf[64];
        std::snprintf(s99Buf, sizeof(s99Buf), "%.2f%% / %.2f%% / %.2f%%", r.pctBelow99Low, r.pctBelow99Mid, r.pctBelow99High);
        char s95Buf[64];
        std::snprintf(s95Buf, sizeof(s95Buf), "%.2f%% / %.2f%% / %.2f%%", r.pctBelow95Low, r.pctBelow95Mid, r.pctBelow95High);

        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(23) << archName
                  << " | " << std::setw(20) << minGainBuf
                  << " | " << std::setw(22) << maxEnergyBuf
                  << " | " << std::setw(26) << s99Buf
                  << " | " << std::setw(26) << s95Buf << " |\n";
    }
    std::cout << "\n";

    return 0;
}
