#include "cloud_grey_verb.hpp"
#include <array>

namespace {

static_assert(CGV_FDN_ORDER == 2 || CGV_FDN_ORDER == 4,
              "CloudGreyVerb supports only 2x2 or 4x4 feedback networks");

inline void encodeStereoForFdn(float left, float right, float output[CGV_FDN_ORDER]) {
#if CGV_FDN_ORDER == 4
    // Two orthonormal stereo injection vectors spanning all four delay lines.
    output[0] = 0.5f * (left + right);
    output[1] = 0.5f * (left - right);
    output[2] = 0.5f * (left + right);
    output[3] = 0.5f * (-left + right);
#else
    output[0] = left;
    output[1] = right;
#endif
}

inline void decodeStereoFromFdn(const float input[CGV_FDN_ORDER], float& left, float& right) {
#if CGV_FDN_ORDER == 4
    // Transpose of the injection basis, preserving stereo energy.
    left  = 0.5f * (input[0] + input[1] + input[2] - input[3]);
    right = 0.5f * (input[0] - input[1] + input[2] + input[3]);
#else
    left = input[0];
    right = input[1];
#endif
}

inline void mixFdnFeedback(const float input[CGV_FDN_ORDER], float output[CGV_FDN_ORDER]) {
#if CGV_FDN_ORDER == 4
    cgv_dsp::applyNormalizedHadamard4(input, output);
#else
    // Orthogonal 2x2 fallback matching the former cross-feedback topology.
    output[0] = input[1];
    output[1] = input[0];
#endif
}

} // namespace

namespace {
using FP = CloudGreyVerb::FactoryPreset;
using P = CloudGreyVerb::Params;
const std::array<FP, 10>& factoryPresets() {
    static const std::array<FP, 10> presets = {{
        [] { P p; p.mix=.40f;p.texture=.32f;p.feedback=.44f;p.size=.35f;p.diffusion=.66f;p.modDepth=.05f;p.modRate=.12f;p.damping=.52f;p.lowDamping=.48f;p.tone=.56f;p.outputGain=.96f; return FP{"SmallCloudRoom",p}; }(),
        // Restore useful low-mid tail body without changing the wide-but-Mid-led
        // character. Output compensation keeps preset changes manageable.
        [] { P p; p.mix=.36f;p.texture=.48f;p.feedback=.58f;p.size=.56f;p.diffusion=.60f;p.modDepth=.10f;p.modRate=.12f;p.damping=.68f;p.lowDamping=.63f;p.tone=.44f;p.inputGain=.90f;p.outputGain=.92f;p.preDelay=.10f;p.stereoWidth=1.5f; return FP{"BassAmbientWash",p}; }(),
        [] { P p; p.mix=.70f;p.texture=.85f;p.freeze=1.f;p.feedback=.65f;p.size=.70f;p.diffusion=.80f;p.modDepth=.40f;p.modRate=.05f;p.damping=.40f;p.lowDamping=.60f;p.tone=.45f;p.stereoWidth=1.2f; return FP{"FrozenOrganPad",p}; }(),
        // A 36 ms pre-delay retains Greyhole depth while reducing the perceived gap.
        [] { P p; p.mix=.60f;p.texture=.58f;p.feedback=.75f;p.size=.76f;p.sizeScale=3.f;p.diffusion=.72f;p.modDepth=.36f;p.modRate=.22f;p.damping=.62f;p.lowDamping=.58f;p.tone=.52f;p.outputGain=.92f;p.preDelay=.18f; return FP{"GreyholeDelayVerb",p}; }(),
        // Slightly more high-frequency survival differentiates cinematic darkness
        // from a simply muffled tail; output trim avoids using level as brightness.
        [] { P p; p.mix=.55f;p.texture=.75f;p.feedback=.75f;p.size=.84f;p.sizeScale=3.5f;p.diffusion=.70f;p.modDepth=.26f;p.modRate=.08f;p.damping=.35f;p.lowDamping=.60f;p.tone=.35f;p.inputGain=.76f;p.outputGain=.78f;p.preDelay=.30f; return FP{"DarkLongCloud",p}; }(),
        [] { P p; p.mix=.50f;p.texture=.05f;p.feedback=.50f;p.size=.25f;p.diffusion=.20f;p.modDepth=.90f;p.modRate=.80f;p.damping=.50f;p.tone=.50f; return FP{"GlitchSmear",p}; }(),
        [] { P p; p.mix=.25f;p.texture=.20f;p.feedback=.28f;p.size=.20f;p.diffusion=.46f;p.modDepth=.02f;p.modRate=.10f;p.damping=.50f;p.lowDamping=.52f;p.tone=.50f;p.outputGain=.98f;p.preDelay=.05f;p.stereoWidth=.8f; return FP{"AlwaysOnSubtle",p}; }(),
        // Air comes from a clear, diffuse tail rather than a hard tilt boost.
        [] { P p; p.mix=.50f;p.texture=.60f;p.feedback=.70f;p.size=.60f;p.diffusion=.72f;p.modDepth=.30f;p.modRate=.28f;p.damping=.64f;p.lowDamping=.56f;p.tone=.68f;p.outputGain=.96f;p.preDelay=.10f;p.stereoWidth=1.2f; return FP{"BrightCloud",p}; }(),
        [] { P p; p.mix=.55f;p.texture=.58f;p.feedback=.60f;p.size=.62f;p.diffusion=.72f;p.modDepth=.16f;p.modRate=.10f;p.damping=.52f;p.lowDamping=.56f;p.tone=.58f;p.shimmer=.20f;p.inputGain=.82f;p.outputGain=.90f;p.preDelay=.15f;p.stereoWidth=1.4f; return FP{"ShimmerCloud",p,true}; }(),
        [] { P p; p.mix=.65f;p.texture=.60f;p.feedback=.70f;p.size=.50f;p.diffusion=.60f;p.modDepth=.40f;p.modRate=.20f;p.damping=.60f;p.lowDamping=.50f;p.tone=.50f;p.stereoWidth=1.2f;p.reverseMix=1.f;p.grainScan=1.f; return FP{"ReverseSmear",p}; }()
    }};
    return presets;
}
static_assert(10 == static_cast<size_t>(CloudGreyVerb::Preset::Count),
              "Preset enum and factory catalogue order/count are a shared contract");
}

size_t CloudGreyVerb::factoryPresetCount() { return factoryPresets().size(); }
const CloudGreyVerb::FactoryPreset& CloudGreyVerb::getFactoryPreset(size_t index) {
    const auto& presets = factoryPresets();
    // This function is also called by the no-exception WebAssembly build.
    // Keep invalid catalogue access deterministic and free of libc++ abort paths.
    if (index >= presets.size())
        index = 0;
    return presets[index];
}
const CloudGreyVerb::FactoryPreset& CloudGreyVerb::getFactoryPreset(Preset preset) {
    return getFactoryPreset(static_cast<size_t>(preset));
}
CloudGreyVerb::Params CloudGreyVerb::getPreset(Preset preset) { return getFactoryPreset(preset).dsp; }

float CloudGreyVerb::sizeToSeconds(float normalized, float scale) {
    normalized = fmaxf(0.0f, fminf(1.0f, normalized));
    scale = fmaxf(1.0f, fminf(kSizeMaxExtendedSeconds / kSizeMaxNormalSeconds, scale));
    const float maximum = fminf(kSizeMaxExtendedSeconds, kSizeMaxNormalSeconds * scale);
    // Exponential mapping gives useful resolution in rooms while retaining a
    // musically gradual route to long delays.
    return kSizeMinSeconds * powf(maximum / kSizeMinSeconds, normalized);
}

float CloudGreyVerb::secondsToSize(float seconds, float scale) {
    scale = fmaxf(1.0f, fminf(kSizeMaxExtendedSeconds / kSizeMaxNormalSeconds, scale));
    const float maximum = fminf(kSizeMaxExtendedSeconds, kSizeMaxNormalSeconds * scale);
    seconds = fmaxf(kSizeMinSeconds, fminf(maximum, seconds));
    return logf(seconds / kSizeMinSeconds) / logf(maximum / kSizeMinSeconds);
}

float CloudGreyVerb::earlyMaxRequestedSeconds() {
    // Capacity follows the active prefix of kEarlyTaps exactly.  Do not add
    // acoustic timing literals here: the specification above is authoritative.
    return earlyMaxTapSeconds() * kEarlyMaxTimeScale;
}

size_t CloudGreyVerb::earlyDelayCapacityFrames(float sampleRate) {
    constexpr size_t kHermiteGuardFrames = 3;
    return static_cast<size_t>(ceilf(earlyMaxRequestedSeconds() * sampleRate))
           + kHermiteGuardFrames;
}

size_t CloudGreyVerb::requiredMemoryFloats(float sampleRate) {
    if (sampleRate <= 0.0f) return 0;
    const auto frames = [sampleRate](float seconds) {
        return static_cast<size_t>(ceilf(seconds * sampleRate)) + 4u;
    };
    const size_t granulSize = frames(0.500f);
    size_t diffuserL[4] = {};
    constexpr float diffuserSeconds[4] = {0.007f, 0.011f, 0.017f, 0.029f};
    diffuserL[0] = cgv_dsp::nextPrime(frames(diffuserSeconds[0])) + 1;
    diffuserL[1] = cgv_dsp::nextPrime(frames(diffuserSeconds[1])) + 1;
#if CGV_NUM_ALLPASS > 2
    diffuserL[2] = cgv_dsp::nextPrime(frames(diffuserSeconds[2])) + 1;
    diffuserL[3] = cgv_dsp::nextPrime(frames(diffuserSeconds[3])) + 1;
#endif
    size_t result = 2 * granulSize + 2 * (frames(kPreDelayCapacitySeconds) + 4)
                  + 2 * earlyDelayCapacityFrames(sampleRate);
    for (int i = 0; i < CGV_NUM_ALLPASS; ++i)
        result += diffuserL[i] + cgv_dsp::nextPrime(diffuserL[i] + 5);
#if CGV_NUM_LOOP_ALLPASS > 0
    constexpr float loopSeconds[4] = {0.0047f, 0.0059f, 0.0073f, 0.0091f};
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        result += cgv_dsp::nextPrime(frames(loopSeconds[i])) + 1;
#endif
#if CGV_ENABLE_SHIMMER
    result += frames(0.064f);
#endif
    return result + frames(kSizeMaxExtendedSeconds + 0.020f) * CGV_FDN_ORDER;
}

#if CGV_ENABLE_SHIMMER
bool ShimmerPitcher::init(float sampleRate, float* buffer, uint32_t bufferSize) {
    if (!buffer || bufferSize == 0 || sampleRate <= 0.0f) return false;
    sampleRate_ = sampleRate;
    buffer_ = buffer;
    size_ = bufferSize;
    
    minDelaySamples_ = (8.0f / 1000.0f) * sampleRate_;
    depthSamples_ = (42.0f / 1000.0f) * sampleRate_;
    
    if (minDelaySamples_ + depthSamples_ + 2.0f > size_) {
        depthSamples_ = size_ - minDelaySamples_ - 2.0f;
    }
    
    if (depthSamples_ < 10.0f) return false;
    
    phaseIncSmoother_.clear();
    phaseIncSmoother_.setFreq(5.0f, sampleRate_);
    
    setRatio(2.0f); // Default to one octave up
    phaseInc_ = targetPhaseInc_;
    phaseIncSmoother_.setValue(targetPhaseInc_); // Snap to target
    
    reset();
    return true;
}

void ShimmerPitcher::setRatio(float ratio) {
    if (depthSamples_ > 0.0f) {
        targetPhaseInc_ = (ratio - 1.0f) / depthSamples_;
    }
}

void ShimmerPitcher::reset() {
    if (buffer_) {
        for (uint32_t i=0; i<size_; ++i) buffer_[i] = 0.0f;
    }
    writePos_ = 0;
    phaseA_ = 0.0f;
    phaseB_ = 0.5f;
    
    phaseIncSmoother_.setValue(targetPhaseInc_);
    phaseInc_ = targetPhaseInc_;
}

float ShimmerPitcher::readDelay(float delaySamples) const {
    float readPos = static_cast<float>(writePos_) - delaySamples;
    float fSize = static_cast<float>(size_);
    
    // Fast path & safe wrap without expensive while loops
    if (readPos < 0.0f) {
        if (readPos > -fSize) {
            readPos += fSize;
        } else {
            readPos -= fSize * floorf(readPos / fSize);
        }
    } else if (readPos >= fSize) {
        if (readPos < 2.0f * fSize) {
            readPos -= fSize;
        } else {
            readPos -= fSize * floorf(readPos / fSize);
        }
    }
    
    uint32_t idx1 = static_cast<uint32_t>(readPos);
    if (idx1 >= size_) idx1 = 0; // Safeguard
    
    uint32_t idx2 = idx1 + 1;
    if (idx2 >= size_) idx2 = 0;
    
    float frac = readPos - static_cast<float>(idx1);
    
    return cgv_dsp::lerp(buffer_[idx1], buffer_[idx2], frac);
}

float ShimmerPitcher::process(float input) {
    if (!buffer_ || size_ == 0) return 0.0f;
    
    cgv_dsp::sanitize(input);
    buffer_[writePos_] = input;
    writePos_ = (writePos_ + 1) % size_;
    
    float delayA = minDelaySamples_ + depthSamples_ * (1.0f - phaseA_);
    float delayB = minDelaySamples_ + depthSamples_ * (1.0f - phaseB_);
    
    float windowA = 4.0f * phaseA_ * (1.0f - phaseA_);
    float windowB = 4.0f * phaseB_ * (1.0f - phaseB_);
    
    float outA = readDelay(delayA);
    float outB = readDelay(delayB);
    
    float out = outA * windowA + outB * windowB;
    float norm = windowA + windowB;
    if (norm > 0.001f) out /= norm;
    
    out = cgv_dsp::softClip(out);
    
    phaseInc_ = phaseIncSmoother_.process(targetPhaseInc_);
    
    phaseA_ += phaseInc_;
    if (phaseA_ >= 1.0f) phaseA_ -= 1.0f;
    if (phaseA_ < 0.0f) phaseA_ += 1.0f;
    
    phaseB_ += phaseInc_;
    if (phaseB_ >= 1.0f) phaseB_ -= 1.0f;
    if (phaseB_ < 0.0f) phaseB_ += 1.0f;
    
    return out;
}

void ShimmerPitcher::processStereo(float input, float& outL, float& outR) {
    if (!buffer_ || size_ == 0) { outL = 0; outR = 0; return; }
    
    cgv_dsp::sanitize(input);
    buffer_[writePos_] = input;
    writePos_ = (writePos_ + 1) % size_;
    
    float delayA = minDelaySamples_ + depthSamples_ * (1.0f - phaseA_);
    float delayB = minDelaySamples_ + depthSamples_ * (1.0f - phaseB_);
    
    float windowA = 4.0f * phaseA_ * (1.0f - phaseA_);
    float windowB = 4.0f * phaseB_ * (1.0f - phaseB_);
    
    outL = readDelay(delayA) * windowA + readDelay(delayB) * windowB;
    
    float offsetR = sampleRate_ * 0.007f; // 7ms delay for Right channel width (mono safer)
    outR = readDelay(delayA + offsetR) * windowA + readDelay(delayB + offsetR) * windowB;
    
    float norm = windowA + windowB;
    if (norm > 0.001f) {
        outL /= norm;
        outR /= norm;
    }
    
    outL = cgv_dsp::softClip(outL);
    outR = cgv_dsp::softClip(outR);
    
    phaseInc_ = phaseIncSmoother_.process(targetPhaseInc_);
    
    phaseA_ += phaseInc_;
    if (phaseA_ >= 1.0f) phaseA_ -= 1.0f;
    if (phaseA_ < 0.0f) phaseA_ += 1.0f;
    
    phaseB_ += phaseInc_;
    if (phaseB_ >= 1.0f) phaseB_ -= 1.0f;
    if (phaseB_ < 0.0f) phaseB_ += 1.0f;
}
#endif

void CloudGreyVerb::init(float sampleRate, float* externalBuffer, size_t bufferSize) {
    initialized_ = false;
    mainDelaySize_ = 0;
#if CGV_ENABLE_SHIMMER
    shimmerAvailable_ = false;
#endif
    
    if (!externalBuffer || sampleRate <= 0.0f) return;

    sampleRate_ = sampleRate;

    // Acoustic capacities are sample-rate-derived. bufferSize is only a hard
    // capacity ceiling: extra RAM can no longer lengthen the reverb.
    auto frames = [this](float seconds) -> size_t {
        return static_cast<size_t>(ceilf(seconds * sampleRate_)) + 4u;
    };
    const size_t granulSize = frames(0.500f); // covers the 400 ms grain window + interpolation
    size_t diffuserLSizes[4] = {0};
    size_t diffuserRSizes[4] = {0};
    constexpr float kDiffuserSeconds[4] = {0.007f, 0.011f, 0.017f, 0.029f};
    diffuserLSizes[0] = cgv_dsp::nextPrime(frames(kDiffuserSeconds[0])) + 1;
    diffuserLSizes[1] = cgv_dsp::nextPrime(frames(kDiffuserSeconds[1])) + 1;
#if CGV_NUM_ALLPASS > 2
    diffuserLSizes[2] = cgv_dsp::nextPrime(frames(kDiffuserSeconds[2])) + 1;
    diffuserLSizes[3] = cgv_dsp::nextPrime(frames(kDiffuserSeconds[3])) + 1;
#endif
    for (int i = 0; i < CGV_NUM_ALLPASS; ++i)
        diffuserRSizes[i] = cgv_dsp::nextPrime(diffuserLSizes[i] + 5);

#if CGV_NUM_LOOP_ALLPASS > 0
    size_t fdnAllpassSizes[CGV_FDN_ORDER] = {0};
    constexpr float kFdnAllpassSeconds[4] = {0.0047f, 0.0059f, 0.0073f, 0.0091f};
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fdnAllpassSizes[i] = cgv_dsp::nextPrime(frames(kFdnAllpassSeconds[i])) + 1;
#endif

#if CGV_ENABLE_SHIMMER
    size_t shimmerSize = frames(0.064f); // 8 ms minimum + 42 ms sweep + 7 ms stereo offset
#else
    size_t shimmerSize = 0;
#endif
    
    // Manual control stays 0..200 ms. Sync requests are clamped by this
    // build profile's documented history capacity (only Desktop reaches 8 s).
    // The guard preserves the interpolation neighbourhood at the longest read.
    size_t predelaySize = frames(kPreDelayCapacitySeconds) + 4;

    // One history per channel serves all feed-forward reflections.  Capacity
    // derives from the actual longest acoustic request, not a hand-tuned
    // nominal number.  The guard keeps Hermite's p1/p2/p3 neighbours inside
    // retained history even at the largest size setting.
    const size_t earlyDelaySize = earlyDelayCapacityFrames(sampleRate_);
    
    size_t fixedSize = 2 * granulSize + shimmerSize + 2 * predelaySize;
    fixedSize += 2 * earlyDelaySize;
    for (int i = 0; i < CGV_NUM_ALLPASS; ++i)
        fixedSize += diffuserLSizes[i] + diffuserRSizes[i];
#if CGV_NUM_LOOP_ALLPASS > 0
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fixedSize += fdnAllpassSizes[i];
#endif

    const size_t nominalMainDelaySize = frames(kSizeMaxExtendedSeconds + 0.020f);
    const size_t requiredSize = fixedSize + nominalMainDelaySize * CGV_FDN_ORDER;
    if (bufferSize < requiredSize)
        return;
    mainDelaySize_ = nominalMainDelaySize;

    // Atribuição sequencial s/ alocação
    float* ptr = externalBuffer;

    preDelayL_.init(ptr, predelaySize); ptr += predelaySize;
    preDelayR_.init(ptr, predelaySize); ptr += predelaySize;

    earlyDelayL_.init(ptr, earlyDelaySize); ptr += earlyDelaySize;
    earlyDelayR_.init(ptr, earlyDelaySize); ptr += earlyDelaySize;

    grainMemoryL_ = ptr; ptr += granulSize;
    grainMemoryR_ = ptr; ptr += granulSize;
    grainMemorySize_ = granulSize;

    for (int i = 0; i < CGV_NUM_ALLPASS; ++i) {
        diffuserApL_[i].init(ptr, diffuserLSizes[i]); ptr += diffuserLSizes[i];
        diffuserApR_[i].init(ptr, diffuserRSizes[i]); ptr += diffuserRSizes[i];
    }
    
#if CGV_NUM_LOOP_ALLPASS > 0
    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        fdnLoopAp_[i].init(ptr, fdnAllpassSizes[i]);
        ptr += fdnAllpassSizes[i];
    }
#endif

#if CGV_ENABLE_SHIMMER
    shimmerAvailable_ = shimmer_.init(sampleRate_, ptr, static_cast<uint32_t>(shimmerSize));
    ptr += shimmerSize;
#endif

    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        fdnDelay_[i].init(ptr, mainDelaySize_);
        ptr += mainDelaySize_;
    }

    // LFO Init
    lfo1_.setRate(0.5f, sampleRate_);
    lfo2_.setRate(0.5f, sampleRate_); // Forçaremos diferença de fase lendo desfasado ou drift
    spinLfo_.setRate(1.1f, sampleRate_); // Spin LFO for micro-modulation

    freezeSmoothingCoeff_ = cgv_dsp::timeConstantCoefficient(0.0042f, sampleRate_);
    preDelaySmoothingCoeff_ = cgv_dsp::timeConstantCoefficient(0.0042f, sampleRate_);
    duckAttackCoeff_ = cgv_dsp::timeConstantCoefficient(0.00041f, sampleRate_);
    duckReleaseCoeff_ = cgv_dsp::timeConstantCoefficient(0.104f, sampleRate_);
    energyCoeff_ = cgv_dsp::timeConstantCoefficient(0.0417f, sampleRate_);
    safetyAttackCoeff_ = cgv_dsp::timeConstantCoefficient(0.00069f, sampleRate_);
    safetyReleaseCoeff_ = cgv_dsp::timeConstantCoefficient(0.0208f, sampleRate_);
    driftLCoeff_ = cgv_dsp::timeConstantCoefficient(0.417f, sampleRate_);
    driftRCoeff_ = cgv_dsp::timeConstantCoefficient(0.521f, sampleRate_);
    
    initialized_ = true;
    reset();
}

void CloudGreyVerb::reset() {
    grainWritePos_ = 0;
    grainPhase_ = 0.0f;
    freezeSmoothed_ = 0.0f;
    prng_.seed(1234567);
    modulationPrng_.seed(7654321);
    modDriftL_ = modDriftR_ = 0.0f;
    modTargetL_ = modTargetR_ = 0.0f;
    modRandomPhase_ = 0.0f;
    
    for (int i=0; i<CGV_NUM_GRAINS; ++i) {
        grainJitter_[i] = 0.0f;
        grainPan_[i] = prng_.randFloat();
        grainOffsetMs_[i] = 5.0f + prng_.randFloat() * 35.0f;
        grainAnchorPos_[i] = 0.0f;
    }
    
    if (grainMemoryL_) {
        for (size_t i = 0; i < grainMemorySize_; ++i) {
            grainMemoryL_[i] = 0.0f;
            grainMemoryR_[i] = 0.0f;
        }
    }
    grainWritePos_ = 0;

    for (int i = 0; i < CGV_NUM_ALLPASS; ++i) {
        diffuserApL_[i].clear();
        diffuserApR_[i].clear();
    }
    
    preDelayL_.clear();
    preDelayR_.clear();
    earlyDelayL_.clear();
    earlyDelayR_.clear();
    // A reset is also the boundary used by preset changes.  Snap to the
    // already-selected target so a factory program's advertised pre-delay is
    // present from its first wet sample; only live parameter edits glide.
    const float resetPreDelaySeconds = params_.preDelaySeconds >= 0.0f
        ? params_.preDelaySeconds : params_.preDelay * kManualPreDelayMaximumSeconds;
    preDelaySmoothed_ = fmaxf(1.0f, fminf(kPreDelayCapacitySeconds * sampleRate_, resetPreDelaySeconds * sampleRate_));
    preDelayTargetFrames_ = preDelaySmoothed_;
    preDelayPreviousFrames_ = preDelaySmoothed_;
    preDelayCrossfadeSamplesRemaining_ = 0;
    preDelayCrossfadeSamplesTotal_ = 0;
#if CGV_NUM_LOOP_ALLPASS > 0
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fdnLoopAp_[i].clear();
#endif
    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        fdnDelay_[i].clear();
        fdnDamp_[i].clear();
        fdnHighPassState_[i].clear();
    }
    lfo1_.clear(); lfo2_.clear(); spinLfo_.clear();
    toneL_.clear(); toneR_.clear();
    duckingEnvState_ = 0.0f;
    loopEnergy_ = 0.0f;
    lastSafetyGain_ = 1.0f;
    
    float smoothHz = 15.0f;
    smoothSize_.clear(); smoothSize_.setFreq(smoothHz, sampleRate_); smoothSize_.setValue(params_.size);
    smoothFeedback_.clear(); smoothFeedback_.setFreq(smoothHz, sampleRate_); smoothFeedback_.setValue(params_.feedback);
    smoothDiffusion_.clear(); smoothDiffusion_.setFreq(smoothHz, sampleRate_); smoothDiffusion_.setValue(params_.diffusion);
    smoothDamping_.clear(); smoothDamping_.setFreq(smoothHz, sampleRate_); smoothDamping_.setValue(params_.damping);
    smoothLowDamping_.clear(); smoothLowDamping_.setFreq(smoothHz, sampleRate_); smoothLowDamping_.setValue(params_.lowDamping);
    smoothTone_.clear(); smoothTone_.setFreq(smoothHz, sampleRate_); smoothTone_.setValue(params_.tone);
    
    lastSmoothedDamping_ = -1.0f;
    lastSmoothedLowDamping_ = -1.0f;
    lastSmoothedTone_ = -1.0f;
    lastModRate_ = -1.0f;
    lastDynamicLpFreq_ = -1.0f;

    // Tone: Tilt EQ Muscial fixo em 800Hz
    toneL_.setFreq(800.0f, sampleRate_); 
    toneR_.setFreq(800.0f, sampleRate_);


#if CGV_ENABLE_SHIMMER
    shimmerHp_.clear();
    shimmerLp_.clear();
    shimmerSmoother_.clear();
    if (shimmerAvailable_) {
        const float ratioMap[5] = {0.5f, 1.5f, 2.0f, 3.0f, 4.0f};
        shimmer_.setRatio(ratioMap[params_.shimmerRatioIndex]);
        shimmer_.reset();
    }
    shimmerSmoother_.setFreq(8.0f, sampleRate_); // 8 Hz smoothing for faster but smooth response
#endif
}

static inline float clampParam(float v, float minV, float maxV) {
    if (v != v) return minV; // NaN proteção!
    return (v < minV) ? minV : ((v > maxV) ? maxV : v);
}

void CloudGreyVerb::setParams(const Params& p) {
    params_ = p;
    
    // Clampar todos os parâmetros por segurança
    params_.mix = clampParam(params_.mix, 0.0f, 1.0f);
    params_.texture = clampParam(params_.texture, 0.0f, 1.0f);
    params_.freeze = clampParam(params_.freeze, 0.0f, 1.0f);
    params_.feedback = clampParam(params_.feedback, 0.0f, 0.94f); // Teto seguro
    params_.size = clampParam(params_.size, 0.0f, 1.0f);
    params_.diffusion = clampParam(params_.diffusion, 0.0f, 1.0f);
    params_.modDepth = clampParam(params_.modDepth, 0.0f, 1.0f);
    params_.modRate = clampParam(params_.modRate, 0.0f, 1.0f);
    params_.damping = clampParam(params_.damping, 0.0f, 1.0f);
    params_.tone = clampParam(params_.tone, 0.0f, 1.0f);
    params_.shimmer = clampParam(params_.shimmer, 0.0f, 1.0f);
    if (params_.shimmerRatioIndex < 0) params_.shimmerRatioIndex = 0;
    if (params_.shimmerRatioIndex > 4) params_.shimmerRatioIndex = 4;
    params_.inputGain = clampParam(params_.inputGain, 0.0f, 2.0f);
    params_.outputGain = clampParam(params_.outputGain, 0.0f, 2.0f);
    params_.preDelay = clampParam(params_.preDelay, 0.0f, 1.0f);
    params_.preDelaySeconds = clampParam(params_.preDelaySeconds, -1.0f, kPreDelayCapacitySeconds);
    params_.stereoWidth = clampParam(params_.stereoWidth, 0.0f, 2.0f);
    params_.lowDamping = clampParam(params_.lowDamping, 0.0f, 1.0f);
    params_.sizeScale = clampParam(params_.sizeScale, 1.0f,
        kSizeMaxExtendedSeconds / kSizeMaxNormalSeconds);
    
    // Pré-cálculo de Ganhos Mix (Equal-power approximation)
    float m = params_.mix;
    gainDry_ = sqrtf(1.0f - m);
    gainWet_ = sqrtf(m);

    const float requestedPreDelaySeconds = params_.preDelaySeconds >= 0.0f
        ? params_.preDelaySeconds : params_.preDelay * kManualPreDelayMaximumSeconds;
    const float requestedPreDelayFrames = fmaxf(1.0f, fminf(kPreDelayCapacitySeconds * sampleRate_, requestedPreDelaySeconds * sampleRate_));
    if (fabsf(requestedPreDelayFrames - preDelayTargetFrames_) > 0.5f) {
        // Large jumps use two stationary taps; moving a single read head over
        // seconds of history produces an obvious Doppler sweep.
        if (fabsf(requestedPreDelayFrames - preDelaySmoothed_) > sampleRate_ * 0.020f) {
            preDelayPreviousFrames_ = preDelaySmoothed_;
            preDelaySmoothed_ = requestedPreDelayFrames;
            preDelayCrossfadeSamplesTotal_ = static_cast<int>(fmaxf(1.0f, sampleRate_ * 0.020f));
            preDelayCrossfadeSamplesRemaining_ = preDelayCrossfadeSamplesTotal_;
        }
        preDelayTargetFrames_ = requestedPreDelayFrames;
    }

    // O resto será recalculado condicionalmente no processSample() para o smoothing


#if CGV_ENABLE_SHIMMER
    shimmerHp_.setFreq(300.0f, sampleRate_);
    
    // Dynamic lowpass quadratic curve: preserves brightness in low amounts, darkens in high amounts
    float shmSq = params_.shimmer * params_.shimmer;
    float shimmerLpFreq = 5500.0f - (shmSq * 1700.0f);
    if (shimmerLpFreq < 3800.0f) shimmerLpFreq = 3800.0f;
    shimmerLp_.setFreq(shimmerLpFreq, sampleRate_);
#endif
}

void CloudGreyVerb::processGranular(float inL, float inR, float lfoDrift, float& outL, float& outR) {
    // FREEZE Smoothed: Transição musical (Real buffer freeze misturado)
    freezeSmoothed_ = cgv_dsp::lerp(freezeSmoothed_, params_.freeze, freezeSmoothingCoeff_);

    float writeGain = 1.0f - freezeSmoothed_;
    writeGain *= writeGain; // curva quadrática: menos vazamento perto de freeze 1
    
    if (params_.hardFreeze) {
        writeGain = 0.0f;
    }

    float oldValL = grainMemoryL_[grainWritePos_];
    float oldValR = grainMemoryR_[grainWritePos_];
    
    grainMemoryL_[grainWritePos_] = inL * writeGain + oldValL * (1.0f - writeGain);
    grainMemoryR_[grainWritePos_] = inR * writeGain + oldValR * (1.0f - writeGain);
    
    grainWritePos_ = (grainWritePos_ + 1) % grainMemorySize_;

    // Texture: Varredura de tamanho e densidade de 15ms a 400ms
    float grainLenMs = cgv_dsp::lerp(15.0f, 400.0f, params_.texture);
    float phaseFramesTotal = (grainLenMs / 1000.0f) * sampleRate_;
    float fGrainMem = static_cast<float>(grainMemorySize_);
    if (phaseFramesTotal > fGrainMem - 100.0f) phaseFramesTotal = fGrainMem - 100.0f;
    if (phaseFramesTotal < 10.0f) phaseFramesTotal = 10.0f;
    
    float increment = 1.0f / phaseFramesTotal;
    
    // Freeze drift: move a base de leitura levemente para dar vida à nuvem congelada
    float driftMs = lfoDrift * params_.texture * 150.0f * freezeSmoothed_;
    
    grainPhase_ += increment;
    if (grainPhase_ >= 1.0f) grainPhase_ -= 1.0f;

    float accL = 0.0f;
    float accR = 0.0f;
    
    float grainPhaseSpan = 1.0f / static_cast<float>(CGV_NUM_GRAINS);

    // Grãos estéreo interpolados para uma nuvem difusa densa
    for(int i = 0; i < CGV_NUM_GRAINS; ++i) {
        float p = grainPhase_ + (float)i * grainPhaseSpan;
        if (p >= 1.0f) p -= 1.0f;

        // Atualiza Jitter de forma limpa apenas no recomeço individual do grão
        float oldP = p - increment;
        if (oldP < 0.0f) oldP += 1.0f;
        
        float fGranSize = static_cast<float>(grainMemorySize_);
        
        if (p < increment || p < oldP) {
            grainJitter_[i] = prng_.randFloat() * params_.texture * 45.0f; // Jitter máx 45ms
            grainPan_[i] = cgv_dsp::lerp(grainPan_[i], prng_.randFloat(), 0.25f);
            grainOffsetMs_[i] = cgv_dsp::lerp(grainOffsetMs_[i], 5.0f + prng_.randFloat() * 45.0f, 0.25f);
            
            float snapReadMs = grainOffsetMs_[i] + grainJitter_[i] + driftMs;
            float snapReadFrames = snapReadMs * (sampleRate_ / 1000.0f);
            snapReadFrames = fmodf(snapReadFrames, fGranSize - 4.0f);
            if (snapReadFrames < 2.0f) snapReadFrames = 2.0f;
            grainAnchorPos_[i] = static_cast<float>(grainWritePos_) - snapReadFrames;
        }

        // Janela Parabólica Otimizada (Cheap e suave como Cosine) -> 4 * p * (1 - p)
        float window = 4.0f * p * (1.0f - p);

        // Onde ler? Pitch neutro (1x) -> delayTap fixo por grão (alterado no jitter)
        float readMs = grainOffsetMs_[i] + grainJitter_[i] + driftMs;
        float readFrames = readMs * (sampleRate_ / 1000.0f);
        
        // Envolve o delay pacificamente para reutilizar o buffer circular sem empilhar grãos no limite
        readFrames = fmodf(readFrames, fGranSize - 4.0f);
        if (readFrames < 2.0f) readFrames = 2.0f;
        
        float tapFixoOriginal = static_cast<float>(grainWritePos_) - readFrames;
        float anchorScanCompleto = grainAnchorPos_[i] + p * phaseFramesTotal;
        float readPosReverse = grainAnchorPos_[i] - p * phaseFramesTotal;
        
        float readPosForward = cgv_dsp::lerp(tapFixoOriginal, anchorScanCompleto, params_.grainScan);
        float readPos = cgv_dsp::lerp(readPosForward, readPosReverse, params_.reverseMix);

        if (readPos != readPos) readPos = 0.0f; // NaN check evasion

        if (readPos < 0.0f || readPos >= fGranSize) {
            readPos = fmodf(readPos, fGranSize);
            if (readPos < 0.0f) readPos += fGranSize;
        }

        // Interpolação fracionária (Hermite/Linear mix)
        size_t idx1 = static_cast<size_t>(readPos);
        size_t idx2 = (idx1 + 1) % grainMemorySize_;
        float frac = readPos - static_cast<float>(idx1);

        float sampleL = cgv_dsp::lerp(grainMemoryL_[idx1], grainMemoryL_[idx2], frac);
        float sampleR = cgv_dsp::lerp(grainMemoryR_[idx1], grainMemoryR_[idx2], frac);
        
        // Espalhamento L/R variável (orgânico)
        float pan = grainPan_[i];
        float panL = 0.25f + (1.0f - pan) * 0.75f;
        float panR = 0.25f + pan * 0.75f;

        if (params_.stereoCore) {
            // Strictly preserve L/R image
            accL += sampleL * window;
            accR += sampleR * window;
        } else {
            // Synthetic width from mono mixdown
            float monoSample = (sampleL + sampleR) * 0.5f;
            accL += monoSample * window * panL;
            accR += monoSample * window * panR;
        }
    }

    // Normalize output based on grain count
    float volumeComp = 1.8f / static_cast<float>(CGV_NUM_GRAINS);
    outL = accL * volumeComp;
    outR = accR * volumeComp;
}

void CloudGreyVerb::processEarly(float inL, float inR, float diffusion, float size,
                                 float& outL, float& outR) {
    // Feed-forward multi-tap early field.  Unlike the cloud diffuser this is
    // sourced straight from the post-pre-delay input, so Texture=0 still has
    // a spatial bridge to the dry source.  The timings are deliberately
    // incommensurate and stable: no short-loop ringing and no LFO pitch smear.
    // A larger virtual space spreads the reflections modestly but lets its
    // late cloud remain dominant.  Keep enough early energy for attachment.
    const float timeScale = cgv_dsp::lerp(kEarlyMinTimeScale, kEarlyMaxTimeScale, size);
    const float density = cgv_dsp::lerp(0.72f, 1.0f, diffusion);
    outL = 0.0f;
    outR = 0.0f;
    earlyDelayL_.write(inL);
    earlyDelayR_.write(inR);
    for (int i = 0; i < CGV_NUM_EARLY_TAPS; ++i) {
        const auto& tap = kEarlyTaps[i];
        const float tapL = earlyDelayL_.read(tap.delayLSeconds * timeScale * sampleRate_);
        const float tapR = earlyDelayR_.read(tap.delayRSeconds * timeScale * sampleRate_);
        const float cross = tap.crossfeed * density;
        const float gain = tap.gain * (i == 0 ? 1.0f : density);
        outL += gain * (tapL * (1.0f - cross) + tapR * cross);
        outR += gain * (tapR * (1.0f - cross) + tapL * cross);
    }
    cgv_dsp::sanitize(outL);
    cgv_dsp::sanitize(outR);
}

void CloudGreyVerb::processSample(float inL, float inR, float& outL, float& outR) {
    if (!initialized_) {
        // Dry-through seguro se não inicializado
        outL = inL; outR = inR;
        return;
    }

    // --- Smoothing Update per-sample ---
    float sSize = smoothSize_.process(params_.size);
    float sFeedback = smoothFeedback_.process(params_.feedback);
    float sDiff = smoothDiffusion_.process(params_.diffusion);
    float sDamp = smoothDamping_.process(params_.damping);
    float sLowDamp = smoothLowDamping_.process(params_.lowDamping);
    float sTone = smoothTone_.process(params_.tone);

    // --- Conditional Recalculation (cheap eps check) ---
    if (fabsf(params_.modRate - lastModRate_) > 0.001f) {
        lastModRate_ = params_.modRate;
        float lfoHz = cgv_dsp::lerp(0.05f, 2.0f, params_.modRate);
        lfo1_.setRate(lfoHz, sampleRate_);
        lfo2_.setRate(lfoHz * 0.87f, sampleRate_);
        spinLfo_.setRate(0.5f + params_.modRate * 2.0f, sampleRate_);
    }

    if (fabsf(sLowDamp - lastSmoothedLowDamping_) > 0.001f) {
        lastSmoothedLowDamping_ = sLowDamp;
        float hpFreq = cgv_dsp::lerp(20.0f, 400.0f, sLowDamp);
        for (int i = 0; i < CGV_FDN_ORDER; ++i)
            fdnHighPassState_[i].setFreq(hpFreq, sampleRate_);
    }

    if (fabsf(sTone - lastSmoothedTone_) > 0.001f) {
        lastSmoothedTone_ = sTone;
        if (sTone < 0.5f) {
            toneGainLow_ = 1.0f;
            toneGainHigh_ = sTone * 2.0f;
        } else {
            toneGainLow_ = (1.0f - (sTone - 0.5f) * 2.0f);
            toneGainHigh_ = 1.0f;
        }
    }

#if CGV_ENABLE_SHIMMER
    if (shimmerAvailable_) {
        const float ratioMap[5] = {0.5f, 1.5f, 2.0f, 3.0f, 4.0f};
        shimmer_.setRatio(ratioMap[params_.shimmerRatioIndex]);
    }
#endif

    // 1. Excitação e Roteamento
    inL *= params_.inputGain;
    inR *= params_.inputGain;
    
    // Envelope tracking of input magnitude para Ducking Tonal e Shimmer
    float inMag = (fabsf(inL) + fabsf(inR)) * 0.5f;
    if (inMag > duckingEnvState_) {
        duckingEnvState_ += duckAttackCoeff_ * (inMag - duckingEnvState_);
    } else {
        duckingEnvState_ += duckReleaseCoeff_ * (inMag - duckingEnvState_);
    }
    
    // Proteção rigorosa contra NaN do input:
    cgv_dsp::sanitize(inL);
    cgv_dsp::sanitize(inR);
    
    // 1.5. Pre-Delay
    preDelaySmoothed_ += preDelaySmoothingCoeff_ * (preDelayTargetFrames_ - preDelaySmoothed_);
    if (preDelaySmoothed_ < 1.0f) preDelaySmoothed_ = 1.0f; // minimum 1 sample delay
    
    preDelayL_.write(inL);
    preDelayR_.write(inR);
    
    float pdL = preDelayL_.read(preDelaySmoothed_);
    float pdR = preDelayR_.read(preDelaySmoothed_);
    if (preDelayCrossfadeSamplesRemaining_ > 0) {
        const float oldL = preDelayL_.read(preDelayPreviousFrames_);
        const float oldR = preDelayR_.read(preDelayPreviousFrames_);
        const float fade = 1.0f - static_cast<float>(preDelayCrossfadeSamplesRemaining_)
            / static_cast<float>(preDelayCrossfadeSamplesTotal_);
        pdL = oldL + (pdL - oldL) * fade;
        pdR = oldR + (pdR - oldR) * fade;
        --preDelayCrossfadeSamplesRemaining_;
    }
    float pdMono = (pdL + pdR) * 0.5f;

    // Kill-Dry dinâmico na injeção da malha: permite solar por cima da nuvem travada
    float freezeKill = 1.0f - freezeSmoothed_;
    freezeKill *= freezeKill; // Curva quadrática para um fade rápido e suave
    
    if (params_.hardFreeze) {
        freezeKill = 0.0f;
    }
    
    pdL *= freezeKill;
    pdR *= freezeKill;
    pdMono *= freezeKill;

    // Early path is intentionally before granular processing and FDN input.
    float earlyL = 0.0f, earlyR = 0.0f;
    processEarly(pdL, pdR, sDiff, sSize, earlyL, earlyR);

    // LFOs (Calculados cedo para fornecer drift p/ motor Granular)
    float lfo1_val = lfo1_.process();
    float lfo2_val = lfo2_.process();
    spinLfo_.process();
    
    // Modulation drift update
    // Generate random modulation targets at a time-domain rate, rather than
    // consuming one PRNG value per audio sample. This also keeps granular
    // jitter's deterministic sequence independent of sample rate.
    modRandomPhase_ += 1000.0f / sampleRate_;
    if (modRandomPhase_ >= 1.0f) {
        modRandomPhase_ -= 1.0f;
        modTargetL_ = modulationPrng_.randFloat() * 2.0f - 1.0f;
        modTargetR_ = modulationPrng_.randFloat() * 2.0f - 1.0f;
    }
    modDriftL_ = cgv_dsp::lerp(modDriftL_, modTargetL_, driftLCoeff_);
    modDriftR_ = cgv_dsp::lerp(modDriftR_, modTargetR_, driftRCoeff_);

    // 2. Núcleo Granular Estéreo (Clouds-ish smear/freeze)
    float granOutL = 0.0f, granOutR = 0.0f;
    if (params_.stereoCore) {
        processGranular(pdL, pdR, lfo1_val, granOutL, granOutR);
    } else {
        processGranular(pdMono, pdMono, lfo1_val, granOutL, granOutR);
    }

    // 3. Diffuser / Allpass Series
    float diffCoef = cgv_dsp::lerp(0.1f, 0.75f, sDiff);
    const float diffuserSpinDepth = params_.modDepth * 2.5f;
    float spin1 = spinLfo_.getValue(0.0f) * diffuserSpinDepth;
    float spin2 = spinLfo_.getValue(0.25f) * diffuserSpinDepth;
    float spin3 = spinLfo_.getValue(0.5f) * diffuserSpinDepth;
    float spin4 = spinLfo_.getValue(0.75f) * diffuserSpinDepth;
    float spinVals[4] = { spin1, spin2, spin3, spin4 };

    float diffInL = 0.0f;
    float diffInR = 0.0f;

    if (params_.stereoCore) {
        diffInL = granOutL;
        diffInR = granOutR;
        for (int i = 0; i < CGV_NUM_ALLPASS; ++i) {
            diffInL = diffuserApL_[i].processModulated(diffInL, diffCoef, spinVals[i]);
            diffInR = diffuserApR_[i].processModulated(diffInR, diffCoef, spinVals[i]);
        }
    } else {
        float diffSignalMono = (granOutL + granOutR) * 0.5f;
        for (int i = 0; i < CGV_NUM_ALLPASS; ++i) {
            diffSignalMono = diffuserApL_[i].processModulated(diffSignalMono, diffCoef, spinVals[i]);
        }
        // Criamos a base injetável combinando o estéreo granular limpo + Diffusor Mono (pseudo-decorrelacionado)
        diffInL = granOutL * 0.4f + diffSignalMono * 0.8f;
        diffInR = granOutR * 0.4f - diffSignalMono * 0.8f;
    }

    // 4. Feedback Delay Network. Os perfis principais usam quatro linhas
    // acopladas por uma Hadamard normalizada; LOW_CPU preserva o fallback 2x2.
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fdnDelay_[i].setFrozen(params_.hardFreeze);

    constexpr float kFdnDelayRatios[4] = {
        1.0f,
        0.81649658f, // sqrt(2/3)
        0.70710678f, // 1/sqrt(2)
        0.61803399f  // golden-ratio conjugate
    };

    float fdnModulation[4] = {
        lfo1_val * 0.85f + modDriftL_ * 0.15f,
        lfo2_val * 0.85f + modDriftR_ * 0.15f,
        spinLfo_.getValue(0.125f) * 0.82f + modDriftL_ * 0.18f,
        spinLfo_.getValue(0.625f) * 0.82f + modDriftR_ * 0.18f
    };

    const float baseDelayTime = sizeToSeconds(sSize, params_.sizeScale) * sampleRate_;
    const float modFrames = params_.modDepth * 0.015f * sampleRate_;
    const float maxDelayAllowed = static_cast<float>(mainDelaySize_) - 2.0f;

    float fdnRead[CGV_FDN_ORDER] = {0.0f};
    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        float delayFrames = baseDelayTime * kFdnDelayRatios[i]
                          + fdnModulation[i] * modFrames * (1.0f - 0.06f * static_cast<float>(i));
        if (delayFrames < 2.0f) delayFrames = 2.0f;
        else if (delayFrames > maxDelayAllowed) delayFrames = maxDelayAllowed;
        fdnRead[i] = fdnDelay_[i].read(delayFrames);
    }

    // Damping independente por linha evita estados de filtro compartilhados e
    // mantém a decorrelação criada pelos comprimentos não proporcionais.
    float baseLpFreq = cgv_dsp::lerp(800.0f, 15000.0f, sDamp);
    // Dynamic damping is a gentle bloom: loud attacks are kept clean/darker,
    // then the existing 104 ms release lets the tail open without an audible
    // post-note brightness jump.  The former 1/(1+8*env) curve could remove
    // more than 80% of the cutoff on normal musical peaks.
    const float bloomFactor = 0.58f + 0.42f / (1.0f + duckingEnvState_ * 3.0f);
    float dynamicLpFreq = baseLpFreq * bloomFactor;
    if (dynamicLpFreq < 300.0f) dynamicLpFreq = 300.0f;

    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        fdnDamp_[i].setFreq(dynamicLpFreq, sampleRate_);
        fdnRead[i] = fdnDamp_[i].process(fdnRead[i]);
        fdnRead[i] -= fdnHighPassState_[i].process(fdnRead[i]);
#if CGV_NUM_LOOP_ALLPASS > 0
        fdnRead[i] = fdnLoopAp_[i].processModulated(fdnRead[i], 0.5f,
                                                    spinVals[i] * 0.5f);
#endif
        cgv_dsp::sanitize(fdnRead[i]);
    }

    float tailL = 0.0f;
    float tailR = 0.0f;
    decodeStereoFromFdn(fdnRead, tailL, tailR);

    float feedbackVector[CGV_FDN_ORDER] = {0.0f};
    mixFdnFeedback(fdnRead, feedbackVector);

    const float inputInject = cgv_dsp::lerp(0.28f, 0.40f, sDiff);
    const float sizeComp = cgv_dsp::lerp(1.0f, 0.88f, sSize);
    float effectiveFeedback = sFeedback * sizeComp;

    // No freeze a matriz continua ortogonal, enquanto a entrada seca é cortada
    // e o ganho se aproxima de unidade sem cruzar o limite de estabilidade.
    effectiveFeedback = cgv_dsp::lerp(effectiveFeedback, 0.98f, freezeSmoothed_);

    float encodedInput[CGV_FDN_ORDER] = {0.0f};
    encodeStereoForFdn(diffInL, diffInR, encodedInput);

    float feedLoop[CGV_FDN_ORDER] = {0.0f};
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        feedLoop[i] = encodedInput[i] * inputInject
                    + feedbackVector[i] * effectiveFeedback;

    float shimmerWetL = 0.0f;
    float shimmerWetR = 0.0f;

#if CGV_ENABLE_SHIMMER
    if (shimmerAvailable_) {
        // Step smoother towards target parameter
        float currentShimmerAmount = shimmerSmoother_.process(params_.shimmer);
        
        if (currentShimmerAmount > 0.001f) {
            // Obter uma média mono filtrada da cauda
            float shimmerIn = (tailL + tailR) * 0.5f;
            shimmerIn = shimmerIn - shimmerHp_.process(shimmerIn); // HP 300Hz (hp = x - lp)
            shimmerIn = shimmerLp_.process(shimmerIn);             // LP dynamic
            
            shimmerIn = cgv_dsp::softClip(shimmerIn);
            
            float shimmerOutL = 0.0f, shimmerOutR = 0.0f;
            shimmer_.processStereo(shimmerIn, shimmerOutL, shimmerOutR);
            cgv_dsp::sanitize(shimmerOutL);
            cgv_dsp::sanitize(shimmerOutR);
            
            // Ducking amount: reduce send when duckEnv is high (input has strong transients).
            // Smooth interpolation with a natural floor at 0.25 to prevent complete disappearance
            float duck = 1.0f / (1.0f + duckingEnvState_ * 6.0f); 
            float duckGain = 0.25f + 0.75f * duck;
            
            float shimmerSend = currentShimmerAmount * 0.08f * duckGain; // Reduced max gain
            
            float encodedShimmer[CGV_FDN_ORDER] = {0.0f};
            encodeStereoForFdn(shimmerOutL, shimmerOutR, encodedShimmer);
            for (int i = 0; i < CGV_FDN_ORDER; ++i)
                feedLoop[i] += encodedShimmer[i] * shimmerSend;
            
            shimmerWetL = shimmerOutL * currentShimmerAmount * 0.08f;
            shimmerWetR = shimmerOutR * currentShimmerAmount * 0.08f;
            
            cgv_dsp::sanitize(shimmerWetL);
            cgv_dsp::sanitize(shimmerWetR);
            shimmerWetL = cgv_dsp::softClip(shimmerWetL);
            shimmerWetR = cgv_dsp::softClip(shimmerWetR);
        }
    }
#endif

    constexpr float kLoopWriteHeadroom = 0.88f;
    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        feedLoop[i] *= kLoopWriteHeadroom;
        // tapeClip normaliza o pico com ganho de 1.5. A compensação abaixo
        // devolve ganho unitário em sinais pequenos, essencial numa FDN.
        feedLoop[i] = cgv_dsp::tapeClip(feedLoop[i]) * (2.0f / 3.0f);
    }

    // --- Safety Energy Guard (v2) ---
    float e = 0.0f;
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        e += feedLoop[i] * feedLoop[i];
    // A codificação estéreo e a Hadamard são ortonormais, portanto a
    // soma das quatro linhas já está na mesma escala energética da entrada L/R.
    loopEnergy_ += energyCoeff_ * (e - loopEnergy_);

    // Feed-loop waveshaping/headroom keeps normal program material far below
    // the old 0.55 threshold, making the guard effectively unreachable. 0.05
    // remains transparent for nominal IRs but engages on sustained abuse.
    constexpr float kSafetyThreshold = 0.05f;
    float safety = 1.0f;
    if (loopEnergy_ > kSafetyThreshold) {
        safety = kSafetyThreshold / loopEnergy_;
        if (safety > 1.0f) safety = 1.0f;
        if (safety < 0.35f) safety = 0.35f;
    }
    float safetyCoeff = (safety < lastSafetyGain_) ? safetyAttackCoeff_ : safetyReleaseCoeff_;
    lastSafetyGain_ = cgv_dsp::lerp(lastSafetyGain_, safety, safetyCoeff);
    
    cgv_dsp::sanitize(loopEnergy_);
    cgv_dsp::sanitize(lastSafetyGain_);

    for (int i = 0; i < CGV_FDN_ORDER; ++i) {
        feedLoop[i] *= lastSafetyGain_;
        cgv_dsp::sanitize(feedLoop[i]);
        fdnDelay_[i].write(feedLoop[i]);
    }
    // --------------------------------

    // 5. Tonalidade Global (Tilt EQ)
    // The new early field is the primary dry/wet bridge.  Retain only a small
    // cloud-diffuser contribution so the historical smear still leads into
    // the tail without duplicating two strong glue mechanisms.
    #if defined(CGV_DISABLE_EARLY_LAYER)
    const float earlyLevel = 0.0f;
    const float cloudGlue = cgv_dsp::lerp(0.38f, 0.50f, sDiff);
    #else
    const float earlyLevel = cgv_dsp::lerp(0.56f, 0.30f, sSize);
    const float cloudGlue = cgv_dsp::lerp(0.10f, 0.16f, sDiff);
    #endif
    float wetL = tailL + earlyL * earlyLevel + diffInL * cloudGlue;
    float wetR = tailR + earlyR * earlyLevel + diffInR * cloudGlue;

    wetL += shimmerWetL;
    wetR += shimmerWetR;

    // Separa Low/High em 800Hz e remix com ganhos Tilt
    float lowL = toneL_.process(wetL);
    float lowR = toneR_.process(wetR);
    float highL = wetL - lowL;
    float highR = wetR - lowR;
    
    wetL = lowL * toneGainLow_ + highL * toneGainHigh_;
    wetR = lowR * toneGainLow_ + highR * toneGainHigh_;

    // 6. Stereo Width (Mid/Side processing)
    // Conversão M/S
    float mid = (wetL + wetR) * 0.5f;
    float side = (wetL - wetR) * 0.5f;
    
    // Scale side component (0.0 = mono, 1.0 = normal, 2.0 = extra wide)
    side *= params_.stereoWidth;
    
    // Reconstrução L/R
    wetL = mid + side;
    wetR = mid - side;

    // Equal Power Crossfading
    float finalL = ((inL * gainDry_) + (wetL * gainWet_)) * params_.outputGain;
    float finalR = ((inR * gainDry_) + (wetR * gainWet_)) * params_.outputGain;

    // Embedded converters retain the final rail guard; float hosts may opt out.
    outL = params_.clipOutput ? cgv_dsp::hardClip(finalL) : finalL;
    outR = params_.clipOutput ? cgv_dsp::hardClip(finalR) : finalR;
    
    // Antídoto final contra NaN blowout:
    if (!std::isfinite(outL)) outL = 0.0f;
    if (!std::isfinite(outR)) outR = 0.0f;
}

void CloudGreyVerb::processBlock(float* left, float* right, size_t numFrames) {
    if (!left || !right) return;
    for(size_t i = 0; i < numFrames; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;
        processSample(left[i], right[i], outL, outR);
        left[i] = outL;
        right[i] = outR;
    }
}
