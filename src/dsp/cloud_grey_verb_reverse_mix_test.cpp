// Reverse Mix / grain direction regression test.
//
// Guarantees that reverseMix only crossfades between forward and reverse
// reproduction and never bends the nominal read-head speed (i.e. never shifts
// the pitch of the granular component).  It also reproduces the legacy
// position-space lerp to demonstrate the historical bug:
//
//   legacy reverseMix=0.25 -> ~half-speed
//   legacy reverseMix=0.50 -> stationary read head (no pitch)
//   legacy reverseMix=0.75 -> ~reverse half-speed
//
// The test is intentionally JUCE-free and builds with the plain DSP core.

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "cloud_grey_verb.hpp"
#include "dsp_utils.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;
// Desktop profile keeps the 0.5 s granular history.  Reserve exactly what the
// core reports so the guard test can detect any write past the boundary.
constexpr size_t kGuardFloats = 32;
constexpr float kGuardValue = 424242.25f;

size_t memoryFor() { return 4000000u; }

// ---------------------------------------------------------------------------
// Iterative radix-2 FFT, used only to find the dominant spectral peak.
// ---------------------------------------------------------------------------
void fft(std::vector<double>& re, std::vector<double>& im) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * cgv_dsp::PI / static_cast<double>(len);
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < len / 2; ++k) {
                const double a = ang * static_cast<double>(k);
                const double wr = std::cos(a);
                const double wi = std::sin(a);
                const double ur = re[i + k];
                const double ui = im[i + k];
                const double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                const double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr;
                im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr;
                im[i + k + len / 2] = ui - vi;
            }
        }
    }
}

// Dominant frequency (Hz) of the supplied signal after Hann windowing.  Returns
// the global magnitude peak with parabolic interpolation for sub-bin accuracy.
double dominantFrequency(const std::vector<float>& signal, double sampleRate) {
    size_t n = 1;
    while (n < signal.size()) n <<= 1;
    std::vector<double> re(n, 0.0), im(n, 0.0);
    for (size_t i = 0; i < signal.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * cgv_dsp::PI * static_cast<double>(i)
                                               / static_cast<double>(signal.size() - 1));
        re[i] = static_cast<double>(signal[i]) * w;
    }
    fft(re, im);

    const size_t bins = n / 2;
    std::vector<double> mag(bins, 0.0);
    for (size_t k = 0; k < bins; ++k) mag[k] = std::hypot(re[k], im[k]);

    size_t peak = 1;
    for (size_t k = 2; k + 1 < bins; ++k)
        if (mag[k] > mag[peak]) peak = k;

    double refined = static_cast<double>(peak);
    if (peak > 0 && peak + 1 < bins) {
        const double a = mag[peak - 1], b = mag[peak], c = mag[peak + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1.0e-12) refined += 0.5 * (a - c) / denom;
    }
    return refined * sampleRate / static_cast<double>(n);
}

// ---------------------------------------------------------------------------
// Real DSP harness: renders the granular component alone for a steady tone.
// ---------------------------------------------------------------------------
struct ToneResult {
    std::vector<float> samples;
    bool finite = true;
    float peak = 0.0f;
    double dominantHz = 0.0;
};

ToneResult renderGranularTone(float frequency, float reverseMix, float grainScan,
                              float texture, CloudGreyVerb::Preset preset,
                              bool usePreset, float freezeAmount = 0.0f) {
    std::vector<float> memory(memoryFor() + kGuardFloats, 0.0f);
    for (size_t i = 0; i < kGuardFloats; ++i)
        memory[memoryFor() + i] = kGuardValue;

    CloudGreyVerb fx;
    fx.init(kSampleRate, memory.data(), memoryFor());
    if (!fx.isInitialized()) throw std::runtime_error("CloudGreyVerb init failed");

    CloudGreyVerb::Params p = usePreset
        ? CloudGreyVerb::getPreset(preset)
        : CloudGreyVerb::Params{};
    p.mix = 1.0f;
    p.texture = texture;
    p.freeze = freezeAmount;
    p.hardFreeze = false;
    p.feedback = 0.0f;
    p.inputGain = 1.0f;
    p.outputGain = 1.0f;
    p.stereoCore = true;
    p.shimmer = 0.0f;
    p.clipOutput = false;
    p.reverseMix = reverseMix;
    p.grainScan = grainScan;
    fx.setParams(p);
    fx.reset();

    CloudGreyVerbComponentTestAccess::setIsolation(
        fx, CloudGreyVerb::ComponentIsolation::GranularOnly);

    const size_t warmup = static_cast<size_t>(kSampleRate * 0.6f);
    const size_t captured = 65536;
    ToneResult result;
    result.samples.reserve(captured);

    for (size_t i = 0; i < warmup + captured; ++i) {
        const float x = 0.5f * std::sin(2.0f * cgv_dsp::PI * frequency
                                        * static_cast<float>(i) / kSampleRate);
        float l = 0.0f, r = 0.0f;
        fx.processSample(x, x, l, r);
        if (!std::isfinite(l) || !std::isfinite(r)) result.finite = false;
        result.peak = std::max(result.peak, std::max(std::abs(l), std::abs(r)));
        if (i >= warmup) result.samples.push_back(l);
    }

    for (size_t i = 0; i < kGuardFloats; ++i) {
        if (memory[memoryFor() + i] != kGuardValue)
            throw std::runtime_error("granular read/write crossed the ring boundary");
    }

    result.dominantHz = dominantFrequency(result.samples, kSampleRate);
    return result;
}

// ---------------------------------------------------------------------------
// Legacy reference: the old read-position lerp, evaluated on a pure tone.
//
//   readPos = lerp(lerp(tap, anchor + scanOffset, grainScan),
//                  anchor - scanOffset, reverseMix)
//
// Velocity = 1 - 2 * reverseMix (roughly), independent of grainScan, which is
// exactly why intermediate reverseMix values detuned the grains.
// ---------------------------------------------------------------------------
class LegacyReadPositionModel {
public:
    LegacyReadPositionModel(float sampleRate, float frequency)
        : sampleRate_(sampleRate),
          w_(2.0f * cgv_dsp::PI * frequency / sampleRate) {}

    double renderDominant(float reverseMix) const {
        const int kGrains = 6;
        const float grainMs = 180.0f;
        const float grainFrames = grainMs * 0.001f * sampleRate_;
        const float increment = 1.0f / grainFrames;
        const float grainScan = 1.0f;

        std::vector<float> phase(kGrains), anchor(kGrains), offset(kGrains);
        for (int g = 0; g < kGrains; ++g) {
            phase[g] = static_cast<float>(g) / static_cast<float>(kGrains);
            offset[g] = (4.0f + 5.0f * static_cast<float>(g)) * 0.001f * sampleRate_;
        }

        float writePos = static_cast<float>(grainFrames);
        const size_t warmup = 8192;
        const size_t captured = 65536;
        std::vector<float> out;
        out.reserve(captured);

        for (size_t n = 0; n < warmup + captured; ++n) {
            writePos += 1.0f;
            float acc = 0.0f;
            for (int g = 0; g < kGrains; ++g) {
                float p = phase[g] + increment;
                if (p >= 1.0f) {
                    p -= 1.0f;
                    anchor[g] = writePos - offset[g];
                }
                phase[g] = p;

                const float scanOffset = p * grainFrames;
                const float tap = writePos - offset[g];
                const float forwardPos = cgv_dsp::lerp(tap, anchor[g] + scanOffset, grainScan);
                const float reversePos = anchor[g] - scanOffset;
                const float readPos = cgv_dsp::lerp(forwardPos, reversePos, reverseMix);
                const float window = 4.0f * p * (1.0f - p);
                acc += sampleAt(readPos) * window;
            }
            if (n >= warmup) out.push_back(acc / static_cast<float>(kGrains));
        }
        return dominantFrequency(out, sampleRate_);
    }

private:
    float sampleAt(float pos) const {
        size_t i = static_cast<size_t>(pos);
        const float frac = pos - static_cast<float>(i);
        const float a = std::sin(w_ * static_cast<float>(i));
        const float b = std::sin(w_ * static_cast<float>(i + 1));
        return cgv_dsp::lerp(a, b, frac);
    }

    float sampleRate_;
    float w_;
};

bool closeRatio(double ratio, double target, double tolerance) {
    return std::abs(ratio - target) <= tolerance;
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    const float mixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const float tones[] = {440.0f, 1000.0f};

    std::cout << "=== New architecture: nominal pitch must stay constant ===\n";
    for (float tone : tones) {
        std::cout << "input=" << tone << " Hz, grainScan=1.0, texture=0.5\n";
        for (float mix : mixes) {
            const ToneResult r = renderGranularTone(tone, mix, 1.0f, 0.5f,
                                                    CloudGreyVerb::Preset::SmallCloudRoom, false);
            const double ratio = r.dominantHz / tone;
            std::cout << "  reverseMix=" << mix << " dominant=" << r.dominantHz
                      << " Hz ratio=" << ratio << " peak=" << r.peak << '\n';
            check(r.finite, "granular output must stay finite");
            check(r.peak < 4.0f, "granular output must stay bounded");
            // Grains carry a small intentional per-grain rate variation, so allow
            // normal granular/windowing spread, but reject any systematic detune.
            check(closeRatio(ratio, 1.0, 0.15),
                  "reverseMix must not detune the nominal granular pitch");
            // The legacy bug collapsed to a stationary head at 0.5; make sure the
            // new crossfade keeps real tonal energy there.
            check(r.dominantHz > 0.4 * static_cast<double>(tone),
                  "reverseMix=0.5 must not produce a stationary/DC grain");
        }
    }

    std::cout << "\n=== Orthogonality with grainScan (440 Hz) ===\n";
    for (float scan : {0.0f, 0.5f, 1.0f}) {
        for (float mix : mixes) {
            const ToneResult r = renderGranularTone(440.0f, mix, scan, 0.5f,
                                                    CloudGreyVerb::Preset::SmallCloudRoom, false);
            const double ratio = r.dominantHz / 440.0;
            std::cout << "  grainScan=" << scan << " reverseMix=" << mix
                      << " dominant=" << r.dominantHz << " Hz ratio=" << ratio << '\n';
            check(closeRatio(ratio, 1.0, 0.15),
                  "grainScan and reverseMix must be orthogonal for pitch");
        }
    }

    std::cout << "\n=== Legacy read-position lerp (bug demonstration) ===\n";
    {
        const LegacyReadPositionModel legacy(kSampleRate, 440.0f);
        const float legacyMixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
        for (float mix : legacyMixes) {
            const double hz = legacy.renderDominant(mix);
            const double ratio = hz / 440.0;
            std::cout << "  reverseMix=" << mix << " dominant=" << hz
                      << " Hz ratio=" << ratio << '\n';
        }

        const double half = legacy.renderDominant(0.25f) / 440.0;
        const double stationary = legacy.renderDominant(0.5f) / 440.0;
        const double revHalf = legacy.renderDominant(0.75f) / 440.0;
        check(closeRatio(half, 0.5, 0.12),
              "legacy reverseMix=0.25 must be ~half-speed");
        check(stationary < 0.25,
              "legacy reverseMix=0.50 must be a stationary/no-pitch read head");
        check(closeRatio(revHalf, 0.5, 0.12),
              "legacy reverseMix=0.75 must be ~reverse half-speed");
    }

    std::cout << "\n=== ReverseSmear preset (reverseMix=1, grainScan=1) ===\n";
    {
        const ToneResult r = renderGranularTone(440.0f, 1.0f, 1.0f, 0.6f,
                                                CloudGreyVerb::Preset::ReverseSmear, true);
        const double ratio = r.dominantHz / 440.0;
        std::cout << "  dominant=" << r.dominantHz << " Hz ratio=" << ratio
                  << " peak=" << r.peak << '\n';
        check(r.finite, "ReverseSmear must stay finite");
        check(closeRatio(ratio, 1.0, 0.15),
              "ReverseSmear must keep the nominal pitch while reversing");
    }

    std::cout << "\n=== BrightCloud / texture extremes / freeze / RT safety ===\n";
    {
        // Sweep reverseMix continuously through every value; output must stay
        // finite, bounded and must never touch the guard region.
        std::vector<float> memory(memoryFor() + kGuardFloats, 0.0f);
        for (size_t i = 0; i < kGuardFloats; ++i)
            memory[memoryFor() + i] = kGuardValue;
        CloudGreyVerb fx;
        fx.init(kSampleRate, memory.data(), memoryFor());
        check(fx.isInitialized(), "BrightCloud harness must initialize");

        auto p = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BrightCloud);
        p.mix = 1.0f;
        p.feedback = 0.2f;
        p.texture = 0.9f;   // long grains
        p.freeze = 0.0f;
        p.clipOutput = false;
        fx.setParams(p);
        fx.reset();

        bool finite = true;
        float peak = 0.0f;
        float maxStep = 0.0f;
        float previous = 0.0f;
        for (int i = 0; i < 48000 * 3; ++i) {
            const float sweep = 0.5f + 0.5f * std::sin(2.0f * cgv_dsp::PI * 1.5f
                                                       * static_cast<float>(i) / kSampleRate);
            p.reverseMix = sweep;
            fx.setParams(p);
            float l = 0.0f, r = 0.0f;
            const float x = 0.3f * std::sin(2.0f * cgv_dsp::PI * 440.0f
                                            * static_cast<float>(i) / kSampleRate);
            fx.processSample(x, x, l, r);
            if (!std::isfinite(l) || !std::isfinite(r)) finite = false;
            peak = std::max(peak, std::abs(l));
            if (i > 0) maxStep = std::max(maxStep, std::abs(l - previous));
            previous = l;
        }
        std::cout << "  continuous sweep: peak=" << peak << " maxStep=" << maxStep << '\n';
        check(finite, "continuous reverseMix sweep must stay finite");
        check(peak < 4.0f, "continuous reverseMix sweep must stay bounded");
        // A crossfade is continuous in reverseMix; no step should resemble a
        // hard sample drop/clip even while sweeping full range at 1.5 Hz.
        check(maxStep < 0.75f, "reverseMix changes must not introduce hard clicks");

        // Short grains + hard freeze exercise the tightest circular-boundary
        // conditions with both heads live.
        for (float texture : {0.0f, 0.1f, 0.5f, 1.0f}) {
            for (float mix : mixes) {
                ToneResult r = renderGranularTone(440.0f, mix, 1.0f, texture,
                                                  CloudGreyVerb::Preset::SmallCloudRoom, false);
                check(r.finite, "texture extreme must stay finite");
                check(r.peak < 4.0f, "texture extreme must stay bounded");
            }
        }

        ToneResult longGrains = renderGranularTone(440.0f, 0.5f, 1.0f, 1.0f,
                                                   CloudGreyVerb::Preset::SmallCloudRoom, false);
        check(longGrains.finite, "texture=1.0 must stay finite (ring boundary)");

        // Freeze keeps the frozen history live on both heads without running off
        // the ring; pitch must remain nominal even while frozen.
        ToneResult frozen = renderGranularTone(440.0f, 0.5f, 1.0f, 0.5f,
                                               CloudGreyVerb::Preset::SmallCloudRoom, false, 1.0f);
        const double frozenRatio = frozen.dominantHz / 440.0;
        std::cout << "  freeze=1.0 reverseMix=0.5 dominant=" << frozen.dominantHz
                  << " Hz ratio=" << frozenRatio << '\n';
        check(frozen.finite, "freeze must stay finite");
        check(closeRatio(frozenRatio, 1.0, 0.15),
              "freeze must keep the nominal pitch while crossfading direction");

        // Stereo image: an identical crossfade gain is applied to L and R, so
        // the image must survive (the two granular channels stay distinct and
        // finite) instead of collapsing.
        {
            std::vector<float> mem(memoryFor() + kGuardFloats, 0.0f);
            CloudGreyVerb image;
            image.init(kSampleRate, mem.data(), memoryFor());
            auto ip = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::SmallCloudRoom);
            ip.mix = 1.0f;
            ip.stereoCore = true;
            ip.reverseMix = 0.5f;
            ip.clipOutput = false;
            image.setParams(ip);
            image.reset();
            double diffEnergy = 0.0;
            bool imageFinite = true;
            for (int i = 0; i < 48000; ++i) {
                const float x = 0.4f * std::sin(2.0f * cgv_dsp::PI * 440.0f
                                                * static_cast<float>(i) / kSampleRate);
                float l = 0.0f, r = 0.0f;
                image.processSample(x, x, l, r);
                if (!std::isfinite(l) || !std::isfinite(r)) imageFinite = false;
                diffEnergy += static_cast<double>(l - r) * (l - r);
            }
            std::cout << "  stereo L-R energy=" << diffEnergy << '\n';
            check(imageFinite, "stereo granular output must stay finite");
            check(diffEnergy > 1.0e-4, "reverseMix crossfade must preserve the stereo image");
        }

        for (size_t i = 0; i < kGuardFloats; ++i)
            check(memory[memoryFor() + i] == kGuardValue,
                  "no granular read/write may leave the ring buffer");
    }

    if (failures != 0) {
        std::cerr << "\n" << failures << " check(s) failed.\n";
        return 1;
    }
    std::cout << "\nSUCCESS: reverseMix preserves nominal pitch and grainScan is orthogonal.\n";
    return 0;
}
