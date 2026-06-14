#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace dsp {

constexpr float PI = 3.14159265358979323846f;

// Saturação (Limiter) rápido para reverb tails. Deve ser transparente em sinais baixos.
inline float softClip(float x) {
    // Curva cúbica rápida que limpa a região < 0.5 e satura suavemente em 1.0
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
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

// Prevenção contra números denormais (evita picos extremos de CPU)
inline void sanitize(float& val) {
    // 1e-15 é seguro para floats de 32 bits
    if (fabsf(val) < 1e-15f) val = 0.0f;
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
        float tri = 4.0f * fabsf(phase_ - 0.5f) - 1.0f;
        // Aproximação polinomial para senoidez (opcional, aqui mantemos triângulo para CPU leve)
        return tri;
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

    // Leitura com interpolação linear para suportar delays modulados fracionários
    float read(float delayFrames) const {
        if (!buffer_ || size_ == 0 || delayFrames != delayFrames) return 0.0f; // Check buffer and NaN

        float readPos = static_cast<float>(writePos_) - delayFrames;
        float fSize = static_cast<float>(size_);
        
        // Wrap-around mais leve (presumindo no máximo um wraparound normal por sample)
        if (readPos < 0.0f || readPos >= fSize) {
            readPos = fmodf(readPos, fSize);
            if (readPos < 0.0f) readPos += fSize;
        }

        size_t idx1 = static_cast<size_t>(readPos);
        size_t idx2 = idx1 + 1;
        if (idx2 >= size_) idx2 = 0;
        float frac = readPos - static_cast<float>(idx1);

        float val = lerp(buffer_[idx1], buffer_[idx2], frac);
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
        feedback = hardClip(feedback); // Evita runaway feedback em Allpass series
        
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
