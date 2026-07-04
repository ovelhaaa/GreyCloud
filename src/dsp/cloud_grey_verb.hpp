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
#if !defined(CLOUD_GREY_PROFILE_H5_LOW_CPU) && !defined(CLOUD_GREY_PROFILE_H7_HIGH_QUALITY) && !defined(CLOUD_GREY_PROFILE_H5_BALANCED) && !defined(CLOUD_GREY_PROFILE_DESKTOP_STUDIO)
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
#elif CLOUD_GREY_PROFILE_DESKTOP_STUDIO
    #define CGV_NUM_GRAINS 6
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
    void setRatio(float ratio);

private:
    float readDelay(float delaySamples) const;

    float* buffer_ = nullptr;
    uint32_t size_ = 0;
    uint32_t writePos_ = 0;
    float sampleRate_ = 48000.0f;

    float phaseA_ = 0.0f;
    float phaseB_ = 0.5f;
    float phaseInc_ = 0.0f;
    float targetPhaseInc_ = 0.0f;
    cgv_dsp::OnePoleRC phaseIncSmoother_;

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
        int shimmerRatioIndex = 2; // 0=-1oct, 1=+5th, 2=+1oct, 3=+1oct+5th, 4=+2oct
        float inputGain = 1.0f;    // 0.0 a 2.0 -> Compensação / Excitação de entrada
        float outputGain = 1.0f;   // 0.0 a 2.0 -> Saída geral
        float preDelay = 0.0f;     // 0.0 a 1.0 -> 0ms a 200ms
        float stereoWidth = 1.0f;  // 0.0 a 2.0 -> 0=Mono, 1=Stereo, 2=Extra Wide
        float lowDamping = 0.5f;   // 0.0 a 1.0 -> High-pass do feedback. 0=Thin (corta graves), 1=Full/Lama
        bool stereoCore = true;    // True: Processa grãos e diffusor em estéreo discreto
        bool hardFreeze = false;   // True: Corta input 100% e congela estado instantaneamente
        float reverseMix = 0.0f;   // 0.0 a 1.0 -> Direção do grão (Forward -> Reverse)
        float grainScan = 0.0f;    // 0.0 a 1.0 -> Janela estática vs varredura real completa
    };
    
    static constexpr float kSizeMinFrameRatio = 0.05f;
    static constexpr float kSizeMaxFrameRatio = 0.95f;

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
    // Status / Monitoramento
    float getFreezeState() const { return freezeSmoothed_; }
    float getLoopEnergy() const { return loopEnergy_; }
    float getSafetyGain() const { return lastSafetyGain_; }
    size_t getMainDelayFrames() const { return mainDelaySize_; }

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
    float* grainMemoryL_ = nullptr;
    float* grainMemoryR_ = nullptr;
    size_t grainMemorySize_ = 0;
    size_t grainWritePos_ = 0;
    float grainPhase_ = 0.0f;
    
    // Controle Granular Estendido
    cgv_dsp::FastPRNG prng_;
    float grainJitter_[CGV_NUM_GRAINS] = {0.0f};
    float grainPan_[CGV_NUM_GRAINS] = {0.5f};
    float grainOffsetMs_[CGV_NUM_GRAINS] = {0.0f};
    float grainAnchorPos_[CGV_NUM_GRAINS] = {0.0f};
    float freezeSmoothed_ = 0.0f;

    // Núcleo Diffuser (Smear Allpasses pré-delay)
    cgv_dsp::Allpass diffuserApL_[CGV_NUM_ALLPASS];
    cgv_dsp::Allpass diffuserApR_[CGV_NUM_ALLPASS];

    // Rede Greyhole (Long Modulated delays + Allpasses no Loop)
    cgv_dsp::DelayLine delayL_, delayR_;
    cgv_dsp::Allpass loopApL_, loopApR_;
    size_t mainDelaySize_ = 0;
    
    cgv_dsp::DelayLine preDelayL_, preDelayR_;
    float preDelaySmoothed_ = 0.0f;

    // LFOs dedicados (Fases cruzadas para imagem estéreo larga)
    cgv_dsp::LFO lfo1_, lfo2_;
    cgv_dsp::LFO spinLfo_;
    
    // Modulation drift state
    float modDriftL_ = 0.0f;
    float modDriftR_ = 0.0f;
    
    // Safety guard loop state
    float loopEnergy_ = 0.0f;
    float lastSafetyGain_ = 1.0f;

    // Filtros
    cgv_dsp::OnePoleRC dampL_, dampR_;
    cgv_dsp::OnePoleRC hpFeedL_, hpFeedR_; // Filtro HP para secar o low end
    cgv_dsp::OnePoleRC toneL_, toneR_;

    // Suavizadores de Parâmetros
    cgv_dsp::OnePoleRC smoothSize_;
    cgv_dsp::OnePoleRC smoothFeedback_;
    cgv_dsp::OnePoleRC smoothDiffusion_;
    cgv_dsp::OnePoleRC smoothDamping_;
    cgv_dsp::OnePoleRC smoothLowDamping_;
    cgv_dsp::OnePoleRC smoothTone_;

    // Variáveis de estado para recálculo condicional de coeficientes
    float lastSmoothedDamping_ = -1.0f;
    float lastSmoothedLowDamping_ = -1.0f;
    float lastSmoothedTone_ = -1.0f;
    float lastModRate_ = -1.0f;
    float lastDynamicLpFreq_ = -1.0f;

    // Envelope Follower para Ducking do Reverb e do Shimmer
    float duckingEnvState_ = 0.0f;

#if CGV_ENABLE_SHIMMER
    ShimmerPitcher shimmer_;
    bool shimmerAvailable_ = false;
    cgv_dsp::OnePoleRC shimmerHp_;
    cgv_dsp::OnePoleRC shimmerLp_;
    cgv_dsp::OnePoleRC shimmerSmoother_;
#endif

    // Helpers
    void processGranular(float inL, float inR, float lfoDrift, float& outL, float& outR);
};
