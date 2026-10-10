// Diagnostic benchmark; no instrumentation, RNG, trig, I/O or allocation in
// the measured loop. Build against the baseline with identical compiler flags.
#include "cloud_grey_verb.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
struct CloudGreyVerbGranularTestAccess {
    static void process(CloudGreyVerb &v, float x, float &l, float &r) { v.processGranular(x, x, 0, l, r); }
};
int main() {
    constexpr int frames = 480000;
    std::vector<float> input(frames);
    for (int n = 0; n < frames; ++n)
        input[n] = 0.3f * std::sin(2 * cgv_dsp::PI * 997 * n / 48000.f);
    for (int mode = 0; mode < 3; ++mode) {
        std::vector<float> memory(CloudGreyVerb::requiredMemoryFloats(48000));
        CloudGreyVerb v;
        v.init(48000, memory.data(), memory.size());
        auto p = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BrightCloud);
        p.grainScan = mode == 2 ? 0.f : .5f;
        p.reverseMix = mode == 2 ? 0.f : .5f;
        p.texture = 1;
        p.clipOutput = false;
        double timings[5], checksum = 0;
        for (int rep = 0; rep < 5; ++rep) {
            v.setParams(p);
            v.reset();
            auto start = std::chrono::steady_clock::now();
            for (int n = 0; n < frames; ++n) {
                if (mode == 2 && n % 960 == 0) {
                    p.grainScan = 1 - p.grainScan;
                    p.reverseMix = 1 - p.reverseMix;
                    v.setParams(p);
                }
                float l, r;
                if (mode == 1)
                    v.processSample(input[n], input[n], l, r);
                else
                    CloudGreyVerbGranularTestAccess::process(v, input[n], l, r);
                checksum += double(l) + r;
            }
            timings[rep] =
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() /
                frames;
        }
        std::sort(timings, timings + 5);
        std::cout << "mode=" << mode << " median_ns_per_sample=" << timings[2]
                  << " memory_floats=" << memory.size() << " state_bytes=" << sizeof(CloudGreyVerb)
                  << " checksum=" << checksum << '\n';
    }
}
