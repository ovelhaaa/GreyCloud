/*
 * CloudGreyVerb Fuzz & Extreme Edge Case Tests
 * 
 * To compile and run locally:
 *   g++ -O3 -std=c++17 cloud_grey_verb_fuzz_test.cpp cloud_grey_verb.cpp -I. -o fuzz_test
 *   ./fuzz_test
 */

#include <iostream>
#include <cmath>
#include <vector>
#include "cloud_grey_verb.hpp"
#include "dsp_utils.hpp"

using namespace std;

static const size_t kBufferSize = 1600000;
static const size_t kGuardFloats = 16;
static const float kGuardValue = 12345.25f;
static float extMem[kBufferSize + kGuardFloats];

#if defined(CLOUD_GREY_PROFILE_H5_LOW_CPU) && CLOUD_GREY_PROFILE_H5_LOW_CPU
static_assert(CloudGreyVerb::kFdnOrder == 2, "LOW_CPU must keep the 2x2 fallback");
#else
static_assert(CloudGreyVerb::kFdnOrder == 4, "Main profiles must use the 4x4 FDN");
#endif

void runFdnMatrixTest() {
    cout << "--- FDN Hadamard Orthogonality Test ---" << endl;

    const float input[4] = {0.25f, -0.5f, 0.75f, -1.0f};
    float mixed[4] = {0.0f};
    float restored[4] = {0.0f};
    cgv_dsp::applyNormalizedHadamard4(input, mixed);
    cgv_dsp::applyNormalizedHadamard4(mixed, restored);

    float inputEnergy = 0.0f;
    float mixedEnergy = 0.0f;
    for (int i = 0; i < 4; ++i) {
        inputEnergy += input[i] * input[i];
        mixedEnergy += mixed[i] * mixed[i];
        if (std::abs(restored[i] - input[i]) > 1.0e-6f) {
            cerr << "FAIL: Hadamard matrix is not self-inverse at lane " << i << endl;
            exit(1);
        }
    }

    if (std::abs(inputEnergy - mixedEnergy) > 1.0e-6f) {
        cerr << "FAIL: Hadamard matrix did not preserve energy." << endl;
        exit(1);
    }

    cout << "FDN Hadamard Test Passed." << endl;
}

void runFdnStereoTailTest() {
    cout << "--- FDN " << CloudGreyVerb::kFdnOrder << "x"
         << CloudGreyVerb::kFdnOrder << " Stereo Tail Test ---" << endl;

    CloudGreyVerb fx;
    fx.init(48000.0f, extMem, kBufferSize);

    CloudGreyVerb::Params p;
    p.mix = 1.0f;
    p.texture = 0.35f;
    p.feedback = 0.78f;
    p.size = 0.12f;
    p.diffusion = 0.7f;
    p.modDepth = 0.35f;
    p.modRate = 0.2f;
    p.damping = 0.75f;
    p.lowDamping = 0.25f;
    p.stereoCore = true;
    fx.setParams(p);

    double tailEnergyL = 0.0;
    double tailEnergyR = 0.0;
    float outL = 0.0f;
    float outR = 0.0f;
    constexpr int kTestFrames = 48000 * 4;

    for (int i = 0; i < kTestFrames; ++i) {
        // Left-only burst: energy in the right tail must come from the network.
        const float input = (i < 4800)
            ? 0.2f * sinf(2.0f * cgv_dsp::PI * 997.0f * static_cast<float>(i) / 48000.0f)
            : 0.0f;
        fx.processSample(input, 0.0f, outL, outR);

        if (!std::isfinite(outL) || !std::isfinite(outR)
            || !std::isfinite(fx.getLoopEnergy())
            || !std::isfinite(fx.getSafetyGain())) {
            cerr << "FAIL: FDN telemetry/output became non-finite at frame " << i << endl;
            exit(1);
        }

        if (i > 24000) {
            tailEnergyL += static_cast<double>(outL) * outL;
            tailEnergyR += static_cast<double>(outR) * outR;
        }
    }

    if (tailEnergyL < 1.0e-7 || tailEnergyR < 1.0e-7) {
        cerr << "FAIL: FDN did not produce a stereo tail. L=" << tailEnergyL
             << " R=" << tailEnergyR << endl;
        exit(1);
    }

    cout << "FDN Stereo Tail Test Passed. L=" << tailEnergyL
         << " R=" << tailEnergyR << endl;
}

void runFuzzTest() {
    cout << "--- Fuzzing Parameters ---" << endl;
    CloudGreyVerb fx;
    fx.init(48000.0f, extMem, kBufferSize);
    
    cgv_dsp::FastPRNG prng;
    prng.seed(1337);

    for (int i = 0; i < 10000; i++) {
        CloudGreyVerb::Params p;
        p.mix = prng.randFloat();
        p.texture = prng.randFloat();
        p.freeze = prng.randFloat();
        p.feedback = prng.randFloat() * 0.94f;
        p.size = prng.randFloat();
        p.diffusion = prng.randFloat();
        p.modDepth = prng.randFloat();
        p.modRate = prng.randFloat();
        p.damping = prng.randFloat();
        p.lowDamping = prng.randFloat();
        p.tone = prng.randFloat();
        p.shimmer = prng.randFloat();
        p.inputGain = prng.randFloat() * 2.0f;
        p.outputGain = prng.randFloat() * 2.0f;
        p.preDelay = prng.randFloat();
        p.stereoWidth = prng.randFloat() * 2.0f;
        p.stereoCore = (prng.randFloat() > 0.5f);
        p.hardFreeze = (prng.randFloat() > 0.95f); // 5% chance of hard freeze
        
        // Critical fuzz target: reverse logic combinations + extremes
        if (i % 10 == 0) {
            // Force maximum stress on the reverse buffers and anchors
            p.reverseMix = (prng.randFloat() > 0.5f) ? 1.0f : 0.0f;
            p.grainScan = (prng.randFloat() > 0.5f) ? 1.0f : 0.0f;
            p.freeze = 1.0f;
            p.texture = 1.0f;
            p.size = 0.01f; // small buffer forces wrapping stress
        } else {
            p.reverseMix = prng.randFloat();
            p.grainScan = prng.randFloat();
        }

        fx.setParams(p);
        
        float outL = 0.0f, outR = 0.0f;
        // Process noise
        for (int j = 0; j < 50; j++) {
            float noise = prng.randFloat() * 2.0f - 1.0f;
            fx.processSample(noise, noise, outL, outR);
            if (std::isnan(outL) || std::isinf(outL)) {
                cerr << "FAIL: NaN/Inf detected on iteration " << i << endl;
                exit(1);
            }
            if (std::abs(outL) > 2.05f || std::abs(outR) > 2.05f) { // Soft margin due to limiting
                cerr << "FAIL: Output exploded beyond bounds! L=" << outL << " R=" << outR << endl;
                exit(1);
            }
        }
        
        // Process silence
        for (int j = 0; j < 50; j++) {
            fx.processSample(0.0f, 0.0f, outL, outR);
            if (std::isnan(outL) || std::isinf(outL)) {
                cerr << "FAIL: NaN/Inf detected on silence iteration " << i << endl;
                exit(1);
            }
        }
    }
    cout << "Fuzz Test Passed." << endl;
}

void runModulationExtremeTest() {
    cout << "--- Modulation Extremes Test ---" << endl;
    CloudGreyVerb fx;
    fx.init(48000.0f, extMem, kBufferSize);
    
    CloudGreyVerb::Params p;
    p.modDepth = 1.0f;
    p.modRate = 1.0f; // Max rate and depth
    p.size = 0.0f; // Minimal size to risk index overlapping
    fx.setParams(p);
    
    float outL = 0, outR = 0;
    // Process 10 seconds
    for (int i = 0; i < 48000 * 10; i++) {
        float in = (i == 0) ? 1.0f : 0.0f; // Impulse
        fx.processSample(in, in, outL, outR);
        if (std::isnan(outL) || std::isinf(outL)) {
            cerr << "FAIL: NaN/Inf detected in extreme modulation at sample " << i << endl;
            exit(1);
        }
    }
    cout << "Modulation Extremes Test Passed." << endl;
}

void runNullBufferTest() {
    cout << "--- Null/Tiny Buffer Test ---" << endl;
    CloudGreyVerb fx;
    
    float tinyMem[100];
    fx.init(48000.0f, tinyMem, 100);
    // Should fail gracefully and not crash
    float outL = 0, outR = 0;
    fx.processSample(1.0f, 1.0f, outL, outR);
    if (std::isnan(outL) || std::isinf(outL)) {
        cerr << "FAIL: NaN generated on tiny buffer!" << endl;
        exit(1);
    }
    
    fx.init(48000.0f, nullptr, kBufferSize);
    fx.processSample(1.0f, 1.0f, outL, outR);
    if (std::isnan(outL) || std::isinf(outL)) {
        cerr << "FAIL: NaN generated on null buffer!" << endl;
        exit(1);
    }
    cout << "Null Buffer Test Passed." << endl;
}

int main() {
    cout << "Starting CloudGreyVerb Test Suite..." << endl;

    for (size_t i = 0; i < kGuardFloats; ++i)
        extMem[kBufferSize + i] = kGuardValue;
    
    runNullBufferTest();
    runFdnMatrixTest();
    runFdnStereoTailTest();
    runModulationExtremeTest();
    runFuzzTest();

    for (size_t i = 0; i < kGuardFloats; ++i) {
        if (extMem[kBufferSize + i] != kGuardValue) {
            cerr << "FAIL: DSP wrote past the external memory boundary." << endl;
            return 1;
        }
    }
    
    cout << "All Extreme & Fuzz tests completed successfully." << endl;
    return 0;
}
