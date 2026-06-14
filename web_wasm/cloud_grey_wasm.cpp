#include "../src/dsp/cloud_grey_verb.hpp"
#include <stdlib.h>
#include <math.h>

static CloudGreyVerb cgv;
static CloudGreyVerb::Params cgv_params;
static float* reverbMemory = nullptr;
static bool isInit = false;
static float lastPeak = 0.0f;

extern "C" {

int cgv_init(float sampleRate, int memoryFloats) {
    if (reverbMemory) { 
        free(reverbMemory); 
        reverbMemory = nullptr; 
    }
    reverbMemory = (float*)malloc(memoryFloats * sizeof(float));
    if (!reverbMemory) return 0; // Failure
    
    cgv.init(sampleRate, reverbMemory, (size_t)memoryFloats);
    isInit = true;
    return 1; // Success
}

void cgv_reset() {
    if (isInit) cgv.reset();
}

void cgv_set_param(int paramId, float value) {
    switch(paramId) {
        case 0: cgv_params.mix = value; break;
        case 1: cgv_params.texture = value; break;
        case 2: cgv_params.freeze = value; break;
        case 3: cgv_params.feedback = value; break;
        case 4: cgv_params.size = value; break;
        case 5: cgv_params.diffusion = value; break;
        case 6: cgv_params.modDepth = value; break;
        case 7: cgv_params.modRate = value; break;
        case 8: cgv_params.damping = value; break;
        case 9: cgv_params.tone = value; break;
        // shimmer is disabled directly, skipped
        case 10: cgv_params.inputGain = value; break;
        case 11: cgv_params.outputGain = value; break;
    }
    if (isInit) cgv.setParams(cgv_params);
}

float cgv_get_param(int paramId) {
    switch(paramId) {
        case 0: return cgv_params.mix;
        case 1: return cgv_params.texture;
        case 2: return cgv_params.freeze;
        case 3: return cgv_params.feedback;
        case 4: return cgv_params.size;
        case 5: return cgv_params.diffusion;
        case 6: return cgv_params.modDepth;
        case 7: return cgv_params.modRate;
        case 8: return cgv_params.damping;
        case 9: return cgv_params.tone;
        case 10: return cgv_params.inputGain;
        case 11: return cgv_params.outputGain;
    }
    return 0.0f;
}

void cgv_set_preset(int presetId) {
    cgv_params = CloudGreyVerb::getPreset(static_cast<CloudGreyVerb::Preset>(presetId));
    if (isInit) cgv.setParams(cgv_params);
}

void cgv_process(float* left, float* right, int frames) {
    if (!isInit) return;
    
    // Process block
    cgv.processBlock(left, right, (size_t)frames);
    
    // Peak extraction for UI
    lastPeak = 0.0f;
    for (int i = 0; i < frames; i++) {
        float absL = fabsf(left[i]);
        float absR = fabsf(right[i]);
        if (absL > lastPeak) lastPeak = absL;
        if (absR > lastPeak) lastPeak = absR;
    }
}

float cgv_get_peak() {
    return lastPeak;
}

int cgv_is_initialized() {
    return isInit ? 1 : 0;
}

} // extern "C"
