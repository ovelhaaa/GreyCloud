#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <string>
#include "cloud_grey_verb.hpp"
#include "dsp_utils.hpp"

using namespace std;

// Funções de geração de sinal
void generateSilence(float* left, float* right, int numFrames) {
    for (int i = 0; i < numFrames; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
}

void generateImpulse(float* left, float* right, int numFrames) {
    if (numFrames > 0) { left[0] = 1.0f; right[0] = 1.0f; }
    for (int i = 1; i < numFrames; ++i) { left[i] = 0.0f; right[i] = 0.0f; }
}

void generateSine(float* left, float* right, int numFrames, float freqHz, float sampleRate, float dbFS) {
    float amp = powf(10.0f, dbFS / 20.0f);
    float phaseInc = 2.0f * dsp::PI * freqHz / sampleRate;
    static float phase = 0.0f;
    for (int i = 0; i < numFrames; ++i) {
        float sample = sinf(phase) * amp;
        left[i] = sample;
        right[i] = sample;
        phase += phaseInc;
        if (phase > 2.0f * dsp::PI) phase -= 2.0f * dsp::PI;
    }
}

void generateNoise(float* left, float* right, int numFrames, float dbFS) {
    float amp = powf(10.0f, dbFS / 20.0f);
    static dsp::FastPRNG noiseRng;
    for (int i = 0; i < numFrames; ++i) {
        float sample = (noiseRng.randFloat() * 2.0f - 1.0f) * amp;
        left[i] = sample;
        right[i] = sample;
    }
}

// Configs do Teste
const float SAMPLE_RATE = 48000.0f;
const size_t BUFFER_SIZE = static_cast<size_t>(SAMPLE_RATE * 3.0f); // 3 segundos de max delay

struct TestResult {
    string presetName;
    bool passed = true;
    float maxPeak = 0.0f;
    int numClips = 0;
    int numSamples = 0;
    float minSafetyGain = 1.0f;
    float maxLoopEnergy = 0.0f;
    bool hadNaN = false;
};

TestResult runTestForPreset(CloudGreyVerb::Preset preset, const string& presetName, float shimmerOverride = -1.0f) {
    TestResult result;
    result.presetName = presetName;

    CloudGreyVerb cgv;
    vector<float> extBuffer(BUFFER_SIZE, 0.0f);
    cgv.init(SAMPLE_RATE, extBuffer.data(), BUFFER_SIZE);

    CloudGreyVerb::Params p = CloudGreyVerb::getPreset(preset);
    if (shimmerOverride >= 0.0f) {
        p.shimmer = shimmerOverride;
    }
    cgv.setParams(p);

    const int chunkFrames = 512;
    vector<float> left(chunkFrames), right(chunkFrames);

    auto processAndMeasure = [&](int durSeconds, int stageIdx, bool freeze = false) {
        int totalFrames = durSeconds * static_cast<int>(SAMPLE_RATE);
        int chunks = totalFrames / chunkFrames;
        
        if (freeze && p.freeze < 0.5f) {
            p.freeze = 1.0f;
            cgv.setParams(p);
        } else if (!freeze && p.freeze > 0.5f && preset != CloudGreyVerb::Preset::FrozenOrganPad) {
            p.freeze = 0.0f;
            cgv.setParams(p);
        }

        for (int c = 0; c < chunks; ++c) {
            // Gerar input para este chunk
            if (stageIdx == 0) generateSilence(left.data(), right.data(), chunkFrames);
            else if (stageIdx == 1) { 
                if (c == 0) generateImpulse(left.data(), right.data(), chunkFrames);
                else generateSilence(left.data(), right.data(), chunkFrames);
            }
            else if (stageIdx == 2) generateSine(left.data(), right.data(), chunkFrames, 80.0f, SAMPLE_RATE, -12.0f);
            else if (stageIdx == 3) generateSine(left.data(), right.data(), chunkFrames, 110.0f, SAMPLE_RATE, -12.0f);
            else if (stageIdx == 4) generateNoise(left.data(), right.data(), chunkFrames, -18.0f);
            else generateSilence(left.data(), right.data(), chunkFrames);

            // Process
            cgv.processBlock(left.data(), right.data(), chunkFrames);

            // Medir
            for (int i = 0; i < chunkFrames; ++i) {
                float absL = fabsf(left[i]);
                float absR = fabsf(right[i]);
                
                if (left[i] != left[i] || right[i] != right[i]) result.hadNaN = true;
                
                if (absL > result.maxPeak) result.maxPeak = absL;
                if (absR > result.maxPeak) result.maxPeak = absR;

                if (absL >= 0.999f) result.numClips++;
                if (absR >= 0.999f) result.numClips++;
                
                result.numSamples += 2;
            }
            
            float sg = cgv.getSafetyGain();
            float le = cgv.getLoopEnergy();
            if (sg < result.minSafetyGain) result.minSafetyGain = sg;
            if (le > result.maxLoopEnergy) result.maxLoopEnergy = le;
        }
    };

    // Estágios de teste (como requisitado)
    processAndMeasure(10, 0); // silêncio
    processAndMeasure(5, 1);  // impulse + silêncio tail
    processAndMeasure(10, 2); // 80Hz
    processAndMeasure(10, 3); // 110Hz
    processAndMeasure(10, 4); // Noise
    
    // Teste de Freeze on/off abusivo
    processAndMeasure(2, 4); // Noise
    processAndMeasure(5, 4, true); // Freeze on e noise rolando (deve ignorar nova input ou limitar)
    processAndMeasure(5, 0, false); // silêncio c/ freeze off tail

    // Regras de Falha
    if (result.hadNaN) result.passed = false;
    if (result.maxPeak > 1.0f) result.passed = false;
    float clipRatio = (float)result.numClips / (float)result.numSamples;
    if (clipRatio > 0.01f) result.passed = false;
    // Se o safety gain diminui muito e fica colado, pode indicar instabilidade extrema.
    // Mas se descer < 0.5 transitoriamente e proteger o loop, é um sucesso do safety!
    
    return result;
}

int main() {
    cout << "--- CloudGreyVerb Abuse Test ---" << endl;
    cout << "Sample Rate = " << SAMPLE_RATE << " Hz" << endl;
    cout << "------------------------------------------" << endl;
    
    vector<pair<CloudGreyVerb::Preset, string>> tests = {
        {CloudGreyVerb::Preset::SmallCloudRoom, "SmallCloudRoom"},
        {CloudGreyVerb::Preset::BassAmbientWash, "BassAmbientWash"},
        {CloudGreyVerb::Preset::FrozenOrganPad, "FrozenOrganPad"},
        {CloudGreyVerb::Preset::GreyholeDelayVerb, "GreyholeDelayVerb"},
        {CloudGreyVerb::Preset::DarkLongCloud, "DarkLongCloud"},
        {CloudGreyVerb::Preset::GlitchSmear, "GlitchSmear"},
        {CloudGreyVerb::Preset::AlwaysOnSubtle, "AlwaysOnSubtle"},
        {CloudGreyVerb::Preset::BrightCloud, "BrightCloud"},
        {CloudGreyVerb::Preset::ShimmerCloud, "ShimmerCloud"}
    };
    
    int passedCount = 0;

    cout << left << setw(20) << "Preset Name" 
         << setw(10) << "Passed" 
         << setw(12) << "Max Peak"
         << setw(12) << "Min Safety"
         << setw(12) << "Max Energy"
         << setw(15) << "Clip %" 
         << "NaN?" << endl;
    cout << string(90, '-') << endl;

    auto printResult = [](const TestResult& r) {
        float clipPct = (float)r.numClips / (float)r.numSamples * 100.0f;
        cout << left << setw(20) << r.presetName 
             << setw(10) << (r.passed ? "YES" : "NO")
             << setw(12) << fixed << setprecision(4) << r.maxPeak
             << setw(12) << r.minSafetyGain
             << setw(12) << r.maxLoopEnergy
             << setw(15) << fixed << setprecision(3) << clipPct
             << (r.hadNaN ? "YES" : "NO") << endl;
    };

    int totalTests = 0;
    for (const auto& t : tests) {
        TestResult r = runTestForPreset(t.first, t.second);
        printResult(r);
        if (r.passed) passedCount++;
        totalTests++;
    }
    
    // Testes de estresse Shimmer
    vector<float> shimmerValues = {0.0f, 0.25f, 0.5f, 1.0f};
    for (float shm : shimmerValues) {
        string name = "SafeShimmer_" + to_string((int)(shm * 100)) + "pct";
        TestResult r = runTestForPreset(CloudGreyVerb::Preset::AlwaysOnSubtle, name, shm);
        printResult(r);
        if (r.passed) passedCount++;
        totalTests++;
    }

    cout << string(90, '-') << endl;
    cout << "Testes passando: " << passedCount << "/" << totalTests << endl;

    if (passedCount == totalTests) {
        cout << "SUCESSO GERAL!" << endl;
        return 0;
    } else {
        cout << "FALHA: Um ou mais presets falharam no teste de stress." << endl;
        return 1;
    }
}
