#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace dsp {

constexpr float PI = 3.14159265358979323846f;

// Saturação (Limiter) rápido para reverb tails. Deve ser transparente em sinais baixos.
inline float softClip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;

    float x2 = x * x;
    float y = x * (27.0f + x2) / (27.0f + 9.0f * x2);

    if (y > 1.0f) return 1.0f;
    if (y < -1.0f) return -1.0f;
    return y;
}

// Tape Saturation: Polinômio para saturação magnética quente
inline float tapeClip(float x) {
    // Adiciona pequeno offset DC para harmônicos pares (assimetria térmica leve)
    float xp = x + 0.05f; 
    
    // Clipper cúbico suave: out = x - (x^3)/3
    if (xp > 1.0f) xp = 1.0f;
    else if (xp < -1.0f) xp = -1.0f;
    
    float x2 = xp * xp;
    float out = xp - (x2 * xp) * 0.333333f;
    
    // Remove o DC (offset rest em ~0.04995)
    out -= 0.049958f;
    
    // Normalizar nível percebido perto de 1 (já cruncheia na fita)
    return out * 1.5f;
}

inline float hardClip(float x) {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

// Gerador Aleatorio Leve (Xorshift32) para jitter - s/ alocação e super rápido
class FastPRNG {
public:
    void seed(uint32_t s) { state_ = s ? s : 1; }
    uint32_t rand() {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    // Retorna ~0.0 a 1.0
    float randFloat() {
        return static_cast<float>(rand() & 0xFFFFFF) / 16777216.0f;
    }
private:
    uint32_t state_ = 1;
};

// Interpolação linear rápida
inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Utilitários para encontrar números primos (útil para Schroeder reverbs)
inline bool isPrime(size_t n) {
    if (n <= 1) return false;
    if (n <= 3) return true;
    if (n % 2 == 0 || n % 3 == 0) return false;
    for (size_t i = 5; i * i <= n; i += 6) {
        if (n % i == 0 || n % (i + 2) == 0) return false;
    }
    return true;
}

inline size_t nextPrime(size_t n) {
    while (!isPrime(n)) {
        n++;
    }
    return n;
}

// Interpolação Hermite Cúbica para preservar altas frequências
inline float hermite(float frac, float p0, float p1, float p2, float p3) {
    float c = (p2 - p0) * 0.5f;
    float v = p1 - p2;
    float w = c + v;
    float a = w + v + (p3 - p1) * 0.5f;
    float b = w + a;
    return ((((a * frac) - b) * frac + c) * frac + p1);
}

// Prevenção contra números denormais (evita picos extremos de CPU)
inline void sanitize(float& val) {
    if (val != val) val = 0.0f;
    if (val > 1000.0f) val = 1000.0f;
    if (val < -1000.0f) val = -1000.0f;
    if (fabsf(val) < 1e-9f) val = 0.0f;
}

// LFO - Simple Triangle/Sine Approximation
class LFO {
public:
    void setRate(float rate, float sampleRate) {
        phaseInc_ = rate / sampleRate;
    }

    void clear() { phase_ = 0.0f; }

    float process() {
        phase_ += phaseInc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        if (phase_ < 0.0f || phase_ >= 1.0f) {
            phase_ = fmodf(phase_, 1.0f);
            if (phase_ < 0.0f) phase_ += 1.0f;
        }
        
        // Triângulo suave de -1 a 1
        return 4.0f * fabsf(phase_ - 0.5f) - 1.0f;
    }

    // Leitura multipolifásica (Decorrelação) s/ avançar fase
    float getValue(float phaseOffset) const {
        float p = phase_ + phaseOffset;
        if (p >= 1.0f) p -= 1.0f;
        if (p < 0.0f || p >= 1.0f) {
            p = fmodf(p, 1.0f);
            if (p < 0.0f) p += 1.0f;
        }
        return 4.0f * fabsf(p - 0.5f) - 1.0f;
    }

private:
    float phase_ = 0.0f;
    float phaseInc_ = 0.0f;
};

// Linha de atraso flexível baseada em buffer circular externo
class DelayLine {
public:
    void init(float* memory, size_t size) {
        buffer_ = memory;
        size_ = size;
        writePos_ = 0;
        for(size_t i = 0; i < size_; ++i) {
            buffer_[i] = 0.0f;
        }
    }

    void clear() {
        if (!buffer_ || size_ == 0) return;
        for(size_t i = 0; i < size_; ++i) buffer_[i] = 0.0f;
        writePos_ = 0;
    }

    void write(float sample) {
        if (!buffer_ || size_ == 0) return;
        buffer_[writePos_] = sample;
        writePos_++;
        if (writePos_ >= size_) writePos_ = 0;
    }

    // Leitura com interpolação Hermite para preservar altas frequências
    float read(float delayFrames) const {
        if (!buffer_ || size_ == 0 || delayFrames != delayFrames) return 0.0f; // Check buffer and NaN

        // Restringir delay: minimo de 1 sample (já que usamos write após read se feedback for usado, ou vice-versa, evitar ler o frame futuro).
        // Máximo é o tamanho inteiro do buffer.
        float fDelayed = fmaxf(1.0f, fminf(static_cast<float>(size_), delayFrames));

        float readPos = static_cast<float>(writePos_) - fDelayed;
        float fSize = static_cast<float>(size_);
        
        // Wrap-around mais leve
        if (readPos < 0.0f || readPos >= fSize) {
            readPos = fmodf(readPos, fSize);
            if (readPos < 0.0f) readPos += fSize;
        }

        size_t idx1 = static_cast<size_t>(readPos);
        float frac = readPos - static_cast<float>(idx1);

        size_t idx0 = (idx1 == 0) ? size_ - 1 : idx1 - 1;
        size_t idx2 = (idx1 + 1 >= size_) ? 0 : idx1 + 1;
        size_t idx3 = (idx2 + 1 >= size_) ? 0 : idx2 + 1;

        float val = hermite(frac, buffer_[idx0], buffer_[idx1], buffer_[idx2], buffer_[idx3]);
        if (val != val) val = 0.0f; // Fix NaNs leaking
        return val;
    }

    size_t getSize() const { return size_; }

private:
    float* buffer_ = nullptr;
    size_t size_ = 0;
    size_t writePos_ = 0;
};

// Allpass Filter - usado para a rede de difusão (Smear / Densidade)
class Allpass {
public:
    void init(float* memory, size_t size) {
        delay_.init(memory, size);
    }

    void clear() { delay_.clear(); }

    float process(float input, float g) {
        // Leitura do atraso máximo
        float delayed = delay_.read(static_cast<float>(delay_.getSize()) - 1.0f);
        
        // Estrutura Allpass padrão Schroeder
        float feedback = input + delayed * g;
        if (feedback != feedback) feedback = 0.0f; // NaN Protection
        sanitize(feedback);
        feedback = softClip(feedback); // Saturação musical protegendo O(INF) feedback em Allpass series
        
        delay_.write(feedback);
        
        float out = -input * g + delayed;
        if (out != out) out = 0.0f;
        return out;
    }

    float processModulated(float input, float g, float modSamples) {
        // modSamples deve variar entre +- poucos samples
        float delayT = static_cast<float>(delay_.getSize()) - 1.5f + modSamples;
        if (delayT < 1.0f) delayT = 1.0f;
        float delayed = delay_.read(delayT);
        
        float feedback = input + delayed * g;
        if (feedback != feedback) feedback = 0.0f;
        sanitize(feedback);
        feedback = softClip(feedback);
        
        delay_.write(feedback);
        
        float out = -input * g + delayed;
        if (out != out) out = 0.0f;
        return out;
    }
private:
    DelayLine delay_;
};

// Filtro One-Pole RC para Damping/Tone
class OnePoleRC {
public:
    void setFreq(float freq, float sr) {
        float w = 2.0f * PI * freq / sr;
        // Evitar instabilidade
        if (w > 1.0f) w = 1.0f; 
        
        a0_ = w / (1.0f + w);
        b1_ = 1.0f - a0_;
    }

    void clear() { z_ = 0.0f; }

    float process(float in) {
        if (in != in) in = 0.0f; // Input NaN
        z_ = in * a0_ + z_ * b1_;
        if (z_ != z_) z_ = 0.0f; // Filter Blow up NaN
        sanitize(z_);
        return z_;
    }
private:
    float a0_ = 1.0f;
    float b1_ = 0.0f;
    float z_ = 0.0f;
};

} // namespace dsp
