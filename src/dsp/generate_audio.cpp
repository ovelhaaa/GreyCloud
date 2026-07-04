#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include "cloud_grey_verb.hpp"
#include "dsp_utils.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output_file.raw>" << std::endl;
        return 1;
    }

    const float sampleRate = 48000.0f;
    const size_t numFrames = 48000 * 2; // 2 seconds
    std::vector<float> inL(numFrames, 0.0f);
    std::vector<float> inR(numFrames, 0.0f);

    // Generate signal: 100ms noise, then silence, then an impulse at 1s
    cgv_dsp::FastPRNG prng;
    for (size_t i = 0; i < 4800; ++i) {
        inL[i] = prng.randFloat() * 2.0f - 1.0f;
        inR[i] = prng.randFloat() * 2.0f - 1.0f;
    }
    inL[48000] = 1.0f;
    inR[48000] = 1.0f;

    std::vector<float> memory(1024 * 1024 * 2); // 2M floats
    CloudGreyVerb cgv;
    cgv.init(sampleRate, memory.data(), memory.size());

    CloudGreyVerb::Params p;
    // Set all params to neutral or extremes that trigger granular
    p.mix = 1.0f; // 100% wet
    p.texture = 0.8f;
    p.freeze = 0.0f;
    p.feedback = 0.5f;
    p.size = 0.5f;
    p.diffusion = 0.5f;
    p.modDepth = 0.5f;
    p.modRate = 0.5f;
    p.damping = 0.5f;
    p.tone = 0.5f;
    p.shimmer = 0.0f;
    p.inputGain = 1.0f;
    p.outputGain = 1.0f;
    p.stereoCore = true;
    p.hardFreeze = false;
    
    // Explicitly set the new parameters to 0
    // But since the old commit doesn't have them in Params, we must use #ifdef or conditionally compile
    // Actually, in the old commit, Params struct is smaller.
    // If we zero-initialize it using `= {0}`, it works for both.
    // Let's just memset to 0 and set the fields we know exist in BOTH.
    
    // Because C++ might fail to compile if we mention `reverseMix` on the old commit,
    // we use a macro check if it exists, or just don't set it (it defaults to 0).
    // The default constructor of Params zeros everything.
    
    cgv.setParams(p);
    
    std::vector<float> outL(numFrames, 0.0f);
    std::vector<float> outR(numFrames, 0.0f);

    // We process block by block of 64
    for (size_t i = 0; i < numFrames; i += 64) {
        size_t block = std::min((size_t)64, numFrames - i);
        float tempL[64];
        float tempR[64];
        for(size_t j=0; j<block; ++j) {
            tempL[j] = inL[i+j];
            tempR[j] = inR[i+j];
        }
        cgv.processBlock(tempL, tempR, block);
        for(size_t j=0; j<block; ++j) {
            outL[i+j] = tempL[j];
            outR[i+j] = tempR[j];
        }
    }

    std::ofstream fout(argv[1], std::ios::binary);
    fout.write((char*)outL.data(), outL.size() * sizeof(float));
    fout.write((char*)outR.data(), outR.size() * sizeof(float));
    fout.close();

    return 0;
}
