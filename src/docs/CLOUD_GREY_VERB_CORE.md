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

## Mecanismos de Segurança e Estabilidade

**Safety Energy Guard (v2)**
Para evitar crescimento explosivo (runaway feedback) na rede do Greyhole – um problema comum em matrizes recirculantes –, implementamos um Energy Guard lento:
1. Ele mensura a energia quadrada `L^2 + R^2` injetada de volta nos delays.
2. Com um filtro passa-baixa (LPF) extremamente lento (`0.9995`), ele rastreia a RMS do loop em janelas longas.
3. Se a energia excede `0.45`, ele calcula um ganho de redução (Safety Gain) para trazer o loop de volta aos níveis sadios.
4. O valor máximo de clipping do limiteur também atua secundariamente via `dsp::softClip()` em allpasses e na saída do loop.
Você pode visualizar a atuação deste limitador verificando o valor *Safety Gain* na UI do testador WASM.

**Por que o preset "DarkLongCloud" não é infinito real?**
Feedback > 0.94 foi restrito para garantir zero clips e evitar matemática infinita na matriz de atraso (Wash Loop). Além disso, damping contínuo consome a energia do sinal, e o freeze mode é a técnica principal sugerida para prender texturas infinitas em vez de apenas contar com o delay path de `feedback = 0.99`.

## Testando Freeze em Ambiente WASM (Render Demo)
Como o buffer de gravação WASM roda offline processando toda a trilha no botão *Process Audio (Offline)*, o Freeze manual não captura o loop "naquele exato momento" tocando na UI.
Criamos o **Render Freeze Demo**:
1. Clique em `❄️ Render Freeze Demo`.
2. O código processará o arquivo do segundo `0` ao `2` em estado normal (bypass The Freeze).
3. Do segundo `2` ao `6`, ele ativará o `Freeze` virtualmente no motor.
4. Você escutará o engarrafamento granular congelado de 4 segundos e natural "decay" logo depois.

## Nota sobre Damping
O controle de `Damping` atua quase como um "Feedback Brightness" natural para a sala física simulada: em valores baixos, a sala se torna densa (Dark), reduzindo drasticamente os agudos em poucos taps do loop; em valores altos, os agudos persistem com nitidez no smearing.

---

## O Que Ficou "Para Depois" (Próximas Atualizações)

Com intenção de finalizar um Core rígido de alta robustez, as exclusões a seguir têm justificativa em escopo:
- **Shimmer Real:** O parâmetro `shimmer` está no pacote, mas o módulo pitch-shifter de +1 OCTAVE não está conectado ainda, aguardando validação de CPU/Memória na MCU.
- **AudioWorklet / Live WASM:** O Applet Web usa o motor de forma *Offline Block Rendering*. A futura implementação Live exigirá uma thread AudioWorklet JavaScript conectada ao microfone com ring-buffers independentes.
- **FDN 4x4:** A arquitetura do Greyhole é baseada em Allpass Loop estéreo. A expansão conceitual para uma verdadeira Feedback Delay Network (Matriz Ortogonal 4x4) será tratada futuramente se pedida.
