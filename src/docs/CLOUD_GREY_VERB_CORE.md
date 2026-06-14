# CloudGreyVerb - Núcleo DSP Finalizado (Beta Testing)

O core `CloudGreyVerb` está preparado para testes de áudio reais em ambiente Host ou Embarcado (como o STM32H5).

## Exemplo de Instanciação Mínima

Este é um formato agnóstico compatível com frameworks de hardware ou C++ limpo de desktop:

```cpp
#include "cloud_grey_verb.hpp"

// 1. Alocação (Global/Estática para evitar a heap em tempo real)
static float cloudGreyMemory[48000 * 2]; // 384 KB

CloudGreyVerb fx;

void setup() {
    // 2. Inicialização segura
    fx.init(48000.0f, cloudGreyMemory, 48000 * 2);

    // 3. Seleção do Preset
    fx.setParams(CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BassAmbientWash));
}

// 4. Inserção no Audio Callback Loop (Por frame L/R em buffer Interleaved por ex.)
void process(float* l, float* r, size_t frames) {
    fx.processBlock(l, r, frames);
}
```

---

## Checklist de Escuta Crítica (RT Audio)

Sugerimos focar na audição musical para validar a transição teórica -> prática.

**Teste 1: `AlwaysOnSubtle`**
- Confirme a relação Dry/Wet igual potência.
- Modifique os knobs em tempo real — confirme a ausência de Zipper Noise ou Clicks (tudo é levemente suavizado na matemática core).
- Quando no Mínimo, bypass suave é ativado (apenas o sinal local da fase de secagem).

**Teste 2: `SmallCloudRoom`**
- Toque acordes rápidos ou staccatos em guitarra. 
- O wet deve aparecer rapidamente colado à transição com um smear granular quase mecânico, e não apenas replicar o delay no fundo solto do ar.

**Teste 3: `BassAmbientWash`**
- Toque acordes no registro Sub/Grave (Baixo ativo/passivo ~40 a 100hz).
- Sinta os Highpasses integrados na rede do delay, isso evitará a temida *"Lama Sônica"* ou cancelamentos de graves prejudiciais à base.
- Ajuste temporário do `Tone` para verificar o *dark/bright mix tilt*.

**Teste 4: `GreyholeDelayVerb`**
- Use tempo generoso no `Feedback` (chegando a >0.85). Em C++, foi grampeado intencionalmente em `0.98` na base para evitar blowout mortal. Confirme estabilidade sem clipping/NaNs e ouça o chorusing gerado.

**Teste 5: `FrozenOrganPad`**
- Utilize um Footswitch digital no painel com debounce, ou um Switch UI ativando a Flag `freeze` para 1.0f e vice-versa.
- Não há loops infinitos cliquentos do reverb estático, mas um "freeze smoothed"; perceba que a malha de granular tracking não paralisa 100%, em vez disso, o áudio base fica contido mantendo-se musical e vivo.

**Teste 6: `GlitchSmear`**
- Verifique os micro grãos (jitter) quase engasgando o motor com curtos buffers. Um modo mais Lo-Fi para ruídos/synths, confirmando ausências de transientes estourados indesejáveis.

---

## O Que Ficou "Para Depois" (Futuras Atualizações)

Com intenção de finalizar um Core rígido de alta robustez, as exclusões a seguir têm justificativa em escopo:
- **Shimmer Real:** Placeholder `shimmer` parameter está nulo temporariamente (`TODO`); implementar Pitch-shifting fracionário limpo em tempo de áudio precisa de mais calibração na contagem de ciclos/CPU de STMs sem afundar do bloco limitador FPU.
- **WASM:** Adicionaria complexidade externa de build toolchain a esta fase.
- **Wrapper STM32 completo:** A integração I2S e DMA específica ficou de fora do Header agnóstico. Cada placa precisará rotear sua `HAL_I2S_Receive_DMA`.
- **Editor de Presets Externos UI** e **Profilling Específico.**
