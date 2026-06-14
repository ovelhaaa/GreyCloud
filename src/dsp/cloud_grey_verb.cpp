#include "cloud_grey_verb.hpp"

// Presets sugeridos
CloudGreyVerb::Params CloudGreyVerb::getPreset(Preset preset) {
    Params p;
    switch(preset) {
        case Preset::SmallCloudRoom:
            p.mix = 0.4f; p.texture = 0.3f; p.freeze = 0.0f; p.feedback = 0.5f;
            p.size = 0.35f; p.diffusion = 0.6f; p.modDepth = 0.2f; p.modRate = 0.15f;
            p.damping = 0.5f; p.tone = 0.6f;
            break;
        case Preset::BassAmbientWash:
            p.mix = 0.35f; p.texture = 0.4f; p.freeze = 0.0f; p.feedback = 0.70f;
            p.size = 0.6f; p.diffusion = 0.5f; p.modDepth = 0.15f; p.modRate = 0.15f;
            p.damping = 0.8f; p.tone = 0.4f;
            break;
        case Preset::FrozenOrganPad:
            p.mix = 0.7f; p.texture = 0.85f; p.freeze = 1.0f; p.feedback = 0.65f;
            p.size = 0.7f; p.diffusion = 0.8f; p.modDepth = 0.4f; p.modRate = 0.05f;
            p.damping = 0.4f; p.tone = 0.45f;
            break;
        case Preset::GreyholeDelayVerb:
            p.mix = 0.6f; p.texture = 0.55f; p.freeze = 0.0f; p.feedback = 0.85f;
            p.size = 0.8f; p.diffusion = 0.75f; p.modDepth = 0.4f; p.modRate = 0.25f;
            p.damping = 0.65f; p.tone = 0.5f;
            break;
        case Preset::DarkInfiniteCloud:
            p.mix = 0.65f; p.texture = 0.75f; p.freeze = 0.0f; p.feedback = 0.95f;
            p.size = 0.9f; p.diffusion = 0.8f; p.modDepth = 0.3f; p.modRate = 0.1f;
            p.damping = 0.3f; p.tone = 0.3f;
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
        case Preset::FauxShimmerCloud:
            p.mix = 0.5f; p.texture = 0.6f; p.freeze = 0.0f; p.feedback = 0.75f;
            p.size = 0.6f; p.diffusion = 0.7f; p.modDepth = 0.6f; p.modRate = 0.4f;
            p.damping = 0.7f; p.tone = 0.8f; p.shimmer = 0.5f;
            break;
    }
    p.inputGain = 1.0f;
    p.outputGain = 1.0f;
    return p;
}

void CloudGreyVerb::init(float sampleRate, float* externalBuffer, size_t bufferSize) {
    initialized_ = false;
    
    // Custo aproximado e Segurança:
    // Para 48kHz, recomenda-se ao menos 24000 frames (96KB RAM) para delays utilizáveis.
    // Menos que isso soará como reverb de mola curto.
    if (!externalBuffer || bufferSize < 24000) return;

    sampleRate_ = sampleRate;

    // Repartição do buffer contínuo (Divisão amigável e segura para a memória entregue).
    // O Granulador usa cerca de 20%, APs pequenos ~10%, L/R Delays o resto.
    size_t granulSize = static_cast<size_t>(bufferSize * 0.20f);
    size_t ap1Size    = static_cast<size_t>(bufferSize * 0.015f); // ~13ms @48k
    size_t ap2Size    = static_cast<size_t>(bufferSize * 0.02f);  // ~20ms
#if CGV_NUM_ALLPASS > 2
    size_t ap3Size    = static_cast<size_t>(bufferSize * 0.025f); // ~25ms
    size_t ap4Size    = static_cast<size_t>(bufferSize * 0.035f); // ~35ms
#else
    size_t ap3Size    = 0;
    size_t ap4Size    = 0;
#endif

#if CGV_NUM_LOOP_ALLPASS > 0
    size_t lapLSize   = static_cast<size_t>(bufferSize * 0.015f); // Allpass loop L
    size_t lapRSize   = static_cast<size_t>(bufferSize * 0.018f); // Allpass loop R
#else
    size_t lapLSize   = 0;
    size_t lapRSize   = 0;
#endif
    
    // O restante vai para main delays (~66%)
    size_t remaining = bufferSize - (granulSize + ap1Size + ap2Size + ap3Size + ap4Size + lapLSize + lapRSize);
    mainDelaySize_ = remaining / 2;

    // Atribuição sequencial s/ alocação
    float* ptr = externalBuffer;

    grainMemory_ = ptr; ptr += granulSize;
    grainMemorySize_ = granulSize;

    ap1_.init(ptr, ap1Size); ptr += ap1Size;
    ap2_.init(ptr, ap2Size); ptr += ap2Size;
#if CGV_NUM_ALLPASS > 2
    ap3_.init(ptr, ap3Size); ptr += ap3Size;
    ap4_.init(ptr, ap4Size); ptr += ap4Size;
#endif
    
#if CGV_NUM_LOOP_ALLPASS > 0
    loopApL_.init(ptr, lapLSize); ptr += lapLSize;
    loopApR_.init(ptr, lapRSize); ptr += lapRSize;
#endif

    delayL_.init(ptr, mainDelaySize_); ptr += mainDelaySize_;
    delayR_.init(ptr, mainDelaySize_); ptr += mainDelaySize_;

    // LFO Init
    lfo1_.setRate(0.5f, sampleRate_);
    lfo2_.setRate(0.5f, sampleRate_); // Forçaremos diferença de fase lendo desfasado ou drift
    
    initialized_ = true;
    reset();
}

void CloudGreyVerb::reset() {
    grainWritePos_ = 0;
    grainPhase_ = 0.0f;
    freezeSmoothed_ = 0.0f;
    prng_.seed(1234567);
    
    for (int i=0; i<CGV_NUM_GRAINS; ++i) grainJitter_[i] = 0.0f;
    
    if (grainMemory_) {
        for(size_t i = 0; i < grainMemorySize_; ++i) grainMemory_[i] = 0.0f;
    }
    
    ap1_.clear(); ap2_.clear(); 
#if CGV_NUM_ALLPASS > 2
    ap3_.clear(); ap4_.clear();
#endif
#if CGV_NUM_LOOP_ALLPASS > 0
    loopApL_.clear(); loopApR_.clear();
#endif
    delayL_.clear(); delayR_.clear();
    lfo1_.clear(); lfo2_.clear();
    dampL_.clear(); dampR_.clear();
    hpFeedL_.clear(); hpFeedR_.clear();
    toneL_.clear(); toneR_.clear();
}

static inline float clampParam(float v, float minV, float maxV) {
    return (v < minV) ? minV : ((v > maxV) ? maxV : v);
}

void CloudGreyVerb::setParams(const Params& p) {
    params_ = p;
    
    // Clampar todos os parâmetros por segurança
    params_.mix = clampParam(params_.mix, 0.0f, 1.0f);
    params_.texture = clampParam(params_.texture, 0.0f, 1.0f);
    params_.freeze = clampParam(params_.freeze, 0.0f, 1.0f);
    params_.feedback = clampParam(params_.feedback, 0.0f, 0.98f); // Teto seguro
    params_.size = clampParam(params_.size, 0.0f, 1.0f);
    params_.diffusion = clampParam(params_.diffusion, 0.0f, 1.0f);
    params_.modDepth = clampParam(params_.modDepth, 0.0f, 1.0f);
    params_.modRate = clampParam(params_.modRate, 0.0f, 1.0f);
    params_.damping = clampParam(params_.damping, 0.0f, 1.0f);
    params_.tone = clampParam(params_.tone, 0.0f, 1.0f);
    params_.shimmer = clampParam(params_.shimmer, 0.0f, 1.0f);
    params_.inputGain = clampParam(params_.inputGain, 0.0f, 4.0f);
    params_.outputGain = clampParam(params_.outputGain, 0.0f, 4.0f);
    
    // Pré-cálculo de Ganhos Mix (Equal Power)
    float mixRadians = params_.mix * dsp::PI * 0.5f;
    gainDry_ = std::cos(mixRadians);
    gainWet_ = std::sin(mixRadians);
    
    // LFO Mapping (0.05 Hz slow drift - 2Hz chorus speed)
    float lfoHz = dsp::lerp(0.05f, 2.0f, params_.modRate);
    lfo1_.setRate(lfoHz, sampleRate_);
    lfo2_.setRate(lfoHz * 0.87f, sampleRate_); // 13% offset estéreo

    // Filtros
    float lpFreq = dsp::lerp(800.0f, 15000.0f, params_.damping);
    dampL_.setFreq(lpFreq, sampleRate_);
    dampR_.setFreq(lpFreq, sampleRate_);
    
    // Highpass no Feedback (Crucial para Bass/Guitars, evita lama 150hz)
    float hpFreq = dsp::lerp(80.0f, 180.0f, params_.damping);
    hpFeedL_.setFreq(hpFreq, sampleRate_);
    hpFeedR_.setFreq(hpFreq, sampleRate_);
    
    // Tone: Tilt EQ Muscial (pré-calcula as bandas de 800Hz)
    toneL_.setFreq(800.0f, sampleRate_); 
    toneR_.setFreq(800.0f, sampleRate_);
    if (params_.tone < 0.5f) {
        toneGainLow_ = 1.0f;
        toneGainHigh_ = params_.tone * 2.0f; // Atenua agudos
    } else {
        toneGainLow_ = (1.0f - (params_.tone - 0.5f) * 2.0f); // Atenua graves
        toneGainHigh_ = 1.0f;
    }
}

void CloudGreyVerb::processGranular(float input, float lfoDrift, float& outL, float& outR) {
    // FREEZE Smoothed: Transição musical (Real buffer freeze misturado)
    freezeSmoothed_ = dsp::lerp(freezeSmoothed_, params_.freeze, 0.005f);

    float oldVal = grainMemory_[grainWritePos_];
    // Se freeze = 1.0, mantemos 100% de oldVal preservando a nuvem estática,
    // mas deixamos o ponteiro de gravação avançar para que os grãos girem.
    grainMemory_[grainWritePos_] = input * (1.0f - freezeSmoothed_) + oldVal * freezeSmoothed_;
    
    grainWritePos_ = (grainWritePos_ + 1) % grainMemorySize_;

    // Texture: Varredura de tamanho e densidade de 15ms a 400ms
    float grainLenMs = dsp::lerp(15.0f, 400.0f, params_.texture);
    float phaseFramesTotal = (grainLenMs / 1000.0f) * sampleRate_;
    if (phaseFramesTotal > grainMemorySize_ - 100) phaseFramesTotal = grainMemorySize_ - 100;
    
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
        if (p < increment) {
            grainJitter_[i] = prng_.randFloat() * params_.texture * 45.0f; // Jitter máx 45ms
        }

        // Janela Parabólica Otimizada (Cheap e suave como Cosine) -> 4 * p * (1 - p)
        float window = 4.0f * p * (1.0f - p);

        // Onde ler? Poffset(overlap) + Jitter + FreezeDrift
        float readMs = (p * phaseFramesTotal * (1000.0f/sampleRate_)) + (i * 13.0f) + grainJitter_[i] + driftMs;
        float readFrames = readMs * (sampleRate_ / 1000.0f);
        
        float readPos = static_cast<float>(grainWritePos_) - readFrames;

        while (readPos < 0.0f) readPos += grainMemorySize_;
        while (readPos >= grainMemorySize_) readPos -= grainMemorySize_;

        size_t idx1 = static_cast<size_t>(readPos);
        size_t idx2 = (idx1 + 1) % grainMemorySize_;
        float frac = readPos - static_cast<float>(idx1);

        float sample = dsp::lerp(grainMemory_[idx1], grainMemory_[idx2], frac);
        
        // Espalhamento L/R sutil por grão
        float panL = (i % 2 == 0) ? 0.7f : 0.3f;
        float panR = 1.0f - panL;

        accL += sample * window * panL;
        accR += sample * window * panR;
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

    // 1. Excitação Mono Interna
    float monoIn = (inL + inR) * 0.5f * params_.inputGain;
    dsp::sanitize(monoIn);

    // LFOs (Calculados cedo para fornecer drift p/ motor Granular)
    float lfo1_val = lfo1_.process();
    float lfo2_val = lfo2_.process();

    // 2. Núcleo Granular Estéreo (Clouds-ish smear/freeze)
    float granOutL = 0.0f, granOutR = 0.0f;
    processGranular(monoIn, lfo1_val, granOutL, granOutR);

    // 3. Diffuser / Allpass Series (O núcleo Greyhole injeta Sum no Diffuser p/ smear)
    float diffCoef = dsp::lerp(0.1f, 0.75f, params_.diffusion);
    
    // Processamos apenas a média do estéreo aqui p/ reduzir CPU (Mono smear -> expansão em seguida)
    float diffSignalMono = ap2_.process(ap1_.process((granOutL + granOutR) * 0.5f, diffCoef), diffCoef);
#if CGV_NUM_ALLPASS > 2
    diffSignalMono = ap4_.process(ap3_.process(diffSignalMono, diffCoef), diffCoef);
#endif
    
    // Criamos a base injetável combinando o estéreo granular limpo + Diffusor Mono (pseudo-decorrelacionado)
    float diffInL = granOutL * 0.4f + diffSignalMono * 0.8f;
    float diffInR = granOutR * 0.4f - diffSignalMono * 0.8f;

    // 4. Rede Recirculante (Greyhole Wash Loop)

    // Size range: 10% a 95% do buffer total disponivel
    float maxDelayBase = static_cast<float>(mainDelaySize_) * 0.95f;
    float baseDelayTimeL = dsp::lerp(sampleRate_ * 0.05f, maxDelayBase, params_.size);
    float baseDelayTimeR = baseDelayTimeL * 0.81f; // Assimetria crucial em reverb
    
    // Modulação (drift) convertida para frames. Depth ~ 0 a 15ms
    float modFrames = params_.modDepth * 0.015f * sampleRate_; 
    float timeL = baseDelayTimeL + lfo1_val * modFrames;
    float timeR = baseDelayTimeR + lfo2_val * modFrames;

    // Puxa do delay (fundo do mar)
    float readL = delayL_.read(timeL);
    float readR = delayR_.read(timeR);

    // Damping intrínseco do ambiente
    readL = dampL_.process(readL);
    readR = dampR_.process(readR);
    
    // Highpass no Loop: Subtração do LP de ~120Hz p/ previnir lama graves
    readL = readL - hpFeedL_.process(readL);
    readR = readR - hpFeedR_.process(readR);
    
    // Difusão DENTRO do feedback (Acumula densidade como algoritmos Lexicon/Greyhole)
#if CGV_NUM_LOOP_ALLPASS > 0
    float inLoopDiff = diffCoef * 0.6f;
    readL = loopApL_.process(readL, inLoopDiff);
    readR = loopApR_.process(readR, inLoopDiff);
#endif

    // Injeção de volta à linha de atraso (CROSS-FEEDBACK Matrix L/R)
    float feedLoopL = diffInL + readR * params_.feedback;
    float feedLoopR = diffInR + readL * params_.feedback;

    // TODO: Implementar módulo Shimmer (+1 OCT pitch shifter).
    // Evitado temporariamente por restrições rigorosas de segurança/CPU.
    // feedLoopL += pitchShift(readL) * params_.shimmer;

    // Saturação musical protegendo O(INF) feedback blowout
    feedLoopL = dsp::softClip(feedLoopL);
    feedLoopR = dsp::softClip(feedLoopR);

    // Escreve novamente
    delayL_.write(feedLoopL);
    delayR_.write(feedLoopR);

    // 5. Tonalidade Global (Tilt EQ)
    // Mistura frações do difusor de entrada na cauda p/ colar ataques
    float wetL = readL + diffInL * 0.35f;
    float wetR = readR + diffInR * 0.35f;

    // Separa Low/High em 800Hz e remix com ganhos Tilt
    float lowL = toneL_.process(wetL);
    float lowR = toneR_.process(wetR);
    float highL = wetL - lowL;
    float highR = wetR - lowR;
    
    wetL = lowL * toneGainLow_ + highL * toneGainHigh_;
    wetR = lowR * toneGainLow_ + highR * toneGainHigh_;

    // Ganho de saída aplicado ao wet final
    wetL *= params_.outputGain;
    wetR *= params_.outputGain;
    
    // Equal Power Crossfading
    float finalL = (inL * gainDry_) + (wetL * gainWet_);
    float finalR = (inR * gainDry_) + (wetR * gainWet_);

    // Clip final final safety para os conversores do MCU
    outL = dsp::softClip(finalL);
    outR = dsp::softClip(finalR);
}

void CloudGreyVerb::processBlock(float* left, float* right, size_t numFrames) {
    if (!left || !right) return;
    for(size_t i = 0; i < numFrames; ++i) {
        processSample(left[i], right[i], left[i], right[i]);
    }
}
