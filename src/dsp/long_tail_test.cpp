#include "cloud_grey_verb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct LongTailMetrics {
    std::string presetName;
    std::string condition;
    bool modulation = true;
    bool nonlinearities = true;

    double peak = 0.0;
    double rmsOverall = 0.0;
    double crestFactor = 0.0;

    double rms_0_1s = 0.0;
    double rms_1_4s = 0.0;
    double rms_4_8s = 0.0;
    double rms_8_12s = 0.0;
    double rms_12_16s = 0.0;

    int unexpectedGrowths = 0;
    double maxGrowthDb = 0.0;
    double maxGrowthTimeS = 0.0;

    int clickCount = 0;
    double maxClickDelta = 0.0;

    double rt60Seconds = 0.0;
    bool stable = true;
};

LongTailMetrics analyzeLongTail(CloudGreyVerb::Preset preset, bool mod, bool nonlin, float sampleRate = 48000.0f, float durationSeconds = 16.0f) {
    LongTailMetrics m;
    const auto fp = CloudGreyVerb::getFactoryPreset(preset);
    m.presetName = fp.name;
    m.modulation = mod;
    m.nonlinearities = nonlin;

    std::string condStr = "";
    condStr += (mod ? "Mod:ON " : "Mod:OFF ");
    condStr += (nonlin ? "Nonlin:ON" : "Nonlin:OFF");
    m.condition = condStr;

    auto p = fp.dsp;
    p.mix = 1.0f;
    p.clipOutput = false;

    std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(sampleRate), 0.0f);
    CloudGreyVerb verb;
    verb.init(sampleRate, memory.data(), memory.size());
    verb.setParams(p);
    CloudGreyVerbComponentTestAccess::setModulationEnabled(verb, mod);
    CloudGreyVerbComponentTestAccess::setNonlinearitiesEnabled(verb, nonlin);
    verb.reset();

    const size_t count = static_cast<size_t>(durationSeconds * sampleRate);
    std::vector<float> l(count, 0.0f);
    std::vector<float> r(count, 0.0f);
    std::vector<double> energy(count, 0.0);

    double sumEnergy = 0.0;
    for (size_t i = 0; i < count; ++i) {
        float in = (i == 0) ? 0.70710678f : 0.0f; // Unit impulse
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
    }

    m.rmsOverall = std::sqrt(sumEnergy / (2.0 * count));
    m.crestFactor = (m.rmsOverall > 1e-12) ? (m.peak / m.rmsOverall) : 0.0;

    auto calcWindowRms = [&](size_t startSample, size_t endSample) {
        endSample = std::min(endSample, count);
        if (startSample >= endSample) return 0.0;
        double winSum = 0.0;
        for (size_t i = startSample; i < endSample; ++i) winSum += energy[i];
        return std::sqrt(winSum / (2.0 * (endSample - startSample)));
    };

    m.rms_0_1s   = calcWindowRms(0, static_cast<size_t>(1.0f * sampleRate));
    m.rms_1_4s   = calcWindowRms(static_cast<size_t>(1.0f * sampleRate), static_cast<size_t>(4.0f * sampleRate));
    m.rms_4_8s   = calcWindowRms(static_cast<size_t>(4.0f * sampleRate), static_cast<size_t>(8.0f * sampleRate));
    m.rms_8_12s  = calcWindowRms(static_cast<size_t>(8.0f * sampleRate), static_cast<size_t>(12.0f * sampleRate));
    m.rms_12_16s = calcWindowRms(static_cast<size_t>(12.0f * sampleRate), static_cast<size_t>(16.0f * sampleRate));

    // 1. Detect unexpected energy growth in 500ms blocks after 1.5s
    const size_t blockSize = static_cast<size_t>(0.500f * sampleRate);
    const size_t startBlock = static_cast<size_t>(1.500f * sampleRate) / blockSize;
    const size_t totalBlocks = count / blockSize;

    std::vector<double> blockEnergy(totalBlocks, 0.0);
    for (size_t b = 0; b < totalBlocks; ++b) {
        double bSum = 0.0;
        size_t bStart = b * blockSize;
        for (size_t i = 0; i < blockSize && (bStart + i) < count; ++i) {
            bSum += energy[bStart + i];
        }
        blockEnergy[b] = bSum;
    }

    for (size_t b = startBlock + 1; b < totalBlocks; ++b) {
        if (blockEnergy[b-1] > 1e-12 && blockEnergy[b] > 1e-12) {
            double ratio = blockEnergy[b] / blockEnergy[b-1];
            double growthDb = 10.0 * std::log10(ratio);
            // In a decaying tail, a growth > 0.5 dB in 500ms indicates unexpected swelling/burst
            if (growthDb > 0.5) {
                m.unexpectedGrowths++;
                if (growthDb > m.maxGrowthDb) {
                    m.maxGrowthDb = growthDb;
                    m.maxGrowthTimeS = static_cast<double>(b * blockSize) / sampleRate;
                }
            }
        }
    }

    // 2. Click / Burst detection: sample-to-sample difference vs local RMS
    const size_t clickWindow = static_cast<size_t>(0.050f * sampleRate); // 50ms local window
    for (size_t i = static_cast<size_t>(0.100f * sampleRate); i < count - 1; ++i) {
        float dl = std::abs(l[i+1] - l[i]);
        float dr = std::abs(r[i+1] - r[i]);
        float delta = std::max(dl, dr);

        // Approximate local RMS around sample i
        size_t wStart = (i >= clickWindow / 2) ? (i - clickWindow / 2) : 0;
        size_t wEnd = std::min(count, i + clickWindow / 2);
        double localE = 0.0;
        for (size_t k = wStart; k < wEnd; ++k) localE += energy[k];
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
    std::cout << "        NIMBUS LONG TAIL AUDIT (16s Renders: Mod & Nonlin)       \n";
    std::cout << "=================================================================\n\n";

    std::vector<CloudGreyVerb::Preset> presets = {
        CloudGreyVerb::Preset::BrightCloud,
        CloudGreyVerb::Preset::DarkLongCloud,
        CloudGreyVerb::Preset::GreyholeDelayVerb
    };

    std::vector<LongTailMetrics> results;

    for (auto preset : presets) {
        for (bool mod : {true, false}) {
            for (bool nonlin : {true, false}) {
                auto m = analyzeLongTail(preset, mod, nonlin, 48000.0f, 16.0f);
                results.push_back(m);
            }
        }
    }

    std::cout << "### TABELA DE AVALIAÇÃO DE LONG TAIL (16 SEGUNDOS)\n\n";
    std::cout << "| Preset | Condição | Peak | RMS Overall | Crest Factor | RMS 1-4s | RMS 4-8s | RMS 8-12s | RMS 12-16s | Clicks | Growth Warns | RT60 (s) |\n";
    std::cout << "|:-------|:---------|:-----|:------------|:-------------|:---------|:---------|:----------|:-----------|:-------|:-------------|:---------|\n";

    for (const auto& r : results) {
        std::cout << "| " << std::left << std::setw(18) << r.presetName
                  << " | " << std::setw(20) << r.condition
                  << " | " << std::fixed << std::setprecision(4) << r.peak
                  << " | " << std::scientific << std::setprecision(2) << r.rmsOverall
                  << " | " << std::fixed << std::setprecision(1) << r.crestFactor
                  << " | " << std::scientific << std::setprecision(2) << r.rms_1_4s
                  << " | " << r.rms_4_8s
                  << " | " << r.rms_8_12s
                  << " | " << r.rms_12_16s
                  << " | " << std::setw(6) << r.clickCount
                  << " | " << std::setw(12) << r.unexpectedGrowths
                  << " | " << std::fixed << std::setprecision(2) << r.rt60Seconds << " s |\n";
    }
    std::cout << "\n";

    return 0;
}
