#include <iostream>
#include <cmath>
#include "cloud_grey_verb.hpp"

// Alocação estática segura
static float externalMemory[48000 * 3]; // 3 segundos mono equivalentes

int main() {
    CloudGreyVerb fx;
    
    // 1. Inicializa
    fx.init(48000.0f, externalMemory, 48000 * 3);
    
    // 2. Define o preset seguro inicial
    fx.setParams(CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BassAmbientWash));
    
    float outL 
= 0.0f, outR = 0.0f;
    
    // 3. Processa silêncio e verifica estabilidade
    fx.processSample(0.0f, 0.0f, outL, outR);
    if (std::isnan(outL) || std::isnan(outR) || std::isinf(outL) || std::isinf(outR)) {
        std::cerr << "FAIL: NaN/Inf detectado no silêncio." << std::endl;
        return 1;
    }
    
    // 4. Injeta pulso de 1 frame e acompanha a cauda 
    fx.processSample(1.0f, 1.0f, outL, outR);
    
    float maxPico = 0.0f;
    // Roda 1 segundo de silêncio para ouvir o rastro
    for(int i = 0; i < 48000; i++) {
        fx.processSample(0.0f, 0.0f, outL, outR);
        if (std::isnan(outL) || std::isnan(outR)) {
            std::cerr << "FAIL: NaN/Inf no frame " << i << std::endl;
            return 1;
        }
        if (std::abs(outL) > maxPico) maxPico = std::abs(outL);
        if (std::abs(outR) > maxPico) maxPico = std::abs(outR);
    }
    
    std::cout << "Pico máximo do Reverb após o impulso: " << maxPico << std::endl;
    
    // 5. Hard Reset e check de estabilidade térmica de memória
    fx.reset();
    fx.processSample(0.0f, 0.0f, outL, outR);
    if (std::isnan(outL) || std::isnan(outR)) {
        std::cerr << "FAIL: NaN/Inf após reset!" << std::endl;
        return 1;
    }
    
    std::cout << "SUCCESS: Nuvem limpa! Smoke test finalizado com estabilidade plena." << std::endl;
    
    return 0;
}
