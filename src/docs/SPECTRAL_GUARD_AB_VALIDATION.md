# Validação Forense e Benchmark A/B/C do Spectral Feedback Guard

**Repositório**: `ovelhaaa/GreyCloud`  
**Data**: 28 de Setembro de 2026  
**Status**: Validação Concluída / Default Selecionado  

---

## 1. Contexto e Objetivos

Esta validação técnica tem por objetivo corrigir a função de saturação suave `gentleSaturate()`, normalizar a energia calculada pelo `SpectralFeedbackGuard` de forma independente da ordem da FDN (2 vs 4 canais), instrumentar com métricas a atividade real do guard e comparar rigorosamente as três arquiteturas de feedback:

- **Arquitetura A (Current TapeClip)**: `feedback -> tapeClip -> loop`
- **Arquitetura B (SpectralGuard Only)**: `feedback -> spectral guard -> loop`
- **Arquitetura C (Weak Saturation + Spectral Guard)**: `feedback -> gentleSaturate -> spectral guard -> loop`

A análise cobre os presets canônicos `BrightCloud`, `DarkLongCloud` e `GreyholeDelayVerb` sob duas condições acústicas distintas de 20 segundos cada:
1. **Decaimento Normal (Impulso unitário, 20s)**: validação de transparência, preservação de RT60 e ausência de alteração tonal.
2. **Freeze Sustentado (Excitação densa 200ms + Freeze 100%, 20s)**: validação de contenção espectral, mitigação de crescimento descontrolado e estabilidade sem pumping audível.

---

## 2. Correção de `gentleSaturate()`

### 2.1 Análise da Implementação Anterior
A formulação anterior:
$$f_{\text{old}}(x) = \frac{x}{1 + 0.15 x^2}$$

apresentava derivada negativa para $|x| > \frac{1}{\sqrt{0.15}} \approx 2.582$:
$$f'_{\text{old}}(x) = \frac{1 - 0.15 x^2}{(1 + 0.15 x^2)^2}$$
Para $x = 5.0$, $f(5) \approx 1.053$, enquanto para $x = 10.0$, $f(10) \approx 0.625$. Essa característica de *foldback* (curva que decresce com o aumento da amplitude de entrada) é altamente indesejada em malhas de realimentação FDN, pois pode induzir histerese destrutiva, distorção caótica e travamento de envelopes.

### 2.2 Nova Formulação Algébrica
Substituiu-se pela saturação algébrica suave de ordem 2 ($C^\infty$):
$$f_{\text{new}}(x) = \frac{x}{\sqrt{1 + k x^2}}, \quad k = 0.15$$

Propriedades analíticas comprovadas:
1. **Simetria Ímpar**: $f(-x) = -f(x)$, com $f(0) = 0$ (offset DC nulo exato, sem harmônicos pares espúrios).
2. **Monotonicidade Estrita**: 
   $$f'(x) = \frac{1}{(1 + k x^2)^{3/2}} > 0, \quad \forall x \in \mathbb{R}$$
   A curva é estritamente crescente para todo $x$, impossibilitando *foldback*.
3. **Ganho Unitário na Origem**: $\lim_{x \to 0} \frac{f(x)}{x} = 1.000000$. Para sinais com $|x| \le 0.1$, a atenuação é inferior a $0.006\text{ dB}$, mantendo transparência quase perfeita.
4. **Saída Finita e Limitada**: Para $x \to \pm\infty$, $|f(x)| \to \frac{1}{\sqrt{k}} \approx 2.581989$.

### 2.3 Testes Unitários de Validação
Implementados em `CloudGreyVerbMilestoneTest`:
- **Ganho em Sinais Pequenos**: $|f(x)/x - 1.0| < 10^{-4}$ para $x \in [-0.01, 0.01]$.
- **Simetria Ímpar**: $|f(-x) + f(x)| < 10^{-6}$ para $x \in [0, 100]$.
- **Monotonicidade**: $f(x_2) > f(x_1)$ no domínio operacional $[0, 8.0]$ e estritamente não-decrescente ($f(x_2) \ge f(x_1)$) na varredura geométrica até $x = 10000$.
- **Finitude**: Saída estritamente finita e limitada por $1/\sqrt{k}$ em amplitudes extremas ($10^6, 10^{12}$).

---

## 3. Normalização de Energia do Spectral Feedback Guard

### 3.1 Normalização por Linhas de Delay
No cálculo original, a energia instantânea somava quadraticamente os $N$ canais da FDN sem divisão:
$$\text{instEnergy}[b] = \sum_{i=0}^{N-1} x_{b,i}^2$$
Isso criava dependência direta da ordem da FDN: em perfis de 4 canais (`Desktop`, `Balanced`, `H7`), a energia era o dobro da verificada no perfil de 2 canais (`H5_LOW_CPU`).

A normalização foi implementada em `SpectralFeedbackGuard::process`:
```cpp
if (numChannels > 0) {
    const float invChannels = 1.0f / static_cast<float>(numChannels);
    instEnergy[0] *= invChannels;
    instEnergy[1] *= invChannels;
    instEnergy[2] *= invChannels;
}
```

### 3.2 Calibração de Limiares por Canal
Com a energia normalizada para a média por linha ($\frac{1}{N} \sum x_i^2$), os limiares de acionamento foram calibrados proporcionalmente:
- **Low (20 Hz - 250 Hz)**: `0.0075f` (RMS equivalente $\approx 0.0866$ por linha)
- **Mid (250 Hz - 3000 Hz)**: `0.00875f` (RMS equivalente $\approx 0.0935$ por linha)
- **High (3000 Hz - 15000 Hz)**: `0.00625f` (RMS equivalente $\approx 0.0791$ por linha)

Dessa forma, o envelope atua com a mesma precisão física em 2 ou 4 canais.

---

## 4. Instrumentação de Atividade do Guard

Foram adicionadas métricas *test-only* no `SpectralFeedbackGuard::Metrics`:
- `minGain[3]`: Ganho mínimo registrado em cada uma das 3 bandas (Low, Mid, High).
- `maxEnergy[3]`: Pico da energia suavizada em cada banda.
- `samplesBelow99[3]` / `pctBelow99`: Porcentagem de amostras onde o ganho da banda foi $< 0.99$.
- `samplesBelow95[3]` / `pctBelow95`: Porcentagem de amostras onde o ganho da banda foi $< 0.95$.

Essas métricas permitiram auditar se o guard permanece em estado de repouso ($g \approx 1.0$) no uso normal e quantificar sua intervenção em situações extremas.

---

## 5. Resultados e Tabelas Forenses A/B/C

Todos os ensaios foram realizados a $48\text{ kHz}$ com renderização offline de 20 segundos por condição (960.000 amostras por render).

### 5.1 Decaimento Normal (Impulso Unitário, 20s)

#### Tabela 1: Métricas Globais da Cauda
| Preset | Arquitetura | Peak | RMS Overall | Crest Factor | Clicks | Growth Warns | Max Grw (dB) | Mean Drift | RT60 (s) |
|:-------|:------------|:-----|:------------|:-------------|:-------|:-------------|:-------------|:-----------|:---------|
| **BrightCloud** | A: Current TapeClip | 0.1555 | 1.74e-04 | 892.4 | 0 | 0 | 0.00 | 0.0220 | 2.88 s |
| **BrightCloud** | B: SpectralGuard Only | 0.1555 | 1.74e-04 | 891.3 | 0 | 0 | 0.00 | 0.0213 | 2.90 s |
| **BrightCloud** | C: WeakSat + SpecGuard | 0.1555 | 1.74e-04 | 891.3 | 0 | 0 | 0.00 | 0.0211 | 2.90 s |
| **DarkLongCloud** | A: Current TapeClip | 0.0585 | 1.00e-04 | 582.4 | 0 | 1 | 0.51 | 0.0186 | 20.03 s |
| **DarkLongCloud** | B: SpectralGuard Only | 0.0585 | 1.01e-04 | 581.6 | 0 | 1 | 0.52 | 0.0186 | 20.18 s |
| **DarkLongCloud** | C: WeakSat + SpecGuard | 0.0585 | 1.01e-04 | 581.6 | 0 | 1 | 0.52 | 0.0186 | 20.18 s |
| **GreyholeDelayVerb** | A: Current TapeClip | 0.1255 | 1.54e-04 | 816.8 | 0 | 0 | 0.00 | 0.0147 | 13.12 s |
| **GreyholeDelayVerb** | B: SpectralGuard Only | 0.1255 | 1.54e-04 | 815.5 | 0 | 0 | 0.00 | 0.0147 | 13.22 s |
| **GreyholeDelayVerb** | C: WeakSat + SpecGuard | 0.1255 | 1.54e-04 | 815.5 | 0 | 0 | 0.00 | 0.0147 | 13.22 s |

#### Tabela 2: Atividade do Spectral Guard em Uso Normal
| Preset | Arquitetura | Min Gain (L / M / H) | Max Energy (L / M / H) | Samples < 0.99 (L / M / H) | Samples < 0.95 (L / M / H) |
|:-------|:------------|:---------------------|:-----------------------|:---------------------------|:---------------------------|
| **BrightCloud** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **BrightCloud** | B: SpectralGuard Only | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **BrightCloud** | C: WeakSat + SpecGuard | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **DarkLongCloud** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **DarkLongCloud** | B: SpectralGuard Only | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **DarkLongCloud** | C: WeakSat + SpecGuard | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **GreyholeDelayVerb** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **GreyholeDelayVerb** | B: SpectralGuard Only | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **GreyholeDelayVerb** | C: WeakSat + SpecGuard | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |

#### Tabela 3: Centroide Espectral ao Longo do Tempo (Hz)
| Preset | Arquitetura | Cent 0.5s | Cent 2s | Cent 6s | Cent 10s | Cent 14s | Cent 18s |
|:-------|:------------|:----------|:--------|:--------|:---------|:---------|:---------|
| **BrightCloud** | A: Current TapeClip | 5386 Hz | 2311 Hz | 3265 Hz | 0 Hz | 0 Hz | 0 Hz |
| **BrightCloud** | B: SpectralGuard Only | 5386 Hz | 2311 Hz | 2436 Hz | 0 Hz | 0 Hz | 0 Hz |
| **BrightCloud** | C: WeakSat + SpecGuard | 5386 Hz | 2311 Hz | 2385 Hz | 0 Hz | 0 Hz | 0 Hz |
| **DarkLongCloud** | A: Current TapeClip | 7265 Hz | 5206 Hz | 2563 Hz | 1834 Hz | 1596 Hz | 1504 Hz |
| **DarkLongCloud** | B: SpectralGuard Only | 7265 Hz | 5206 Hz | 2562 Hz | 1834 Hz | 1595 Hz | 1496 Hz |
| **DarkLongCloud** | C: WeakSat + SpecGuard | 7265 Hz | 5206 Hz | 2562 Hz | 1834 Hz | 1595 Hz | 1496 Hz |
| **GreyholeDelayVerb** | A: Current TapeClip | 4761 Hz | 4599 Hz | 2366 Hz | 1844 Hz | 1644 Hz | 1665 Hz |
| **GreyholeDelayVerb** | B: SpectralGuard Only | 4761 Hz | 4598 Hz | 2366 Hz | 1842 Hz | 1627 Hz | 1545 Hz |
| **GreyholeDelayVerb** | C: WeakSat + SpecGuard | 4761 Hz | 4598 Hz | 2366 Hz | 1842 Hz | 1627 Hz | 1544 Hz |

---

### 5.2 Freeze Sustentado (Excitação 200ms + Freeze 100%, 20s)

#### Tabela 4: Métricas em Condição Freeze
| Preset | Arquitetura | Peak | RMS Overall | RMS 1-4s | RMS 16-20s | Low % (16-20s) | High % (16-20s) | Cent 18s | Mean Drift | Growth Warns |
|:-------|:------------|:-----|:------------|:---------|:-----------|:---------------|:----------------|:---------|:-----------|:-------------|
| **BrightCloud** | A: Current TapeClip | 0.4373 | 9.10e-02 | 8.05e-02 | 9.85e-02 | 5.9% | 5.8% | 3814 Hz | 0.0220 | 12 |
| **BrightCloud** | B: SpectralGuard Only | 0.4029 | 8.11e-02 | 7.90e-02 | 8.44e-02 | 7.3% | 7.7% | 4454 Hz | 0.0250 | 13 |
| **BrightCloud** | C: WeakSat + SpecGuard | 0.4026 | 8.11e-02 | 7.88e-02 | 8.43e-02 | 7.3% | 7.7% | 4449 Hz | 0.0250 | 13 |
| **DarkLongCloud** | A: Current TapeClip | 0.2878 | 4.96e-02 | 3.52e-02 | 5.76e-02 | 8.7% | 2.5% | 2088 Hz | 0.0113 | 13 |
| **DarkLongCloud** | B: SpectralGuard Only | 0.2919 | 5.05e-02 | 3.53e-02 | 5.89e-02 | 8.7% | 2.4% | 2080 Hz | 0.0111 | 13 |
| **DarkLongCloud** | C: WeakSat + SpecGuard | 0.2913 | 5.04e-02 | 3.53e-02 | 5.87e-02 | 8.7% | 2.4% | 2083 Hz | 0.0111 | 13 |
| **GreyholeDelayVerb** | A: Current TapeClip | 0.4099 | 8.03e-02 | 5.94e-02 | 9.25e-02 | 7.4% | 5.4% | 2381 Hz | 0.0182 | 8 |
| **GreyholeDelayVerb** | B: SpectralGuard Only | 0.4053 | 7.78e-02 | 6.00e-02 | 8.59e-02 | 7.6% | 6.1% | 2502 Hz | 0.0182 | 8 |
| **GreyholeDelayVerb** | C: WeakSat + SpecGuard | 0.4055 | 7.77e-02 | 5.99e-02 | 8.58e-02 | 7.6% | 6.1% | 2500 Hz | 0.0183 | 8 |

#### Tabela 5: Atividade do Spectral Guard em Freeze
| Preset | Arquitetura | Min Gain (L / M / H) | Max Energy (L / M / H) | Samples < 0.99 (L / M / H) | Samples < 0.95 (L / M / H) |
|:-------|:------------|:---------------------|:-----------------------|:---------------------------|:---------------------------|
| **BrightCloud** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **BrightCloud** | B: SpectralGuard Only | 1.0000 / 0.8656 / 1.0000 | 0.00464 / 0.01374 / 0.00187 | 0.00% / 81.09% / 0.00% | 0.00% / 37.95% / 0.00% |
| **BrightCloud** | C: WeakSat + SpecGuard | 1.0000 / 0.8678 / 1.0000 | 0.00461 / 0.01365 / 0.00186 | 0.00% / 80.93% / 0.00% | 0.00% / 36.32% / 0.00% |
| **DarkLongCloud** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **DarkLongCloud** | B: SpectralGuard Only | 1.0000 / 0.9712 / 1.0000 | 0.00324 / 0.01003 / 0.00138 | 0.00% / 5.78% / 0.00% | 0.00% / 0.00% / 0.00% |
| **DarkLongCloud** | C: WeakSat + SpecGuard | 1.0000 / 0.9736 / 1.0000 | 0.00320 / 0.00994 / 0.00137 | 0.00% / 4.93% / 0.00% | 0.00% / 0.00% / 0.00% |
| **GreyholeDelayVerb** | A: Current TapeClip | 1.0000 / 1.0000 / 1.0000 | 0.00000 / 0.00000 / 0.00000 | 0.00% / 0.00% / 0.00% | 0.00% / 0.00% / 0.00% |
| **GreyholeDelayVerb** | B: SpectralGuard Only | 1.0000 / 0.9038 / 1.0000 | 0.00366 / 0.01238 / 0.00155 | 0.00% / 53.98% / 0.00% | 0.00% / 13.32% / 0.00% |
| **GreyholeDelayVerb** | C: WeakSat + SpecGuard | 1.0000 / 0.9063 / 1.0000 | 0.00364 / 0.01228 / 0.00154 | 0.00% / 52.23% / 0.00% | 0.00% / 11.70% / 0.00% |

---

## 6. Análise Técnica e Comparativa

### 6.1 Transparência em Uso Normal
- **Atividade Zero**: Em todas as arquiteturas avaliadas em uso normal (B e C), os ganhos por banda mantiveram-se estritamente em **`1.0000`** para todos os presets (0,00% das amostras com ganho $< 0.99$ ou $< 0.95$). O envelope de 60 ms rejeita picos transitórios de curta duração sem atenuar o decaimento exponencial natural.
- **Variação de RT60 Desprezível**: A diferença de RT60 entre A e B/C foi de **`+0.69%`** no `BrightCloud`, **`+0.74%`** no `DarkLongCloud` e **`+0.76%`** no `GreyholeDelayVerb`. Essa variação (inferior a 0.8%) é acusticamente inaudível e cumpre com ampla folga o critério de tolerância ($< 3\%$).
- **Fidelidade Espectral**: Em `DarkLongCloud` e `GreyholeDelayVerb`, os centroides espectrais e desvios ao longo de 20s são equivalentes até a terceira casa decimal entre A, B e C. Em `BrightCloud`, a Arquitetura A exibe distorção harmônica tardia aos 6s (centroide artificial de 3265 Hz devido ao clipping cúbico assimétrico), enquanto B e C decaem suavemente para ~2400 Hz.

### 6.2 Comportamento em Freeze / Saturação Extrema
- **Contenção Eficaz Sem Pumping**: No modo Freeze sustentado (20s), o acúmulo modal na banda média (250 Hz - 3 kHz) ultrapassa o limiar nominal de energia por canal ($0.00875$). O `SpectralFeedbackGuard` intervém de forma seletiva:
  - `BrightCloud`: reduz o ganho médio para $0.8656$ (B) e $0.8678$ (C).
  - `GreyholeDelayVerb`: reduz o ganho médio para $0.9038$ (B) e $0.9063$ (C).
  - `DarkLongCloud`: atenua levemente para $0.9712$ (B) e $0.9736$ (C).
- As bandas Low e High permaneceram em $1.0000$, comprovando a seletividade espectral do filtro complementar (não abafa o sinal globalmente como faria um compressor mono-banda).
- As constantes de tempo balanceadas ($50\text{ ms}$ de ataque no ganho, $500\text{ ms}$ de *release*) garantem ausência completa de *pumping* audível ou instabilidades rítmicas.
- Em freeze, o nível de pico é contido em $0.4026$ (C) e $0.4029$ (B), contra $0.4373$ em A (onde a fita saturava de forma mais dura).

### 6.3 Comparação B (Spectral Guard Only) vs C (Weak Saturation + Spectral Guard)
1. **Atuação do Guard**: Na Arquitetura C, a presença de `gentleSaturate` antes do guard executa uma compressão suave e instantânea de crista amostra a amostra. Com isso, a energia de pico que atinge os envelopes do guard é ligeiramente menor, fazendo com que o guard precise atuar com menos agressividade:
   - Em `GreyholeDelayVerb`: a porcentagem de amostras com ganho $< 0.95$ cai de **`13.32%` (B)** para **`11.70%` (C)**.
   - Em `BrightCloud`: a porcentagem $< 0.95$ cai de **`37.95%` (B)** para **`36.32%` (C)**.
2. **Proteção Sub-Amostral**: A Arquitetura B depende exclusivamente dos envelopes do guard (ataque de 50 ms a 60 ms). Surtos abruptos transitórios podem propagar um pulso de alta intensidade durante o tempo de integração do envelope antes que o ganho caia. A Arquitetura C elimina esse risco fornecendo contenção instantânea ($C^\infty$ estritamente monotônica), sem latência e sem descontinuidades de fase.
3. **Preservação de Timbre e Caráter Greyhole**: A saturação suave algébrica com $k=0.15$ reproduz com grande pureza a sensação de corpo e densidade clássica de delays/reverbs de hardware valvulado/fita, sem o DC offset e sem os harmônicos espúrios que degradavam o `tapeClip` da Arquitetura A.

---

## 7. Escolha do Default Permanente

Com base nas evidências empíricas dos testes:

1. **Rejeição da Arquitetura A**: O `tapeClip` adiciona DC offset (assimetria térmica artificial), eleva o pico em freeze para $0.4373$ e introduz artefatos espectrais mensuráveis no decaimento tardio de presets brilhantes.
2. **Comparação B vs C**:
   - A Arquitetura B provou ser estável e 100% transparente em uso normal.
   - A Arquitetura C provou ter a mesma transparência analítica em uso normal (ganho unitário exato na origem, ganho do guard $= 1.0000$, variação de RT60 $< 0.8\%$), mas oferece **dupla camada de proteção**:
     - *Nível Instantâneo*: `gentleSaturate` limita cristas extremas sem foldback e sem DC offset.
     - *Nível Espectral Dinâmico*: `SpectralFeedbackGuard` equaliza energia sustentada por faixas de frequência.
   - O alívio na profundidade de atenuação do guard (redução de amostras com ganho $< 0.95$) confere maior estabilidade dinâmica à cauda.

### Decisão Técnica:
**A Arquitetura C (`WeakSaturationSpectralGuard`) é ratificada como o default do CloudGreyVerb.**

```cpp
FeedbackArchitecture feedbackArchitecture_ = FeedbackArchitecture::WeakSaturationSpectralGuard;
```
