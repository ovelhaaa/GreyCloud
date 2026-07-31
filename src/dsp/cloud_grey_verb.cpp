#include "cloud_grey_verb.hpp"

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

// Presets sugeridos
CloudGreyVerb::Params CloudGreyVerb::getPreset(Preset preset) {
    Params p;
    p.inputGain = 1.0f;
    p.outputGain = 1.0f;
    p.shimmer = 0.0f;
    switch(preset) {
        case Preset::SmallCloudRoom:
            p.mix = 0.4f; p.texture = 0.3f; p.freeze = 0.0f; p.feedback = 0.5f;
            p.size = 0.35f; p.diffusion = 0.6f; p.modDepth = 0.2f; p.modRate = 0.15f;
            p.damping = 0.5f; p.tone = 0.6f;
            break;
        case Preset::BassAmbientWash:
            p.mix = 0.36f; p.texture = 0.42f; p.freeze = 0.0f; p.feedback = 0.62f;
            p.size = 0.56f; p.diffusion = 0.52f; p.modDepth = 0.14f; p.modRate = 0.15f;
            p.damping = 0.78f; p.tone = 0.40f; p.inputGain = 0.90f; p.outputGain = 0.92f; p.shimmer = 0.0f;
            break;
        case Preset::FrozenOrganPad:
            p.mix = 0.7f; p.texture = 0.85f; p.freeze = 1.0f; p.feedback = 0.65f;
            p.size = 0.7f; p.diffusion = 0.8f; p.modDepth = 0.4f; p.modRate = 0.05f;
            p.damping = 0.4f; p.tone = 0.45f;
            break;
        case Preset::GreyholeDelayVerb:
            p.mix = 0.6f; p.texture = 0.55f; p.freeze = 0.0f; p.feedback = 0.76f;
            p.size = 0.76f; p.diffusion = 0.70f; p.modDepth = 0.4f; p.modRate = 0.25f;
            p.damping = 0.65f; p.tone = 0.5f; p.outputGain = 0.90f;
            break;
        case Preset::DarkLongCloud:
            p.mix = 0.55f; p.texture = 0.75f; p.freeze = 0.0f; p.feedback = 0.76f;
            p.size = 0.84f; p.diffusion = 0.66f; p.modDepth = 0.3f; p.modRate = 0.1f;
            p.damping = 0.3f; p.tone = 0.3f; p.inputGain = 0.72f; p.outputGain = 0.72f;
            break;
        case Preset::GlitchSmear:
            p.mix = 0.5f; p.texture = 0.05f; p.freeze = 0.0f; p.feedback = 0.5f;
            p.size = 0.25f; p.diffusion = 0.2f; p.modDepth = 0.9f; p.modRate = 0.8f;
            p.damping = 0.5f; p.tone = 0.5f;
            break;
        case Preset::AlwaysOnSubtle:
            p.mix = 0.25f; p.texture = 0.2f; p.freeze = 0.0f; p.feedback = 0.3f;
            p.size = 0.2f; p.diffusion = 0.4f; p.modDepth = 0.1f; p.modRate = 0.1f;
            p.damping = 0.5f; p.tone = 0.5f;
            break;
        case Preset::BrightCloud:
            p.mix = 0.5f; p.texture = 0.6f; p.freeze = 0.0f; p.feedback = 0.75f;
            p.size = 0.6f; p.diffusion = 0.7f; p.modDepth = 0.6f; p.modRate = 0.4f;
            p.damping = 0.7f; p.tone = 0.8f; p.shimmer = 0.0f;
            break;
        case Preset::ShimmerCloud:
            p.mix = 0.55f; p.texture = 0.55f; p.freeze = 0.0f; p.feedback = 0.58f;
            p.size = 0.62f; p.diffusion = 0.70f; p.modDepth = 0.20f; p.modRate = 0.12f;
            p.damping = 0.55f; p.tone = 0.62f; p.shimmer = 0.20f; p.shimmerRatioIndex = 2; p.inputGain = 0.80f; p.outputGain = 0.85f;
            break;
    }
    return p;
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
#if CGV_ENABLE_SHIMMER
    shimmerAvailable_ = false;
#endif
    
    // Custo aproximado e Segurança:
    // Para 48kHz, recomenda-se ao menos 24000 frames (96KB RAM) para delays utilizáveis.
    // Menos que isso soará como reverb de mola curto.
    if (!externalBuffer || bufferSize < 24000 || sampleRate <= 0.0f) return;

    sampleRate_ = sampleRate;

    // Repartição do buffer contínuo. O granulador usa cerca de 10% por canal,
    // os all-passes ocupam frações pequenas e a FDN divide igualmente o restante.
    size_t granulSize = static_cast<size_t>(bufferSize * 0.10f);
    size_t diffuserLSizes[4] = {0};
    size_t diffuserRSizes[4] = {0};
    diffuserLSizes[0] = cgv_dsp::nextPrime(static_cast<size_t>(bufferSize * 0.0075f)) + 1;
    diffuserLSizes[1] = cgv_dsp::nextPrime(static_cast<size_t>(bufferSize * 0.01f)) + 1;
#if CGV_NUM_ALLPASS > 2
    diffuserLSizes[2] = cgv_dsp::nextPrime(static_cast<size_t>(bufferSize * 0.0125f)) + 1;
    diffuserLSizes[3] = cgv_dsp::nextPrime(static_cast<size_t>(bufferSize * 0.0175f)) + 1;
#endif
    for (int i = 0; i < CGV_NUM_ALLPASS; ++i)
        diffuserRSizes[i] = cgv_dsp::nextPrime(diffuserLSizes[i] + 5);

#if CGV_NUM_LOOP_ALLPASS > 0
    size_t fdnAllpassSizes[CGV_FDN_ORDER] = {0};
    constexpr float kFdnAllpassRatios[4] = {0.0060f, 0.0072f, 0.0086f, 0.0101f};
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fdnAllpassSizes[i] = cgv_dsp::nextPrime(static_cast<size_t>(bufferSize * kFdnAllpassRatios[i])) + 1;
#endif

#if CGV_ENABLE_SHIMMER
    size_t shimmerSize = static_cast<size_t>(bufferSize * 0.08f); // ~80ms a 48kHz
#else
    size_t shimmerSize = 0;
#endif
    
    // Allocate predelay (up to 200ms per channel)
    size_t predelaySize = static_cast<size_t>(sampleRate_ * 0.2f);
    if (predelaySize > bufferSize * 0.1f) {
        predelaySize = static_cast<size_t>(bufferSize * 0.1f);
    }
    
    size_t fixedSize = 2 * granulSize + shimmerSize + 2 * predelaySize;
    for (int i = 0; i < CGV_NUM_ALLPASS; ++i)
        fixedSize += diffuserLSizes[i] + diffuserRSizes[i];
#if CGV_NUM_LOOP_ALLPASS > 0
    for (int i = 0; i < CGV_FDN_ORDER; ++i)
        fixedSize += fdnAllpassSizes[i];
#endif

    constexpr size_t kMinimumDelayCapacity = 8;
    if (fixedSize >= bufferSize ||
        bufferSize - fixedSize < kMinimumDelayCapacity * CGV_FDN_ORDER)
        return;

    size_t remaining = bufferSize - fixedSize;
    mainDelaySize_ = remaining / CGV_FDN_ORDER;

    // Atribuição sequencial s/ alocação
    float* ptr = externalBuffer;

    preDelayL_.init(ptr, predelaySize); ptr += predelaySize;
    preDelayR_.init(ptr, predelaySize); ptr += predelaySize;

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
    
    initialized_ = true;
    reset();
}

void CloudGreyVerb::reset() {
    grainWritePos_ = 0;
    grainPhase_ = 0.0f;
    freezeSmoothed_ = 0.0f;
    prng_.seed(1234567);
    
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
    params_.stereoWidth = clampParam(params_.stereoWidth, 0.0f, 2.0f);
    params_.lowDamping = clampParam(params_.lowDamping, 0.0f, 1.0f);
    
    // Pré-cálculo de Ganhos Mix (Equal-power approximation)
    float m = params_.mix;
    gainDry_ = sqrtf(1.0f - m);
    gainWet_ = sqrtf(m);

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
    freezeSmoothed_ = cgv_dsp::lerp(freezeSmoothed_, params_.freeze, 0.005f);

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
        duckingEnvState_ += 0.05f * (inMag - duckingEnvState_); // Fast attack
    } else {
        duckingEnvState_ += 0.0002f * (inMag - duckingEnvState_); // Slow release
    }
    
    // Proteção rigorosa contra NaN do input:
    cgv_dsp::sanitize(inL);
    cgv_dsp::sanitize(inR);
    
    // 1.5. Pre-Delay
    float targetPredelayFrames = params_.preDelay * 0.2f * sampleRate_;
    preDelaySmoothed_ += 0.005f * (targetPredelayFrames - preDelaySmoothed_); 
    if (preDelaySmoothed_ < 1.0f) preDelaySmoothed_ = 1.0f; // minimum 1 sample delay
    
    preDelayL_.write(inL);
    preDelayR_.write(inR);
    
    float pdL = preDelayL_.read(preDelaySmoothed_);
    float pdR = preDelayR_.read(preDelaySmoothed_);
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

    // LFOs (Calculados cedo para fornecer drift p/ motor Granular)
    float lfo1_val = lfo1_.process();
    float lfo2_val = lfo2_.process();
    spinLfo_.process();
    
    // Modulation drift update
    float randL = prng_.randFloat() * 2.0f - 1.0f;
    float randR = prng_.randFloat() * 2.0f - 1.0f;
    modDriftL_ = cgv_dsp::lerp(modDriftL_, randL, 0.00005f);
    modDriftR_ = cgv_dsp::lerp(modDriftR_, randR, 0.00004f);

    // 2. Núcleo Granular Estéreo (Clouds-ish smear/freeze)
    float granOutL = 0.0f, granOutR = 0.0f;
    if (params_.stereoCore) {
        processGranular(pdL, pdR, lfo1_val, granOutL, granOutR);
    } else {
        processGranular(pdMono, pdMono, lfo1_val, granOutL, granOutR);
    }

    // 3. Diffuser / Allpass Series
    float diffCoef = cgv_dsp::lerp(0.1f, 0.75f, sDiff);
    float spin1 = spinLfo_.getValue(0.0f) * 2.5f;
    float spin2 = spinLfo_.getValue(0.25f) * 2.5f;
    float spin3 = spinLfo_.getValue(0.5f) * 2.5f;
    float spin4 = spinLfo_.getValue(0.75f) * 2.5f;
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

    const float maxDelayBase = static_cast<float>(mainDelaySize_) * kSizeMaxFrameRatio;
    const float baseDelayTime = cgv_dsp::lerp(sampleRate_ * kSizeMinFrameRatio,
                                               maxDelayBase, sSize);
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
    float duckFactor = 1.0f / (1.0f + duckingEnvState_ * 8.0f);
    float dynamicLpFreq = baseLpFreq * duckFactor;
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
    loopEnergy_ = 0.9995f * loopEnergy_ + 0.0005f * e;

    constexpr float kSafetyThreshold = 0.55f;
    float safety = 1.0f;
    if (loopEnergy_ > kSafetyThreshold) {
        safety = kSafetyThreshold / loopEnergy_;
        if (safety > 1.0f) safety = 1.0f;
        if (safety < 0.35f) safety = 0.35f;
    }
    float safetyCoeff = (safety < lastSafetyGain_) ? 0.03f : 0.001f;
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
    // Mistura frações do difusor de entrada na cauda p/ colar ataques
    float wetGlue = cgv_dsp::lerp(0.38f, 0.50f, sDiff);
    float wetL = tailL + diffInL * wetGlue;
    float wetR = tailR + diffInR * wetGlue;

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

    // Ganho de saída aplicado ao wet final
    wetL *= params_.outputGain;
    wetR *= params_.outputGain;
    
    // Equal Power Crossfading
    float finalL = (inL * gainDry_) + (wetL * gainWet_);
    float finalR = (inR * gainDry_) + (wetR * gainWet_);

    // Clip final safety para os conversores do MCU
    outL = cgv_dsp::hardClip(finalL);
    outR = cgv_dsp::hardClip(finalR);
    
    // Antídoto final contra NaN blowout:
    if (outL != outL) outL = 0.0f;
    if (outR != outR) outR = 0.0f;
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
