#pragma once
#include <cstdint>
#include <cstddef>
#include "dsp_utils.hpp"

/**
 * CloudGreyVerb DSP Core
 * ----------------------
 * Inspirado livremente no Mutable Instruments Clouds (Granular core/smear)
 * e Greyhole (Feedback difuso, wash longo estéreo). 
 * 
 * Target Principal: STM32H5 (Cortex-M33F)
 * Targets Secundários: STM32H7, ESP32-S3
 * Características: Sem alocação dinâmica, operação float 32-bit (portável para fixed point opcionalmente), 
 * processamento mono->estéreo, rotinas isoladas do RTOS, otimizado para pequenos block sizes.
 */

// --- Perfis de Compilação STM32H5 ---
// Use flags no compilador para definir (-DCLOUD_GREY_PROFILE_H5_LOW_CPU=1)
#if !defined(CLOUD_GREY_PROFILE_H5_LOW_CPU) && !defined(CLOUD_GREY_PROFILE_H7_HIGH_QUALITY) && !defined(CLOUD_GREY_PROFILE_H5_BALANCED)
    // Default = Balanced
    #define CLOUD_GREY_PROFILE_H5_BALANCED 1
#endif

#if CLOUD_GREY_PROFILE_H5_LOW_CPU
    #define CGV_NUM_GRAINS 3
    #define CGV_NUM_ALLPASS 2
    #define CGV_NUM_LOOP_ALLPASS 0
    #ifndef CGV_ENABLE_SHIMMER
        #define CGV_ENABLE_SHIMMER 0
    #endif
#elif CLOUD_GREY_PROFILE_H7_HIGH_QUALITY
    #define CGV_NUM_GRAINS 4
    #define CGV_NUM_ALLPASS 4
    #define CGV_NUM_LOOP_ALLPASS 2
    #ifndef CGV_ENABLE_SHIMMER
        #define CGV_ENABLE_SHIMMER 1
    #endif
#else // H5_BALANCED
    #define CGV_NUM_GRAINS 4
    #define CGV_NUM_ALLPASS 4
    #define CGV_NUM_LOOP_ALLPASS 2
    #ifndef CGV_ENABLE_SHIMMER
        #define CGV_ENABLE_SHIMMER 0
    #endif
#endif

#if CGV_ENABLE_SHIMMER
class ShimmerPitcher {
public:
    bool init(float sampleRate, float* buffer, uint32_t bufferSize);
    void reset();
    float process(float input);
    void processStereo(float input, float& outL, float& outR);

private:
    float readDelay(float delaySamples) const;

    float* buffer_ = nullptr;
    uint32_t size_ = 0;
    uint32_t writePos_ = 0;
    float sampleRate_ = 48000.0f;

    float phaseA_ = 0.0f;
    float phaseB_ = 0.5f;
    float phaseInc_ = 0.0f;

    float minDelaySamples_ = 0.0f;
    float depthSamples_ = 0.0f;
};
#endif

class CloudGreyVerb {
public:
    enum class Preset {
        SmallCloudRoom,
        BassAmbientWash,
        FrozenOrganPad,
        GreyholeDelayVerb,
        DarkLongCloud,
        GlitchSmear,
        AlwaysOnSubtle,
        BrightCloud,
        ShimmerCloud
    };

    struct Params {
        float mix = 0.5f;          // 0.0 a 1.0 -> Dry/Wet mix igual potência
        float texture = 0.5f;      // 0.0 a 1.0 -> Janela/Densidade granular (Curto/Mecânico -> Longo/Smear)
        float freeze = 0.0f;       // 0.0 a 1.0 -> Congela buffer granular (se > 0.5 trava leitura e prolonga recirculação)
        float feedback = 0.5f;     // 0.0 a 1.0 -> Realimentação do Greyhole
        float size = 0.5f;         // 0.0 a 1.0 -> Tempo base da rede delay (Curto -> Huge Cloud)
        float diffusion = 0.5f;    // 0.0 a 1.0 -> Coeficiente dos Allpasses (Ringing metálico -> Nuvem difusa)
        float modDepth = 0.2f;     // 0.0 a 1.0 -> Quantidade de drift dos LFOs no delay (Pitch modulation)
        float modRate = 0.2f;      // 0.0 a 1.0 -> Frequência dos LFOs (0.05 Hz a 2 Hz)
        float damping = 0.5f;      // 0.0 a 1.0 -> Absorção de altas frequências no feedback (Dark -> Bright)
        float tone = 0.5f;         // 0.0 a 1.0 -> Filtro Tilt no Wet: <0.5 Dark, >0.5 Bright
        float shimmer = 0.0f;      // 0.0 a 1.0 -> Placeholder para Pitch Shift (+1 OCT) no feedback (TODO)
        float inputGain = 1.0f;    // 0.0 a 2.0 -> Compensação / Excitação de entrada
        float outputGain = 1.0f;   // 0.0 a 2.0 -> Saída geral
        float preDelay = 0.0f;     // 0.0 a 1.0 -> 0ms a 200ms
        float stereoWidth = 1.0f;  // 0.0 a 2.0 -> 0=Mono, 1=Stereo, 2=Extra Wide
        float lowDamping = 0.5f;   // 0.0 a 1.0 -> High-pass do feedback. 0=Thin (corta graves), 1=Full/Lama
    };

    // Utilitário de Presets Internos
    static Params getPreset(Preset preset);

    /**
     * Inicializa o motor DSP.
     * @param sampleRate Frequência de amostragem (padrão 48000.0f)
     * @param externalBuffer Buffer pré-alocado contínuo pelo usuário (BSS ou Heap/PSRAM)
     * @param bufferSize Tamanho total do buffer em palavras float
     */
    void init(float sampleRate, float* externalBuffer, size_t bufferSize);
    
    // Limpa a memória do áudio sem desalocar/realocar
    void reset();
    
    // Atualiza os parâmetros do algoritmo (Pode ser chamado pela thread de UI suavemente)
    void setParams(const Params& p);

    // Processamento de bloco completo (Acelera caches/MCUs modernas)
    void processBlock(float* left, float* right, size_t numFrames);

    // Processamento amostra a amostra (Útil em loops menores ou ISR/Callbacks simples)
    void processSample(float inL, float inR, float& outL, float& outR);

    // Status / Monitoramento
    float getFreezeState() const { return freezeSmoothed_; }
    float getLoopEnergy() const { return loopEnergy_; }
    float getSafetyGain() const { return lastSafetyGain_; }

private:
    bool initialized_ = false;
    float sampleRate_ = 48000.0f;
    Params params_;
    
    // Ganhos pré-calculados para otimizar sample loop
    float gainDry_ = 0.7071f;
    float gainWet_ = 0.7071f;
    float toneGainLow_ = 1.0f;
    float toneGainHigh_ = 1.0f;

    // Buffer handling para o micro-kernel granulador
    float* grainMemory_ = nullptr;
    size_t grainMemorySize_ = 0;
    size_t grainWritePos_ = 0;
    float grainPhase_ = 0.0f;
    
    // Controle Granular Estendido
    dsp::FastPRNG prng_;
    float grainJitter_[CGV_NUM_GRAINS] = {0.0f};
    float grainPan_[CGV_NUM_GRAINS] = {0.5f};
    float grainOffsetMs_[CGV_NUM_GRAINS] = {0.0f};
    float freezeSmoothed_ = 0.0f;

    // Núcleo Diffuser (Smear Allpasses pré-delay)
    dsp::Allpass ap1_, ap2_, ap3_, ap4_;

    // Rede Greyhole (Long Modulated delays + Allpasses no Loop)
    dsp::DelayLine delayL_, delayR_;
    dsp::Allpass loopApL_, loopApR_;
    size_t mainDelaySize_ = 0;
    
    dsp::DelayLine preDelayMono_;
    float preDelaySmoothed_ = 0.0f;

    // LFOs dedicados (Fases cruzadas para imagem estéreo larga)
    dsp::LFO lfo1_, lfo2_;
    dsp::LFO spinLfo_;
    
    // Modulation drift state
    float modDriftL_ = 0.0f;
    float modDriftR_ = 0.0f;
    
    // Safety guard loop state
    float loopEnergy_ = 0.0f;
    float lastSafetyGain_ = 1.0f;

    // Filtros
    dsp::OnePoleRC dampL_, dampR_;
    dsp::OnePoleRC hpFeedL_, hpFeedR_; // Filtro HP para secar o low end
    dsp::OnePoleRC toneL_, toneR_;

    // Envelope Follower para Ducking do Reverb e do Shimmer
    float duckingEnvState_ = 0.0f;

#if CGV_ENABLE_SHIMMER
    ShimmerPitcher shimmer_;
    bool shimmerAvailable_ = false;
    dsp::OnePoleRC shimmerHp_;
    dsp::OnePoleRC shimmerLp_;
    dsp::OnePoleRC shimmerSmoother_;
#endif

    // Helpers
    void processGranular(float input, float lfoDrift, float& outL, float& outR);
};
