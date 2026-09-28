#include "cloud_grey_verb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;

// Minimal 16-bit PCM WAV writer
void writeWav16(const std::string& filepath, const std::vector<float>& left, const std::vector<float>& right, float sampleRate) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return;

    uint32_t numSamples = static_cast<uint32_t>(left.size());
    uint16_t numChannels = 2;
    uint32_t sr = static_cast<uint32_t>(sampleRate);
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sr * numChannels * (bitsPerSample / 8);
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t subchunk2Size = numSamples * numChannels * (bitsPerSample / 8);
    uint32_t chunkSize = 36 + subchunk2Size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunkSize), 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1; // PCM
    file.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
    file.write(reinterpret_cast<const char*>(&audioFormat), 2);
    file.write(reinterpret_cast<const char*>(&numChannels), 2);
    file.write(reinterpret_cast<const char*>(&sr), 4);
    file.write(reinterpret_cast<const char*>(&byteRate), 4);
    file.write(reinterpret_cast<const char*>(&blockAlign), 2);
    file.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subchunk2Size), 4);

    for (size_t i = 0; i < numSamples; ++i) {
        float sl = std::max(-1.0f, std::min(1.0f, left[i]));
        float sr_val = std::max(-1.0f, std::min(1.0f, right[i]));
        int16_t sampleL = static_cast<int16_t>(sl * 32767.0f);
        int16_t sampleR = static_cast<int16_t>(sr_val * 32767.0f);
        file.write(reinterpret_cast<const char*>(&sampleL), 2);
        file.write(reinterpret_cast<const char*>(&sampleR), 2);
    }
}

struct Signal {
    std::string name;
    std::vector<float> samples;
};

// Generate the 4 required test signals:
// 1. Impulse
// 2. Synthetic harp / pluck with short attack
// 3. Snare / short percussion
// 4. Piano / tonal pluck
std::vector<Signal> generateSignals(float sampleRate, float durationSeconds) {
    const size_t totalFrames = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<Signal> signals;

    // 1. Impulse
    {
        Signal sig;
        sig.name = "Impulse";
        sig.samples.assign(totalFrames, 0.0f);
        sig.samples[0] = 1.0f;
        signals.push_back(sig);
    }

    // 2. Synthetic Harp / Pluck (short attack ~0.8ms, decaying harmonics)
    {
        Signal sig;
        sig.name = "HarpPluck";
        sig.samples.assign(totalFrames, 0.0f);
        const float f0 = 330.0f; // E4
        for (size_t i = 0; i < totalFrames; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            if (t > 0.600f) break;
            // Attack in ~0.8 ms, fast decay
            float env = (1.0f - std::exp(-t / 0.0008f)) * std::exp(-t / 0.160f);
            float sum = 0.0f;
            for (int k = 1; k <= 8; ++k) {
                float freq = f0 * static_cast<float>(k);
                float tau = 0.160f / std::pow(static_cast<float>(k), 0.7f);
                float harmAmp = 1.0f / std::pow(static_cast<float>(k), 1.2f);
                sum += harmAmp * std::sin(2.0f * static_cast<float>(kPi) * freq * t) * std::exp(-t / tau);
            }
            sig.samples[i] = env * sum;
        }
        // Normalize peak to 0.95
        float maxVal = 0.0f;
        for (float s : sig.samples) maxVal = std::max(maxVal, std::abs(s));
        if (maxVal > 0.0f) {
            for (float& s : sig.samples) s = s * (0.95f / maxVal);
        }
        signals.push_back(sig);
    }

    // 3. Snare / Short Percussion (sharp transient, low pitch sweep + noise)
    {
        Signal sig;
        sig.name = "SnarePerc";
        sig.samples.assign(totalFrames, 0.0f);
        cgv_dsp::FastPRNG prng;
        prng.seed(98765);
        float noiseState = 0.0f;
        for (size_t i = 0; i < totalFrames; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            if (t > 0.200f) break;
            // Body: 180 Hz sweeping to 80 Hz with 25 ms decay
            float bodyEnv = (1.0f - std::exp(-t / 0.0003f)) * std::exp(-t / 0.035f);
            float bodyFreq = 80.0f + 100.0f * std::exp(-t / 0.020f);
            float bodyPhase = 2.0f * static_cast<float>(kPi) * (80.0f * t - 100.0f * 0.020f * (std::exp(-t / 0.020f) - 1.0f));
            float body = std::sin(bodyPhase) * bodyEnv;

            // Wires: noise burst with 45 ms decay
            float rawNoise = prng.randFloat() * 2.0f - 1.0f;
            noiseState += 0.4f * (rawNoise - noiseState); // Simple lowpass filter
            float noiseEnv = (1.0f - std::exp(-t / 0.0002f)) * std::exp(-t / 0.045f);
            float noise = (rawNoise - noiseState) * noiseEnv; // Highpassed noise

            sig.samples[i] = body * 0.5f + noise * 0.5f;
        }
        float maxVal = 0.0f;
        for (float s : sig.samples) maxVal = std::max(maxVal, std::abs(s));
        if (maxVal > 0.0f) {
            for (float& s : sig.samples) s = s * (0.95f / maxVal);
        }
        signals.push_back(sig);
    }

    // 4. Piano / Tonal Pluck (220 Hz A3, 2 ms attack, 450 ms decay)
    {
        Signal sig;
        sig.name = "PianoPluck";
        sig.samples.assign(totalFrames, 0.0f);
        const float f0 = 220.0f;
        for (size_t i = 0; i < totalFrames; ++i) {
            float t = static_cast<float>(i) / sampleRate;
            if (t > 0.800f) break;
            float env = (1.0f - std::exp(-t / 0.002f)) * std::exp(-t / 0.400f);
            float sum = 0.0f;
            for (int k = 1; k <= 7; ++k) {
                float freq = f0 * static_cast<float>(k);
                float tau = 0.400f / std::pow(static_cast<float>(k), 0.6f);
                float harmAmp = 1.0f / static_cast<float>(k);
                sum += harmAmp * std::sin(2.0f * static_cast<float>(kPi) * freq * t) * std::exp(-t / tau);
            }
            sig.samples[i] = env * sum;
        }
        float maxVal = 0.0f;
        for (float s : sig.samples) maxVal = std::max(maxVal, std::abs(s));
        if (maxVal > 0.0f) {
            for (float& s : sig.samples) s = s * (0.95f / maxVal);
        }
        signals.push_back(sig);
    }

    return signals;
}

struct AuditMetrics {
    std::string presetName;
    std::string signalName;
    float preDelayMs = 0.0f;

    double firstWetArrivalMs = 0.0;
    double firstEarlyArrivalMs = 0.0;
    double firstGranularArrivalMs = 0.0;
    double firstFdnArrivalMs = 0.0;

    double peakEnergy250ms = 0.0;
    double peakTimeMs = 0.0;
    double t10ms = 0.0;
    double t50ms = 0.0;
    double t90ms = 0.0;

    // Windowed energy in wet output
    double energy_0_10 = 0.0;
    double energy_10_25 = 0.0;
    double energy_25_50 = 0.0;
    double energy_50_100 = 0.0;
    double energy_100_250 = 0.0;
    double energy_0_250_total = 0.0;

    // Component energy in 0-250ms
    double earlyEnergy250 = 0.0;
    double granularEnergy250 = 0.0;
    double diffuserEnergy250 = 0.0;
    double fdnEnergy250 = 0.0;

    // Damping metrics
    float minDynamicLpFreq = 0.0f;
    float maxDynamicLpFreq = 0.0f;
};

AuditMetrics runAudit(CloudGreyVerb::Preset preset, const Signal& sig, float sampleRate,
                     int dynamicDampingMode = 0,
                     const std::string& wavStemPrefix = "") {
    AuditMetrics m;
    const auto fp = CloudGreyVerb::getFactoryPreset(preset);
    m.presetName = fp.name;
    m.signalName = sig.name;
    auto params = fp.dsp;
    params.mix = 1.0f; // 100% wet for pure wet audit
    params.clipOutput = false;

    const float manualPreDelayMs = params.preDelay * CloudGreyVerb::kManualPreDelayMaximumSeconds * 1000.0f;
    m.preDelayMs = manualPreDelayMs;

    std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(sampleRate), 0.0f);
    CloudGreyVerb verb;
    verb.init(sampleRate, memory.data(), memory.size());
    verb.setParams(params);
    CloudGreyVerbComponentTestAccess::setDynamicDampingMode(verb, dynamicDampingMode);
    verb.reset();

    const size_t count = sig.samples.size();
    std::vector<float> outL(count, 0.0f);
    std::vector<float> outR(count, 0.0f);
    std::vector<float> earlyL(count, 0.0f), earlyR(count, 0.0f);
    std::vector<float> granL(count, 0.0f), granR(count, 0.0f);
    std::vector<float> diffL(count, 0.0f), diffR(count, 0.0f);
    std::vector<float> tailL(count, 0.0f), tailR(count, 0.0f);
    std::vector<double> wetEnergy(count, 0.0);

    CloudGreyVerbComponentTestAccess::Components probe;
    m.minDynamicLpFreq = 999999.0f;
    m.maxDynamicLpFreq = 0.0f;

    for (size_t i = 0; i < count; ++i) {
        float in = sig.samples[i];
        float l = 0.0f, r = 0.0f;
        CloudGreyVerbComponentTestAccess::processSampleProbe(verb, in, in, l, r, &probe);
        outL[i] = l;
        outR[i] = r;
        earlyL[i] = probe.earlyL;
        earlyR[i] = probe.earlyR;
        granL[i] = probe.granL;
        granR[i] = probe.granR;
        diffL[i] = probe.diffL;
        diffR[i] = probe.diffR;
        tailL[i] = probe.tailL;
        tailR[i] = probe.tailR;
        wetEnergy[i] = static_cast<double>(l) * l + static_cast<double>(r) * r;

        m.minDynamicLpFreq = std::min(m.minDynamicLpFreq, probe.dynamicLpFreq);
        m.maxDynamicLpFreq = std::max(m.maxDynamicLpFreq, probe.dynamicLpFreq);
    }

    if (!wavStemPrefix.empty()) {
        writeWav16(wavStemPrefix + "_wet.wav", outL, outR, sampleRate);
        writeWav16(wavStemPrefix + "_early.wav", earlyL, earlyR, sampleRate);
        writeWav16(wavStemPrefix + "_granular.wav", granL, granR, sampleRate);
        writeWav16(wavStemPrefix + "_diffuser.wav", diffL, diffR, sampleRate);
        writeWav16(wavStemPrefix + "_fdn.wav", tailL, tailR, sampleRate);
    }

    // 1. Detect arrivals (threshold: 1e-6 of energy or -60dB)
    const double arrivalThreshold = 1.0e-6;
    m.firstWetArrivalMs = -1.0;
    m.firstEarlyArrivalMs = -1.0;
    m.firstGranularArrivalMs = -1.0;
    m.firstFdnArrivalMs = -1.0;

    for (size_t i = 0; i < count; ++i) {
        double tMs = static_cast<double>(i) * 1000.0 / sampleRate;
        if (m.firstWetArrivalMs < 0.0 && wetEnergy[i] > arrivalThreshold) {
            m.firstWetArrivalMs = tMs;
        }
        double eEarly = static_cast<double>(earlyL[i])*earlyL[i] + static_cast<double>(earlyR[i])*earlyR[i];
        if (m.firstEarlyArrivalMs < 0.0 && eEarly > arrivalThreshold) {
            m.firstEarlyArrivalMs = tMs;
        }
        double eGran = static_cast<double>(granL[i])*granL[i] + static_cast<double>(granR[i])*granR[i];
        if (m.firstGranularArrivalMs < 0.0 && eGran > arrivalThreshold) {
            m.firstGranularArrivalMs = tMs;
        }
        double eFdn = static_cast<double>(tailL[i])*tailL[i] + static_cast<double>(tailR[i])*tailR[i];
        if (m.firstFdnArrivalMs < 0.0 && eFdn > arrivalThreshold) {
            m.firstFdnArrivalMs = tMs;
        }
    }

    // 2. Windowed energy and peak in first 250 ms
    const size_t n250ms = static_cast<size_t>(0.250f * sampleRate);
    const size_t n10ms = static_cast<size_t>(0.010f * sampleRate);
    const size_t n25ms = static_cast<size_t>(0.025f * sampleRate);
    const size_t n50ms = static_cast<size_t>(0.050f * sampleRate);
    const size_t n100ms = static_cast<size_t>(0.100f * sampleRate);

    m.peakEnergy250ms = 0.0;
    size_t peakIdx = 0;
    for (size_t i = 0; i < std::min(count, n250ms); ++i) {
        if (wetEnergy[i] > m.peakEnergy250ms) {
            m.peakEnergy250ms = wetEnergy[i];
            peakIdx = i;
        }
    }
    m.peakTimeMs = static_cast<double>(peakIdx) * 1000.0 / sampleRate;

    // Time to 10%, 50%, 90% of peak energy
    m.t10ms = -1.0;
    m.t50ms = -1.0;
    m.t90ms = -1.0;
    for (size_t i = 0; i < std::min(count, n250ms); ++i) {
        double tMs = static_cast<double>(i) * 1000.0 / sampleRate;
        if (m.t10ms < 0.0 && wetEnergy[i] >= 0.10 * m.peakEnergy250ms) m.t10ms = tMs;
        if (m.t50ms < 0.0 && wetEnergy[i] >= 0.50 * m.peakEnergy250ms) m.t50ms = tMs;
        if (m.t90ms < 0.0 && wetEnergy[i] >= 0.90 * m.peakEnergy250ms) m.t90ms = tMs;
    }

    for (size_t i = 0; i < std::min(count, n250ms); ++i) {
        double e = wetEnergy[i];
        if (i < n10ms) m.energy_0_10 += e;
        else if (i < n25ms) m.energy_10_25 += e;
        else if (i < n50ms) m.energy_25_50 += e;
        else if (i < n100ms) m.energy_50_100 += e;
        else m.energy_100_250 += e;

        m.earlyEnergy250 += static_cast<double>(earlyL[i])*earlyL[i] + static_cast<double>(earlyR[i])*earlyR[i];
        m.granularEnergy250 += static_cast<double>(granL[i])*granL[i] + static_cast<double>(granR[i])*granR[i];
        m.diffuserEnergy250 += static_cast<double>(diffL[i])*diffL[i] + static_cast<double>(diffR[i])*diffR[i];
        m.fdnEnergy250 += static_cast<double>(tailL[i])*tailL[i] + static_cast<double>(tailR[i])*tailR[i];
    }
    m.energy_0_250_total = m.energy_0_10 + m.energy_10_25 + m.energy_25_50 + m.energy_50_100 + m.energy_100_250;

    return m;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "           NIMBUS / GREYCLOUD DSP ONSET AUDIT                    \n";
    std::cout << "=================================================================\n\n";

    const float sampleRate = 48000.0f;
    fs::create_directories("audit_output");

    auto signals = generateSignals(sampleRate, 1.5f);

    // Save input signals to WAV for listening
    for (const auto& sig : signals) {
        writeWav16("audit_output/input_" + sig.name + ".wav", sig.samples, sig.samples, sampleRate);
    }

    std::vector<CloudGreyVerb::Preset> presetsToTest = {
        CloudGreyVerb::Preset::AlwaysOnSubtle,
        CloudGreyVerb::Preset::SmallCloudRoom,
        CloudGreyVerb::Preset::BrightCloud,
        CloudGreyVerb::Preset::GreyholeDelayVerb,
        CloudGreyVerb::Preset::DarkLongCloud
    };

    std::vector<AuditMetrics> allMetrics;

    for (auto preset : presetsToTest) {
        for (const auto& sig : signals) {
            std::string stemPrefix = "";
            // Export audio stems for BrightCloud and AlwaysOnSubtle
            if (preset == CloudGreyVerb::Preset::BrightCloud || preset == CloudGreyVerb::Preset::AlwaysOnSubtle) {
                stemPrefix = "audit_output/" + std::string(CloudGreyVerb::getFactoryPreset(preset).name) + "_" + sig.name;
            }
            auto m = runAudit(preset, sig, sampleRate, 0, stemPrefix);
            allMetrics.push_back(m);
        }
    }

    // Print Table 1: Arrival times and peak build
    std::cout << "### TABELA 1: AUDITORIA TEMPORAL DE ONSET (BASELINE ATUAL)\n\n";
    std::cout << "| Preset | Sinal | PreDelay | 1st Wet | 1st ER | 1st Gran | 1st FDN | t(10%) | t(50%) | t(90%) | Peak(ms) |\n";
    std::cout << "|:-------|:------|:---------|:--------|:-------|:---------|:--------|:-------|:-------|:-------|:---------|\n";
    for (const auto& m : allMetrics) {
        std::cout << "| " << std::left << std::setw(18) << m.presetName
                  << " | " << std::setw(10) << m.signalName
                  << " | " << std::fixed << std::setprecision(1) << std::setw(5) << m.preDelayMs << " ms"
                  << " | " << std::setw(5) << m.firstWetArrivalMs << " ms"
                  << " | " << std::setw(5) << m.firstEarlyArrivalMs << " ms"
                  << " | " << std::setw(5) << m.firstGranularArrivalMs << " ms"
                  << " | " << std::setw(5) << m.firstFdnArrivalMs << " ms"
                  << " | " << std::setw(5) << m.t10ms << " ms"
                  << " | " << std::setw(5) << m.t50ms << " ms"
                  << " | " << std::setw(5) << m.t90ms << " ms"
                  << " | " << std::setw(5) << m.peakTimeMs << " ms |\n";
    }
    std::cout << "\n";

    // Print Table 2: Accumulated energy in first 250ms
    std::cout << "### TABELA 2: DISTRIBUIÇÃO TEMPORAL DE ENERGIA WET (0 a 250 ms)\n\n";
    std::cout << "| Preset | Sinal | 0-10ms (%) | 10-25ms (%) | 25-50ms (%) | 50-100ms (%) | 100-250ms (%) | Total Energy |\n";
    std::cout << "|:-------|:------|:-----------|:------------|:------------|:-------------|:--------------|:-------------|\n";
    for (const auto& m : allMetrics) {
        double tot = m.energy_0_250_total > 1e-12 ? m.energy_0_250_total : 1e-12;
        double p0 = 100.0 * m.energy_0_10 / tot;
        double p1 = 100.0 * m.energy_10_25 / tot;
        double p2 = 100.0 * m.energy_25_50 / tot;
        double p3 = 100.0 * m.energy_50_100 / tot;
        double p4 = 100.0 * m.energy_100_250 / tot;
        std::cout << "| " << std::left << std::setw(18) << m.presetName
                  << " | " << std::setw(10) << m.signalName
                  << " | " << std::fixed << std::setprecision(1) << std::setw(7) << p0 << "%"
                  << " | " << std::setw(8) << p1 << "%"
                  << " | " << std::setw(8) << p2 << "%"
                  << " | " << std::setw(9) << p3 << "%"
                  << " | " << std::setw(10) << p4 << "%"
                  << " | " << std::scientific << std::setprecision(2) << m.energy_0_250_total << " |\n";
    }
    std::cout << "\n";

    // Print Table 3: Component Energy Distribution (Who causes the hit?)
    std::cout << "### TABELA 3: ENERGIA POR COMPONENTE NOS PRIMEIROS 250 ms\n\n";
    std::cout << "| Preset | Sinal | Early Refl | Granular Cloud | Diffuser Out | FDN Late Tail |\n";
    std::cout << "|:-------|:------|:-----------|:---------------|:-------------|:--------------|\n";
    for (const auto& m : allMetrics) {
        double compTotal = m.earlyEnergy250 + m.granularEnergy250 + m.diffuserEnergy250 + m.fdnEnergy250 + 1e-12;
        std::cout << "| " << std::left << std::setw(18) << m.presetName
                  << " | " << std::setw(10) << m.signalName
                  << " | " << std::fixed << std::setprecision(1) << std::setw(7) << (100.0 * m.earlyEnergy250 / compTotal) << "%"
                  << " | " << std::setw(11) << (100.0 * m.granularEnergy250 / compTotal) << "%"
                  << " | " << std::setw(9) << (100.0 * m.diffuserEnergy250 / compTotal) << "%"
                  << " | " << std::setw(10) << (100.0 * m.fdnEnergy250 / compTotal) << "% |\n";
    }
    std::cout << "\n";

    // Part 4: Dynamic Damping A/B/C Test
    std::cout << "### TABELA 4: COMPARAÇÃO A/B/C DO DYNAMIC DAMPING (BrightCloud com HarpPluck)\n\n";
    auto harpSig = signals[1]; // HarpPluck
    auto mA = runAudit(CloudGreyVerb::Preset::BrightCloud, harpSig, sampleRate, 0, "audit_output/BrightCloud_Harp_ModeA");
    auto mB = runAudit(CloudGreyVerb::Preset::BrightCloud, harpSig, sampleRate, 1, "audit_output/BrightCloud_Harp_ModeB");
    auto mC = runAudit(CloudGreyVerb::Preset::BrightCloud, harpSig, sampleRate, 2, "audit_output/BrightCloud_Harp_ModeC");

    std::cout << "| Modo Dynamic Damping | Cutoff Min (Hz) | Cutoff Max (Hz) | Variação (Delta Hz) | Peak Time (ms) | Energia 25-50ms | Energia 50-100ms |\n";
    std::cout << "|:----------------------|:----------------|:----------------|:--------------------|:---------------|:----------------|:-----------------|\n";
    std::cout << "| A (Atual / Baseline)  | " << std::fixed << std::setprecision(0) << mA.minDynamicLpFreq << " Hz"
              << " | " << mA.maxDynamicLpFreq << " Hz"
              << " | " << (mA.maxDynamicLpFreq - mA.minDynamicLpFreq) << " Hz (" << std::setprecision(1) << (100.0f * (mA.maxDynamicLpFreq - mA.minDynamicLpFreq) / mA.maxDynamicLpFreq) << "%)"
              << " | " << mA.peakTimeMs << " ms"
              << " | " << std::scientific << std::setprecision(2) << mA.energy_25_50
              << " | " << mA.energy_50_100 << " |\n";
    std::cout << "| B (Desabilitado)      | " << std::fixed << std::setprecision(0) << mB.minDynamicLpFreq << " Hz"
              << " | " << mB.maxDynamicLpFreq << " Hz"
              << " | " << (mB.maxDynamicLpFreq - mB.minDynamicLpFreq) << " Hz (0.0%)"
              << " | " << mB.peakTimeMs << " ms"
              << " | " << std::scientific << std::setprecision(2) << mB.energy_25_50
              << " | " << mB.energy_50_100 << " |\n";
    std::cout << "| C (Sutil / Proteção)  | " << std::fixed << std::setprecision(0) << mC.minDynamicLpFreq << " Hz"
              << " | " << mC.maxDynamicLpFreq << " Hz"
              << " | " << (mC.maxDynamicLpFreq - mC.minDynamicLpFreq) << " Hz (" << std::setprecision(1) << (100.0f * (mC.maxDynamicLpFreq - mC.minDynamicLpFreq) / mC.maxDynamicLpFreq) << "%)"
              << " | " << mC.peakTimeMs << " ms"
              << " | " << std::scientific << std::setprecision(2) << mC.energy_25_50
              << " | " << mC.energy_50_100 << " |\n\n";

    return 0;
}
