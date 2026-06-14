# CloudGreyVerb - Migração e Integração no STM32H5

O projeto foi atualizado para mirar nativamente a arquitetura do **STM32H5 (Cortex-M33F)** para uso em pedais de efeito de guitarra e contrabaixo.

## 1. O que mudou musicalmente

**Textura Granular e Nuvem Congelada (Clouds-like):**
- Expandida a mistura de janelas parabólicas de 3 para até 4 grãos sobrepostos.
- Jitter randômico e drift de posição injetados para dar "vida" à nuvem congelada. Menos loop estático e mais "frozen pad orgânico".
- Transição suavizada do *Freeze*.

**Feedback Dinâmico Difuso (Greyhole-like):**
- Adicionados 2 Allpasses *dentro* do loop de injeção de feedback L/R (disponível no perfil `BALANCED` ou `HIGH_QUALITY`). Cada volta do sinal fica mais embaçada, espessa e densa.
- Modulação nos frames de leitura criando wash analógico sem cliques.

**Adaptação P/ Baixo e Guitarra:**
- Filtro Highpass no feedback: Corta o lodo e *boomness* de notas graves (fundeado a ~120 Hz). Deixa o Dry reinar nas frequências de ataque.
- Nova equalização Tilt musical substituiu o damping destrutivo do core inicial. Mais corpo, mais brilho, menos "filtro subaquático".

*(O "Shimmer" ainda é mantido como `#define CGV_ENABLE_SHIMMER 0` e `TODO` devido a limitações de ciclo de clock para pitch shifting de alta qualidade e delay network na pipeline atual).*

## 2. Recomendações de Compilação (Perfis)

A fim de balancear uso da MCU e FPU, foram inseridos perfis. Defina a macro desejada em tempo de compilação:

* `-DCLOUD_GREY_PROFILE_H5_BALANCED=1` : 4 grãos, 4 diffusion allpasses, 2 feedback loop allpasses. Recomendado como padrão.
* `-DCLOUD_GREY_PROFILE_H5_LOW_CPU=1` : 3 grãos, 2 diffusion allpasses, sem allpass no loop de delay. Para quando adicionar outros módulos pesados na mesma MCU.
* `-DCLOUD_GREY_PROFILE_H7_HIGH_QUALITY=1` : Mesmos specs que balanced, com portas abertas para habilitar cross-modulation ou shimmer (habilitar shimmer flag manual).

### Flags de Compilação & Cortex-M33F
O STM32H5 utiliza um Cortex-M33 com FPU (Single Precision). Recomendações para GCC:
- `-mthumb -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16`
- `-O3` (altamente recomendado).
- `-ffast-math` ou `-Ofast`: **Aviso:** Pode causar problemas de estabilidade no feedback e propagação de NaNs/Infs devido à falta de checagens da norma IEEE754. Valide a estabilidade rigorosamente se usar `-Ofast`. O uso seguro prefere `-O3 -fno-math-errno`.
- Garanta que todos os literais no código-fonte possuem o sufixo `f` (ex: `0.5f`) para evitar chamadas de conversão obscuras por casting implícito de `double`.

## 3. Estimativa de RAM e Posicionamento na SRAM

A API do DSP confia inteiramente ao usuário um buffer `float*` pré-alocado. **Importante: Este NÃO é o seu buffer de DMA/Áudio. Este é o mega-buffer de memória estática onde residem todas as *delay lines*, *allpasses* e a memória granular rotativa do reverb.** 

### Tempo Equivalente Mono VS RAM Mínima
Para a taxa padrão de **48 kHz float (32-bit/4 bytes)**:
* 1 Segundo de memória base = 192 KB.
* Devido a repartição granular e espalhamento LR, este delay base de 1s soa mais como um "Medium Room".
* Para pads espaciais massivos, aponte para **> 2s de memória (384 KB até 576 KB RAM)**.

### Mapeamento Físico de Memória (Memória Persistente)
STM32H5 não possui a AXI SRAM ou DTCM específicas do H7, mas sim blocos SRAM1, SRAM2 e SRAM3 rápidos. Tente mapear o mega-buffer do reverb na região contígua mais veloz disponível usando atributos de linker (ex: `__attribute__((section(".sram1")))`). 
Evite colocar esse buffer persistente de delay em memórias PSRAM/SPI externas, pois a leitura fracionária aleatória dos *allpasses* destrói a eficiência do cache e eleva a latência a pontos insustentáveis no áudio de tempo real.

## 4. Integração com I2S/DMA (Buffer Menor)

O núcleo de processamento do CloudGreyVerb se mantém agnóstico a I/O (*core puro de ponteiros float*). A conversão entre os formatos e tamanhos do barramento do Codec de áudio deve residir em um container superior na arquitetura do firmware.

Para manter a latência o menor possível (ex: pedal em série para baixistas), recomenda-se buffers DMA na ordem de 16, 32 ou 48 frames. **Evite severamente alocação na *stack* (como VLAs via `float temp[size]`) dentro da rotina de interrupção.**

**Exemplo Seguro e Wrapper I2S (Sample-by-Sample com Interleaved Int32):**
```cpp
// 1. Instanciação do buffer de MEMÓRIA PERSISTENTE DO REVERB (ex. SRAM2)
__attribute__((section(".sram2"))) static float reverbMemory[48000 * 2]; // ~384 KB (2 segs)

CloudGreyVerb reverbFx;
CloudGreyVerb::Params currentParams;

void initAudio() {
    reverbFx.init(48000.0f, reverbMemory, sizeof(reverbMemory)/sizeof(float));
    reverbFx.setParams(CloudGreyVerb::getPreset(CloudGreyVerb::Mode::BalancedCloud));
}

// 2. Callback Direto de DMA/I2S - Embutindo a Saturação e Conversão
// Processando bloco Interleaved (L, R, L, R, L, R...) para evitar arrays na stack
void I2S_Callback(int32_t* inBuf, int32_t* outBuf, size_t numFrames) {
    const float scaleIn  = 1.0f / 8388608.0f; // Ex. de conversão p/ 24 bits em frame 32bit
    const float scaleOut = 8388608.0f;
    
    for(size_t i = 0; i < numFrames; i++) {
        // Conversão Codec -> Float Core
        float floatL = (float)inBuf[2*i]     * scaleIn;
        float floatR = (float)inBuf[2*i + 1] * scaleIn;

        // Processamento direto amostra-a-amostra
        reverbFx.processSample(floatL, floatR, floatL, floatR);

        // Saturação final (Prevenção de wraparound extremo que vira ruído forte no DAC)
        if (floatL > 1.0f) floatL = 1.0f;
        else if (floatL < -1.0f) floatL = -1.0f;
        
        if (floatR > 1.0f) floatR = 1.0f;
        else if (floatR < -1.0f) floatR = -1.0f;

        // Conversão Float Core -> Codec
        outBuf[2*i]     = (int32_t)(floatL * scaleOut);
        outBuf[2*i + 1] = (int32_t)(floatR * scaleOut);
    }
}
```

## 5. Atualização Segura de Parâmetros Fora do Callback

A aplicação principal (UI, encoders, MIDI) não deve acessar a engine simultaneamente à interrupção de áudio. Utilizar `setParams()` dentro do I2S_Callback partindo de variáveis globais pode corromper dados se preempções ocorrerem no meio de conversões `float`.

A estratégia de Double-Buffering (Cópia Atômica) ou controle de Mutexes / Flags *Lock-Free* é exigida para trocar predefinições via FreeRTOS / Loop de base:

```cpp
// Padrão Thread-Safe simples:
volatile bool paramsChanged = false;
CloudGreyVerb::Params newParams;

// Main / UI Thread
void updateUI() {
    newParams.mix = readPot(0);
    newParams.size = readPot(1);
    paramsChanged = true; // Informa a thread I2S
}

// DMA Thread (no início do I2S_Callback anterior):
if (paramsChanged) {
    reverbFx.setParams(newParams);
    paramsChanged = false;
}
```

## 6. UI de Pedal Físico (STM32H5) Mapping Sugerido

Se estiver montando fisicamente o dispositivo ou UI de prototipagem:
* **POT 1 - Mix:** Dry/wet master control.
* **POT 2 - Texture:** Curto, slap smear a pad espalhado.
* **POT 3 - Size:** Aumenta base delay lines do Greyhole L/R.
* **POT 4 - Feedback:** Decay do ambiente infinito. (No maximo bate em 0.98 de absorção para nao clipar o conversor).
* **POT 5 - Tone:** Escurece a cauda ou atenua sub/graves do reverb.
* **Switch / Botão:** Alterna os modos `Mode` (veja metodo `getPreset(Mode)` interno para pré-redefinições de all-pass).
* **Footswitch (Momentary):** *Freeze*. Troca os frames correntes p/ circular fixo na granular engine. Atue o input "Freeze" enviando 1.0f para ligar, 0.0f desligar, o fade é tratado internamente.
