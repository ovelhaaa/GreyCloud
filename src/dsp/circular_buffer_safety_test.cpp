#include "cloud_grey_verb.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

static bool countAudioAllocations = false;
static uint64_t audioAllocations = 0;
void* operator new(size_t size) {
    if (countAudioAllocations) ++audioAllocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

struct ShimmerPitcherTestAccess {
#if CGV_ENABLE_SHIMMER
    static float read(ShimmerPitcher& s, uint32_t write, float delay) {
        s.writePos_ = write;
        return s.readDelay(delay);
    }
#endif
};

struct CloudGreyVerbGranularTestAccess {
    static size_t size(const CloudGreyVerb& v) { return v.grainMemorySize_; }
    static void fill(CloudGreyVerb& v) {
        for (size_t i = 0; i < v.grainMemorySize_; ++i) {
            const double phase = 2.0 * 3.141592653589793 * i / v.grainMemorySize_;
            v.grainMemoryL_[i] = 0.125f + 0.2f * float(std::sin(phase));
            v.grainMemoryR_[i] = -0.375f + 0.1f * float(std::cos(phase));
        }
    }
    static const float* left(const CloudGreyVerb& v) { return v.grainMemoryL_; }
    static const float* right(const CloudGreyVerb& v) { return v.grainMemoryR_; }
    static void read(CloudGreyVerb& v, float pos, float& l, float& r) {
        v.readGrainHead(pos, l, r);
    }
};

static int failures = 0;
static void check(bool ok, const char* name, size_t size, float value) {
    if (!ok) {
        if (failures < 20) std::cerr << "FAIL " << name << " size=" << size << " position=" << value << '\n';
        ++failures;
    }
}

static long double wrap(float pos, size_t size) {
    long double p = std::fmod(static_cast<long double>(pos), static_cast<long double>(size));
    if (p < 0) p += size;
    return p >= size ? 0 : p;
}

static float reference(const float* data, size_t size, float raw, bool hermite) {
    const long double pos = wrap(raw, size);
    const size_t i1 = size_t(pos), i0 = i1 ? i1 - 1 : size - 1;
    const size_t i2 = i1 + 1 == size ? 0 : i1 + 1, i3 = i2 + 1 == size ? 0 : i2 + 1;
    const float frac = float(pos - i1);
    return hermite ? cgv_dsp::hermite(frac, data[i0], data[i1], data[i2], data[i3])
                   : cgv_dsp::lerp(data[i1], data[i2], frac);
}

int main() {
    const float inf = std::numeric_limits<float>::infinity();
    // Ring-relative sub-ULP values, representable neighbours, and many wraps.
    for (size_t size : {size_t(1), size_t(2), size_t(3), size_t(31), size_t(4096), size_t(154564), size_t(524287)}) {
        if (size == 0 || size > 524287) return 1;
        const float length = float(size);
        std::vector<float> memory(size + 1, 98765.0f);
        cgv_dsp::DelayLine line;
        line.init(memory.data(), size);
        for (size_t i = 0; i < size; ++i) {
            const float x = 0.125f + 0.2f * std::sin(float(2.0 * 3.141592653589793 * i / size));
            line.write(x);
        }
        for (size_t write : {size_t(0), size_t(1) % size, size - 1}) {
            line.clear();
            for (size_t i = 0; i < size; ++i)
                line.write(0.125f + 0.2f * std::sin(float(2.0 * 3.141592653589793 * i / size)));
            for (size_t i = 0; i < write; ++i) line.write(memory[i]);
            for (float delay : {1.f, std::nextafter(1.f, inf), std::nextafter(float(write), inf),
                                length, std::nextafter(length, 0.f), length * 3.25f, -length, 1.5f}) {
                const float clamped = std::max(1.f, std::min(length, delay));
                const float raw = float(write) - clamped;
                const float expected = reference(memory.data(), size, raw, true);
                // Float wrapping can quantize by half a ring-length ULP. The
                // periodic waveform's slope scales as 1/size, so error stays
                // below this tolerance even for the largest audio capacities.
                check(std::abs(line.read(delay) - expected) < 2e-6f, "DelayLine", size, raw);
            }
        }
        check(line.read(std::numeric_limits<float>::quiet_NaN()) == 0, "DelayLine NaN", size, 0);
        check(memory[size] == 98765.0f, "sentinel write", size, 0);
#if !defined(CGV_M6_BASELINE)
        // Exact allocation: ASan must also see a physical overread, rather
        // than only the sentinel test's logical sub-buffer violation.
        std::vector<float> exact(size);
        cgv_dsp::DelayLine unpadded;
        unpadded.init(exact.data(), size);
        unpadded.write(0.125f);
        check(std::abs(unpadded.read(std::nextafter(1.f, inf)) - 0.125f) < 1e-6f,
              "DelayLine exact allocation", size, 0);
#endif
    }

#if CGV_ENABLE_SHIMMER
    for (size_t size : {size_t(3072), size_t(154564), size_t(524287)}) {
        std::vector<float> memory(size);
        ShimmerPitcher s;
        check(s.init(48000, memory.data(), uint32_t(size)), "Shimmer init", size, 0);
        for (size_t i = 0; i < size; ++i)
            memory[i] = 0.125f + 0.2f * std::sin(float(2.0 * 3.141592653589793 * i / size));
        for (uint32_t write : {0u, 1u, uint32_t(size - 1)})
            for (float delay : {0.00001f, -0.00001f, std::nextafter(1.f, inf), -float(size),
                                float(size) * 4.25f, -float(size) * 3.5f, 1.5f,
                                std::nextafter(float(size), 0.f), std::nextafter(float(size), inf)}) {
                float raw = float(write) - delay;
                check(std::abs(ShimmerPitcherTestAccess::read(s, write, delay) -
                               reference(memory.data(), size, raw, false)) < 2e-6f, "Shimmer", size, raw);
            }
#if !defined(CGV_M6_BASELINE)
        check(ShimmerPitcherTestAccess::read(s, 0, inf) == 0 &&
              ShimmerPitcherTestAccess::read(s, 0, std::numeric_limits<float>::quiet_NaN()) == 0,
              "Shimmer non-finite delay", size, 0);
#endif
        // Separate transient/pitch regression below exercises all supported ratios.
    }
#endif

    for (float sr : {44100.f, 48000.f, 96000.f, 192000.f}) {
        std::vector<float> pool(CloudGreyVerb::requiredMemoryFloats(sr));
        CloudGreyVerb v;
        v.init(sr, pool.data(), pool.size());
        CloudGreyVerbGranularTestAccess::fill(v);
        const size_t size = CloudGreyVerbGranularTestAccess::size(v);
        const float length = float(size);
        for (float pos : {-0.00001f, 0.f, std::nextafter(length, 0.f), length,
                          std::nextafter(length, inf), -length, -length * 8.25f, length * 5.75f}) {
            float l, r;
            CloudGreyVerbGranularTestAccess::read(v, pos, l, r);
            check(std::abs(l - reference(CloudGreyVerbGranularTestAccess::left(v), size, pos, false)) < 2e-6f &&
                  std::abs(r - reference(CloudGreyVerbGranularTestAccess::right(v), size, pos, false)) < 2e-6f,
                  "granular stereo wrap", size, pos);
        }
        auto params = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::ShimmerCloud);
        params.clipOutput = false;
        v.setParams(params);
        v.reset();
        float left[257] = {}, right[257] = {};
        countAudioAllocations = true;
        for (int block = 0; block < 64; ++block) {
            params.freeze = float(block % 2);
            params.hardFreeze = block % 3 == 0;
            params.grainScan = float(block % 2);
            params.reverseMix = 1 - params.grainScan;
            params.shimmerRatioIndex = block % 5;
            v.setParams(params);
            v.processBlock(left, right, 257);
        }
        countAudioAllocations = false;
        check(audioAllocations == 0, "audio callback allocations", pool.size(), sr);
    }

    // Unity pitch retains a 29 ms left head and the right head's 7 ms offset.
#if CGV_ENABLE_SHIMMER
    for (float sr : {44100.f, 48000.f, 96000.f, 192000.f}) {
        std::vector<float> memory(size_t(sr * 0.064f));
        ShimmerPitcher s;
        s.init(sr, memory.data(), uint32_t(memory.size()));
        s.setRatio(1);
        s.reset();
        float delayL = 0.008f * sr + 0.042f * sr * 0.5f;
        float delayR = delayL + sr * 0.007f;
        for (size_t n = 0; n < size_t(sr * 0.1f); ++n) {
            float l, r;
            s.processStereo(n == 0 ? 0.1f : 0, l, r);
            auto impulse = [&](float delay) {
                const float pos = float(n + 1) - delay;
                return cgv_dsp::softClip(0.1f * std::max(0.f, 1.f - std::abs(pos)));
            };
            check(std::abs(l - impulse(delayL)) < 3e-5f && std::abs(r - impulse(delayR)) < 3e-5f,
                  "Shimmer latency/stereo", memory.size(), float(n));
        }
        // Twenty cycles fit the 42 ms scan depth. Both heads then agree in
        // phase, isolating pitch from the existing window-splice sidebands.
        const double inputFrequency = 20.0 / 0.042;
        for (float ratio : {0.5f, 1.5f, 2.f, 3.f, 4.f}) {
            s.setRatio(ratio);
            s.reset();
            const size_t count = size_t(sr);
            std::vector<float> left(count), right(count);
            for (size_t n = 0; n < 2 * count; ++n) {
                float l, r;
                s.processStereo(0.1f * std::sin(float(2.0 * 3.141592653589793 * inputFrequency * n / sr)), l, r);
                if (n >= count) { left[n-count] = l; right[n-count] = r; }
                check(std::isfinite(l) && std::isfinite(r), "Shimmer finite", memory.size(), float(n));
            }
            const double expected = inputFrequency * ratio;
            double bestEnergy = -1, bestFrequency = 0, energy = 0, stereoDifference = 0;
            for (size_t n = 0; n < count; ++n) {
                energy += double(left[n]) * left[n];
                stereoDifference += double(left[n]-right[n]) * (left[n]-right[n]);
            }
            for (int offset = -5; offset <= 5; ++offset) {
                const double frequency = expected + offset;
                double re = 0, im = 0;
                for (size_t n = 0; n < count; ++n) {
                    const double phase = 2.0 * 3.141592653589793 * frequency * n / sr;
                    re += left[n] * std::cos(phase); im += left[n] * std::sin(phase);
                }
                const double spectralEnergy = re * re + im * im;
                if (spectralEnergy > bestEnergy) { bestEnergy = spectralEnergy; bestFrequency = frequency; }
            }
            std::cout << "pitch sr=" << sr << " ratio=" << ratio << " freq=" << bestFrequency << " concentration=" << 2*bestEnergy/(count*energy) << " stereo=" << stereoDifference/count << "\n";
            check(std::abs(bestFrequency-expected) <= expected * 0.01 &&
                  2 * bestEnergy / (count * energy) > 0.9 && stereoDifference / count > 1e-6,
                  "Shimmer pitch/stereo", memory.size(), ratio);
        }
    }
#endif

#if !defined(CGV_M6_BASELINE)
    for (size_t size : {size_t(1), size_t(3), size_t(154564), size_t(16777219), size_t(33554431)}) {
        const float length = float(size);
        for (float pos : {-std::numeric_limits<float>::denorm_min(), -0.00001f, -length * 100.f,
                          0.f, length, std::nextafter(length, inf), length * 100.f, inf}) {
            const auto p = cgv_dsp::circularPosition(pos, size);
            check(p.index < size && p.fraction >= 0 && p.fraction < 1, "normalized bounds", size, pos);
        }
    }
#endif
    std::cout << "circular safety failures=" << failures << '\n';
    return failures ? 1 : 0;
}
