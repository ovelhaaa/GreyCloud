#include <iostream>
#include <cmath>
#include <cassert>
#include "cloud_grey_verb.hpp"
#include "dsp_utils.hpp"

int main() {
    float tapFixoOriginal = 5000.5f;
    float anchorScanCompleto = 6000.5f;
    float readPosReverse = 4000.5f;
    
    float readPosForward = cgv_dsp::lerp(tapFixoOriginal, anchorScanCompleto, 0.0f);
    float readPos = cgv_dsp::lerp(readPosForward, readPosReverse, 0.0f);
    
    std::cout << "A/B test verification for readPos at grainScan=0.0 and reverseMix=0.0:" << std::endl;
    std::cout << "Original Tap Position = " << tapFixoOriginal << std::endl;
    std::cout << "readPosForward (grainScan=0.0) = " << readPosForward << std::endl;
    std::cout << "final readPos (reverseMix=0.0) = " << readPos << std::endl;

    if (readPos == tapFixoOriginal) {
        std::cout << "BIT-EXACTNESS CONFIRMED! Diff is exactly 0." << std::endl;
    } else {
        std::cout << "NOT BIT-EXACT! Diff = " << std::abs(readPos - tapFixoOriginal) << std::endl;
        return 1;
    }

    return 0;
}
