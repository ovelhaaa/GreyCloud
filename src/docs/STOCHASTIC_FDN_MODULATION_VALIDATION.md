# Validação da Modulação Multiphase + Stochastic Drift na FDN

**Repositório**: `ovelhaaa/GreyCloud`  
**Data**: 28 de Setembro de 2026  
**Status**: Validação concluída / Arquitetura C ratificada como default de modulação  
**Escopo**: modulação das linhas da FDN (densidade modal e organicidade de caudas longas)  
**Fora de escopo (preservado)**: arquitetura de feedback (`WeakSaturationSpectralGuard`), presets públicos, IDs, UI, formato de estado, difusor, granular/freeze drift, feedback, size, diffusion, damping, tone.

---

## 1. Contexto e Objetivo

A milestone anterior (`SPECTRAL_GUARD_AB_VALIDATION.md`) ratificou a arquitetura de feedback `WeakSaturationSpectralGuard` (C) e o `SpectralFeedbackGuard` permanece default. Esta milestone ataca um problema distinto: caudas longas (> 10–12 s) soam **periódicas**, com sensação de *chorus/warble*, por dependerem excessivamente de modulação periódica correlacionada entre as linhas da FDN.

Princípio adotado: **menos excursão total, mais complexidade temporal.**

Foi implementada uma arquitetura de modulação **multiphase + stochastic drift band-limited, independente por linha da FDN**, com três modos internos de teste (A/B/C). Nenhum preset, ID, UI ou formato de estado foi alterado.

---

## 2. Auditoria da Modulação Anterior

### 2.1 Fontes que alteram delays dentro da FDN (arquitetura A / legado)

| Fonte | Onde atua | Natureza | Escala |
|:------|:----------|:---------|:-------|
| `lfo1_` | drift do motor granular em freeze; linha FDN 0 | LFO periódico `lerp(0.05,2.0,modRate)` Hz | — / ±15 ms·modDepth |
| `lfo2_` | linha FDN 1 | LFO periódico `0.87··lfo1` Hz | ±15 ms·modDepth |
| `spinLfo_` | difusor (fases 0/0.25/0.5/0.75); linhas FDN 2 e 3 (fases 0.125/0.625); allpasses de loop | LFO periódico `0.5+2·modRate` Hz | difusor ±2.5 samples·modDepth; loop ±~1.25 samples |
| `modDriftL_` / `modDriftR_` | misturado 15–18 % nas linhas 0/1/2/3 | sample-and-hold interpolado (1 kHz de alvo, RC 0.417 s / 0.521 s) | ±(0.15–0.18) do fundo |
| granular freeze drift | base de leitura dos grãos em freeze | `lfoDrift · texture · 150 ms · freeze` | ±150 ms (somente freeze) |

### 2.2 Excursão máxima real por linha (ms) @ 48 kHz — Legacy

| modDepth | line0 | line1 | line2 | line3 | global |
|:---------|:------|:------|:------|:------|:-------|
| 0.25 | 3.210 | 3.011 | 2.737 | 2.544 | **3.210** |
| 0.50 | 6.420 | 6.022 | 5.473 | 5.089 | **6.420** |
| 0.75 | 9.629 | 9.033 | 8.210 | 7.633 | **9.629** |
| 1.00 | 12.839 | 12.043 | 10.947 | 10.178 | **12.839** |

Confirma o comportamento documentado `modDepth · 15 ms` (o fator efetivo medido é ~0.856 pela forma do LFO parabólico e pelos pesos 0.85/0.15).

### 2.3 Correlação entre fontes

- **Linhas 2 e 3** eram versões *phase-shifted* exatas do mesmo `spinLfo_` (fases 0.125 e 0.625 = meio ciclo) → correlação de Pearson ≈ **1.000** (anti-fase).
- Linhas 0 e 1 usavam `lfo1`/`lfo2` (mesma origem, razão fixa 1.00 : 0.87), e ainda compartilhavam `modDriftL/R` com as linhas 2/3.
- O difusor reutilizava `spinLfo_` em quatro fases fixas.

Correlação cruzada medida no legado (30 s, `modDepth = 0.5`): **max |corr| = 1.000**, média = 0.181–0.209 por preset.

---

## 3. Nova Arquitetura

### 3.1 Componentes

1. **Banco periódico multiphase** (`cgv_dsp::LFO modLfo_[CGV_FDN_ORDER]`)
   - Uma instância por linha, com **taxa própria e incommensurável** e **fase inicial própria**.
   - Taxas relativas: `{1.000, 0.7071, 0.5774, 0.4472}` (1, 1/√2, 1/√3, 1/√5).
   - Fases iniciais: `{0.00, 0.31, 0.62, 0.84}`.
   - Não é um único LFO com offsets de 0/90/180/270°.

2. **Stochastic drift independente por linha** (`cgv_dsp::StochasticDrift lineDrift_[]`, em `dsp_utils.hpp`)
   - Random **sample-and-hold interpolado** com interpolante `smoothstep` (3t² − 2t³), que é **C¹ com derivada nula nas duas pontas**: sem saltos e sem mudança abrupta de derivada nas fronteiras.
   - Band-limited por construção (energia concentrada muito abaixo da taxa de segmento; sem ruído branco aplicado ao delay).
   - **PRNG próprio por linha** (Xorshift32, seeds distintas), **time scale própria**, **amplitude própria** e **fase/história independentes**.
   - Zero alocação no audio thread; sem chamadas transcendentais no caminho de áudio.

3. **Política para perfil 2×2** (`H5_LOW_CPU`): usa apenas os dois primeiros rates/amplitudes; a mesma política é válida para o fallback.

### 3.2 Tempos de drift por linha

| Linha FDN | characteristic drift | período nominal |
|:---------:|:---------------------|:----------------|
| 0 | ~0.110 Hz | ~9.1 s |
| 1 | ~0.073 Hz | ~13.7 s |
| 2 | ~0.047 Hz | ~21.3 s |
| 3 | ~0.031 Hz | ~32.3 s |

Amplitude relativa por linha (fator sobre o depth stochastic): `{1.00, 0.92, 0.84, 0.76}`.

### 3.3 Ranges de modulação (por `modDepth`)

| Componente | Excursão em `modDepth = 1` | Observação |
|:-----------|:---------------------------|:-----------|
| Periódica multiphase | **±2.0 ms** | `kPeriodicModDepthSeconds = 0.0020f` |
| Stochastic band-limited | **±0.8 ms** | `kStochasticModDepthSeconds = 0.0008f` |
| Combinada (C) | ≈ **±2.8 ms** teórico | medido 2.777 ms |
| Legado (A) | ≈ ±12.8 ms medido | redução de **~4.6×** |

Mantém-se o taper histórico `(1 − 0.06·i)` por linha. O *clamp* físico do `DelayLine` (mínimo 2 frames, máximo buffer−2) foi preservado; nenhum clamp adicional achata a modulação.

### 3.4 Modos internos de teste (test-only)

Enumerador `CloudGreyVerb::ModulationMode`, acessível apenas via `CloudGreyVerbComponentTestAccess` (sem parâmetro, sem UI, sem estado):

- **A — `Legacy`**: caminho legado bit-exato (preservado para A/B offline).
- **B — `ReducedPeriodic`**: somente banco periódico reduzido.
- **C — `MultiphaseStochastic`**: periódico reduzido + stochastic independente por linha. **Default de produção.**

---

## 4. Excursão Medida (48 kHz, BrightCloud, 30 s)

| modDepth | Modo | line0 | line1 | line2 | line3 | global |
|:---------|:-----|:------|:------|:------|:------|:-------|
| 0.25 | A Legacy | 3.210 | 3.011 | 2.737 | 2.544 | 3.210 |
| 0.25 | B Reduced | 0.500 | 0.470 | 0.440 | 0.410 | 0.500 |
| 0.25 | C Stoch | 0.694 | 0.592 | 0.523 | 0.463 | 0.694 |
| 0.50 | A Legacy | 6.420 | 6.022 | 5.473 | 5.089 | 6.420 |
| 0.50 | B Reduced | 1.000 | 0.940 | 0.880 | 0.820 | 1.000 |
| 0.50 | C Stoch | 1.388 | 1.184 | 1.047 | 0.926 | 1.388 |
| 0.75 | A Legacy | 9.629 | 9.033 | 8.210 | 7.633 | 9.629 |
| 0.75 | B Reduced | 1.500 | 1.410 | 1.320 | 1.230 | 1.500 |
| 0.75 | C Stoch | 2.083 | 1.776 | 1.570 | 1.388 | 2.083 |
| 1.00 | A Legacy | 12.839 | 12.043 | 10.947 | 10.178 | 12.839 |
| 1.00 | B Reduced | 2.000 | 1.880 | 1.760 | 1.640 | 2.000 |
| 1.00 | C Stoch | 2.777 | 2.368 | 2.094 | 1.851 | 2.777 |

Na faixa musical típica (`modDepth ≈ 0.25–0.50`) a excursão combinada de C fica em **0.7–1.4 ms**, contra **3.2–6.4 ms** no legado.

---

## 5. Correlação Cruzada dos Moduladores (Pearson, 30 s, modDepth = 0.5)

| Preset | Modo | pairs | max \|corr\| | média \|corr\| | pior par |
|:-------|:-----|:------|:-------------|:---------------|:---------|
| BrightCloud | A Legacy | 6 | **1.000** | 0.181 | L2–L3 |
| BrightCloud | B Reduced | 6 | 0.100 | 0.035 | L2–L3 |
| BrightCloud | C Stoch | 6 | **0.085** | 0.039 | L2–L3 |
| DarkLongCloud | A Legacy | 6 | **1.000** | 0.209 | L2–L3 |
| DarkLongCloud | B Reduced | 6 | 0.217 | 0.075 | L1–L2 |
| DarkLongCloud | C Stoch | 6 | **0.196** | 0.059 | L1–L2 |
| GreyholeDelayVerb | A Legacy | 6 | **1.000** | 0.192 | L2–L3 |
| GreyholeDelayVerb | B Reduced | 6 | 0.074 | 0.020 | L1–L2 |
| GreyholeDelayVerb | C Stoch | 6 | **0.075** | 0.019 | L1–L2 |

A correlação cai de **1.000** (A) para **≤ 0.196** (C) em todos os presets. Nenhuma dupla apresenta correlação alta persistente.

---

## 6. Métrica de Warble (late tail 0.5–5 s)

Excitacão: *sine burst* Hann de 150 ms; rastreio do pico espectral próximo de f0 (janela 16384, interpolação parabólica) na cauda tardia.

| Preset | f0 | A Legacy std (cents) | B Reduced std (cents) | C Stoch std (cents) | C periodicity |
|:-------|:---|:---------------------|:----------------------|:--------------------|:--------------|
| BrightCloud | 440 | 44.602 | 11.682 | 14.125 | 0.279 |
| BrightCloud | 1000 | 43.759 | 5.351 | 9.999 | 0.486 |
| DarkLongCloud | 440 | 16.094 | 13.056 | 12.945 | 0.270 |
| DarkLongCloud | 1000 | 11.005 | 5.066 | 5.383 | 0.362 |
| GreyholeDelayVerb | 440 | 30.745 | 12.923 | 12.804 | 0.239 |
| GreyholeDelayVerb | 1000 | 25.205 | 5.641 | 5.424 | 0.192 |

- **Warble não aumenta**: em todos os casos C é muito inferior ou equivalente ao legado.
- **Periodicidade**: o legado apresenta periodicity de 0.258–0.569; C apresenta 0.192–0.486, com mediana menor — a trajetória da frequência é menos periódica.
- Nenhum vibrato audível novo é introduzido (C preserva f0 com desvio ≤ ~14 cents em 440 Hz, contra ~45 cents do legado).

---

## 7. RT60, Spectral Drift e Long Tail (impulso, 20 s)

| Preset | Modo | Peak | RT60 (s) | ΔRT60 vs A | Mean Drift | Cent10 | Cent18 | Clicks | Growth |
|:-------|:-----|:-----|:---------|:-----------|:-----------|:-------|:-------|:-------|:-------|
| BrightCloud | A | 1.555e-01 | 2.90 | — | 0.0216 | 0 | 0 | 0 | 0 |
| BrightCloud | B | 1.555e-01 | 2.98 | +2.4 % | 0.0200 | 0 | 0 | 0 | 0 |
| BrightCloud | C | 1.555e-01 | 2.97 | +2.4 % | 0.0253 | 0 | 0 | 0 | 0 |
| DarkLongCloud | A | 5.850e-02 | 20.18 | — | 0.0186 | 1834 | 1496 | 0 | 1 |
| DarkLongCloud | B | 5.850e-02 | 20.28 | +0.5 % | 0.0231 | 1799 | 1447 | 0 | 1 |
| DarkLongCloud | C | 5.850e-02 | 20.16 | −0.1 % | 0.0235 | 1851 | 1440 | 0 | 1 |
| GreyholeDelayVerb | A | 1.255e-01 | 13.22 | — | 0.0147 | 1842 | 1544 | 0 | 0 |
| GreyholeDelayVerb | B | 1.255e-01 | 13.42 | +1.5 % | 0.0154 | 1917 | 1548 | 0 | 0 |
| GreyholeDelayVerb | C | 1.255e-01 | 13.37 | +1.1 % | 0.0159 | 1901 | 1678 | 0 | 0 |

- **RT60 dentro de poucos %** do baseline (máx +2.4 %), com pico idêntico ao legado por preset.
- **Spectral drift** maior em C (0.0253 / 0.0235 / 0.0159) que no legado (0.0216 / 0.0186 / 0.0147): mais movimento espectral = cauda menos estacionária, que é o objetivo.
- **Zero clicks** em todos os modos/presets.

### 7.1 Pluck / harpa (10 s)

| Preset | Modo | Peak | RMS | Clicks | Growth |
|:-------|:-----|:-----|:----|:-------|:-------|
| BrightCloud | A / B / C | 1.048e-01 | 7.02e-03 / 6.49e-03 / 6.68e-03 | 0 / 0 / 0 | 0 / 0 / 0 |
| DarkLongCloud | A / B / C | 6.702e-02 | 4.83e-03 / 4.90e-03 / 4.90e-03 | 0 / 0 / 0 | 1 / 2 / 2 |
| GreyholeDelayVerb | A / B / C | 1.116e-01 | 9.01e-03 / 8.69e-03 / 8.52e-03 | 0 / 0 / 0 | 0 / 0 / 0 |

Sem clicks nem instabilidade.

---

## 8. Comportamento em Freeze (20 s; 200 ms de excitação densa + freeze)

| Preset | Modo | Peak | RMS 1–4 s | RMS 16–20 s | Mean Drift | Growth | Guard %<0.99 (M) | MinGain (M) |
|:-------|:-----|:-----|:----------|:------------|:-----------|:-------|:-----------------|:------------|
| BrightCloud | A | 4.026e-01 | 7.882e-02 | 8.430e-02 | 0.0250 | 13 | 80.93 % | 0.8678 |
| BrightCloud | B | 4.489e-01 | 8.387e-02 | 8.858e-02 | 0.0270 | 14 | 77.39 % | 0.8244 |
| BrightCloud | C | 4.588e-01 | 8.717e-02 | 8.485e-02 | 0.0264 | 14 | 73.37 % | 0.7933 |
| DarkLongCloud | A | 2.913e-01 | 3.529e-02 | 5.866e-02 | 0.0111 | 13 | 4.93 % | 0.9736 |
| DarkLongCloud | B | 2.845e-01 | 3.545e-02 | 5.753e-02 | 0.0108 | 9 | 6.21 % | 0.9217 |
| DarkLongCloud | C | 2.766e-01 | 3.498e-02 | 5.982e-02 | 0.0107 | 11 | 6.02 % | 0.9272 |
| GreyholeDelayVerb | A | 4.055e-01 | 5.995e-02 | 8.576e-02 | 0.0183 | 8 | 52.23 % | 0.9063 |
| GreyholeDelayVerb | B | 4.566e-01 | 5.775e-02 | 8.425e-02 | 0.0197 | 12 | 53.57 % | 0.8724 |
| GreyholeDelayVerb | C | 3.952e-01 | 5.924e-02 | 8.524e-02 | 0.0199 | 11 | 59.22 % | 0.8615 |

- **Sem crescimento de energia descontrolado**: RMS 16–20 s estável e da mesma ordem do legado; growth warnings comparáveis (ex.: BrightCloud 13 → 14).
- **Sem pumping**: as constantes do guard (ataque 50 ms / release 500 ms) não foram alteradas.
- O `SpectralFeedbackGuard` continua seletivo (atua só na banda média). A frequência de atuação permanece na mesma ordem de grandeza; em BrightCloud até diminui (80.9 % → 73.4 %), em Greyhole sobe modestamente (52.2 % → 59.2 %).
- O granular/freeze drift legado foi **intencionalmente preservado** (fora de escopo) para não alterar o caráter do freeze; a diversidade modal adicional vem da FDN.

---

## 9. Interação com o Spectral Guard (uso normal, impulso 20 s)

| Preset | Modo | MinGain L/M/H | %<0.99 L/M/H | %<0.95 L/M/H |
|:-------|:-----|:--------------|:-------------|:-------------|
| BrightCloud | A/B/C | 1.0000 / 1.0000 / 1.0000 | 0/0/0 % | 0/0/0 % |
| DarkLongCloud | A/B/C | 1.0000 / 1.0000 / 1.0000 | 0/0/0 % | 0/0/0 % |
| GreyholeDelayVerb | A/B/C | 1.0000 / 1.0000 / 1.0000 | 0/0/0 % | 0/0/0 % |

Em condições normais o guard permanece **100 % transparente** em todos os modos: a nova modulação **não** faz o guard atuar mais.

---

## 10. Tabela Consolidada A/B/C

| Critério | A Legacy | B Reduced periodic | C Multiphase stochastic |
|:---------|:---------|:-------------------|:------------------------|
| Excursão `modDepth=1` | ~12.8 ms | 2.0 ms | 2.8 ms |
| Correlação máx. entre linhas | 1.000 | ≤ 0.217 | ≤ 0.196 |
| Warble (cents) | 11–45 | 5–13 | 5–14 |
| Periodicidade da trajetória | alta | alta | **baixa** |
| Spectral drift (caudas) | baseline | similar | **maior** |
| RT60 vs baseline | — | +0.5…+2.4 % | −0.1…+2.4 % |
| Guard em uso normal | transparente | transparente | transparente |
| Freeze estável | sim | sim | sim |
| Clicks | 0 | 0 | 0 |
| Complexidade temporal | periódica | periódica | **multiphase + stochastic** |

---

## 11. Arquitetura Escolhida

**C — `MultiphaseStochastic`** é ratificada como o default de modulação (`modulationMode_ = ModulationMode::MultiphaseStochastic`).

Justificativa:
- Cumpre todos os critérios: excursão muito menor que o legado, correlação drasticamente reduzida, warble menor, RT60 dentro de poucos %, guard transparente em uso normal, freeze estável, sem clicks.
- **B foi rejeitada como default** por apenas reduzir a excursão mantendo a natureza periódica e até reduzindo o spectral drift (cauda mais estacionária). A arquitetura C é a única que efetivamente troca excursão por **complexidade temporal** (multiphase + drift independente por linha).

Preservado intacto nesta milestone:
- `FeedbackArchitecture::WeakSaturationSpectralGuard` (arquitetura de feedback);
- `SpectralFeedbackGuard` e suas constantes;
- presets públicos, catálogo/IDs, UI e formato de estado.

---

## 12. Arquivos Alterados

| Arquivo | Mudança |
|:--------|:--------|
| `src/dsp/dsp_utils.hpp` | `LFO::setPhase`; novo `StochasticDrift` (S&H C¹, PRNG próprio, sem alocação). |
| `src/dsp/cloud_grey_verb.hpp` | `enum class ModulationMode`; membros `modLfo_`, `lineDrift_`, `lineDelayOffsetFrames_`; acessores test-only. |
| `src/dsp/cloud_grey_verb.cpp` | Constantes do milestone; init/reset dos bancos; branch de modulação A/B/C no `processSample`. |
| `src/dsp/stochastic_modulation_test.cpp` | Harness de validação (excursão, correlação, warble, long tail, pluck, freeze, guard). |
| `src/dsp/CMakeLists.txt` | Alvo `stochastic_modulation_test` (diagnóstico). |

---

## 13. Critérios de Aceitação

| # | Critério | Resultado |
|:-:|:---------|:----------|
| 1 | Excursão total significativamente menor que o legado | ✅ 2.8 ms vs 12.8 ms (`modDepth=1`) |
| 2 | Sem clicks nem instabilidade | ✅ 0 clicks, saída finita |
| 3 | Correlação entre linhas reduzida | ✅ 1.000 → ≤ 0.196 |
| 4 | Warble não aumenta | ✅ reduzido (≤ 14 vs 11–45 cents) |
| 5 | Caudas longas menos periódicas | ✅ periodicity menor + spectral drift maior |
| 6 | RT60 dentro de poucos % | ✅ máx +2.4 % |
| 7 | Guard não atua significativamente mais em uso normal | ✅ 100 % transparente |
| 8 | Freeze estável | ✅ sem crescimento, guard comparável |
| 9 | BrightCloud reconhecível | ✅ pico/RT60/drift preservados |
| 10 | Todos os testes existentes passam | ✅ 7/7 CTest |
