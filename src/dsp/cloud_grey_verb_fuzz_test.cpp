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
static float extMem[kBufferSize];

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
        
        // Critical fuzz target: reverse logic combinations
        p.reverseMix = prng.randFloat();
        p.grainScan = prng.randFloat();

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
    
    runNullBufferTest();
    runModulationExtremeTest();
    runFuzzTest();
    
    cout << "All Extreme & Fuzz tests completed successfully." << endl;
    return 0;
}
