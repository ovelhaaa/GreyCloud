# Nimbus M6 — auditoria de segurança DSP

Base: `48f1cf1005cb3ed1edbe1d6219567d8965d99d3e`, merge do PR #2.

## Causa raiz reproduzida

O cenário de 16 s do harness original foi reproduzido integralmente, incluindo
fonte pluck 1700 Hz, seed `1234567`, stereo core, feedback `0.6`, envelopes de
entrada, automação a cada 31 samples e Freeze entre 0,8 e 2,8 s. O CSV tem 331
cenários: o caso em questão é o índice **329 contando de zero**, ou cenário 330
contando de um; está na linha 331 incluindo o cabeçalho. `--extreme` executa
somente esse caso, sem reduzir duração ou ativar clipping.

No frame **623509** (12,98977083 s), a quarta linha da FDN pede delay
`5253.004883`, com write position 5253. A posição bruta é `-0.0048828125`.
O ring tem 154564 samples; sua ULP float é `0.015625`. Ao adicionar o tamanho,
o resto negativo arredonda para **154564**, em vez de uma posição menor.
O antigo `DelayLine::read()` converte essa posição em `idx1 == size_` e lê
`buffer_[size_]`. O defeito está em `src/dsp/dsp_utils.hpp`, na normalização
e conversão do índice do leitor compartilhado (linhas 284 e 287 na base);
a instrumentação encontra o
primeiro acesso inválido desse cenário exatamente nesse frame.

A quarta linha é a última região do pool. O harness deixa `98765.0f` nos
16 floats de guarda após a capacidade anunciada ao DSP. Esse sentinela é a
amplitude lida, explica o valor numérico extremo e demonstra o acesso fora
da região permitida. Em produção, o valor depende da memória seguinte;
ler outra região do pool também é incorreto mesmo quando ASan não distingue
essas sub-regiões de uma alocação maior.

O evento ocorre com Scan=1, Reverse=0, Texture=1, Freeze target=0,
Freeze smoothed aproximadamente `1.4e-43`, feedback efetivo `0.564` e sem
excitação. Não é crescimento espontâneo da FDN nem uma transição de Freeze.
A modulação aproxima a posição da fronteira e expõe um defeito do leitor.
Hadamard, filtros, allpasses, saturação e guard propagam um valor já inválido.
A decodificação estéreo usa a leitura da cauda antes da saturação de escrita;
portanto, a saturação existente não impede o pico de saída.

| Estágio no frame 623509 | Antes | Depois |
|---|---:|---:|
| Entrada / Pre-Delay / Granular / Diffuser / Injeção | 0 | 0 |
| Leitura bruta FDN, linha 4 | 98765 | 0 |
| Após damping, HP e loop allpass, linha 4 | -486,62323 | 0 |
| Hadamard, magnitude por linha | 243,311615 | 0 |
| Antes da saturação, magnitude por linha | 120,760422 | 0 |
| Após saturação e Spectral Guard | 2,581399 | 0 |
| Escrita de feedback, magnitude por linha | 2,581399 | 0 |
| Decodificação e saída final L/R | ±243,311615 | 0 / 0 |

Snapshots completos e máximos por estágio: [antes](before-stages.csv) e
[depois](after-stages.csv). A saída float mantém `clipOutput=false` e
Output Gain=1 em toda a comparação.

| Métrica do cenário inteiro | Antes | Depois |
|---|---:|---:|
| Peak | 243,312 | 0,128177 |
| RMS | 0,318950 | 0,0107756 |
| Maior diferença entre samples | 243,312 | 0,0277821 |
| Maior segunda diferença | 371,477 | 0,0151674 |
| Energia média da segunda diferença | 0,263940 | 2,75504e-7 |
| Picos isolados | 1 | 0 |
| Leituras circulares inválidas no cenário | 1 | 0 |
| Máxima energia suavizada da malha | 0,480759 | 0,00507742 |

O pico legítimo depois da correção ocorre no frame 132699 (2,7645625 s).
Os dois CSVs originais das métricas estão em [before.csv](before.csv) e
[after.csv](after.csv). A regressão retorna 1 para os leitores antigos e 0
para os corrigidos; ela não depende de observar somente a saída.

## Correção e preservação sonora

`cgv_dsp::circularPosition()` normaliza índice e fração em conjunto. Um
wrap cobre as leituras comuns; `fmod` só atende posições com múltiplos wraps.
O arredondamento para exatamente o tamanho é interpretado como sample zero,
mantendo índice válido e fração zero. Valores não finitos não são convertidos
em inteiros. Acima de 2^24 samples há um fallback double, pois nem todos os
tamanhos inteiros são representáveis em float. A quantização do seam permanece
limitada pela resolução float da posição, sem extrapolação.

DelayLine conserva Hermite e a restrição de delay original. Shimmer conserva
interpolação linear, janelas, pitch ratios e offset estéreo de 7 ms. No leitor
antigo do Shimmer, zerar `idx1` após o wrap arredondado deixava `frac=size_`:
o leitor extrapolava a diferença entre samples em vez de interpolar. Os testes
de fronteira demonstram esse defeito independentemente da FDN.

O granular já possuía a proteção desse seam no PR #2; apenas passa a usar
a mesma normalização. Timing, trajetórias, Scan, Reverse, reinícios e fades
não mudam. Nenhum coeficiente musical, preset, ID/APVTS, ganho de feedback,
filtro, diffuser, saturador, Spectral Guard ou parâmetro de Freeze foi alterado.

Os testes circulares falham na versão anterior com dez leituras DelayLine e
seis leituras Shimmer incorretas; passam depois. Incluem rings de 1 a 524287
samples, posições sub-ULP negativas, vizinhos representáveis das fronteiras,
delays fracionários, múltiplos wraps e limites normalizados acima de 2^24.
Há alocação exata sem padding para permitir detecção física por ASan, além
dos sentinelas que detectam violações lógicas dentro do pool.

O teste de pitch usa uma senoide com 20 ciclos por janela de 42 ms para isolar
o pitch das bandas laterais das janelas existentes. Testa ratios 0,5 / 1,5 /
2 / 3 / 4 nas quatro taxas; erro permitido de pitch é 1%, concentração
espectral mínima 90%. O teste de impulso verifica o head de 29 ms no ratio
unitário e o offset direito de 7 ms, incluindo delays fracionários.
Resultados detalhados: [circular antes](circular-before.txt) e
[circular depois](circular-after.txt).

## Critérios de regressão

O cenário determinístico e as caudas prolongadas usam entrada com amplitude
máxima 0,3 e saída sem clipping. Limites conservadores de headroom:
peak ≤ 0,6, RMS ≤ 0,1, passo ≤ 0,3, segunda diferença ≤ 0,6, energia média
da segunda diferença ≤ 0,002, nenhum pico isolado e RMS tardio ≤ 0,01.
Todos os estágios medidos devem permanecer finitos e os índices válidos.
A leitura interna tem limite 3,23: 1,25 vezes o teto 1/sqrt(0,15) do
saturador existente, com o Spectral Guard transparente em uso nominal.
A saída do saturador tem limite 2,583. Esses checks antecedem os filtros
de saída e não podem ser satisfeitos adicionando clipping final.

`--safety-long` executa 20 combinações: 60 s em 44,1 / 48 / 96 / 192 kHz,
cinco pitch ratios, Shimmer=1, automação abrupta de Scan/Reverse/Texture,
entrada encerrada em 3 s e Freeze entre 0,8 e 20 s. O RMS tardio é medido
após 24 s. Os testes anteriores preservam a cobertura adicional de hard
Freeze, modos mono/estéreo, perfis MCU e processamento em blocos.

## Validação e limitações

- CTest DSP: **15/15 aprovados**, incluindo quatro perfis;
  [log completo](ctest.txt). O teste circular foi executado novamente após
  adicionar verificação de zero alocações: [log](circular-ctest.txt).
- A/B dos leitores: falha reproduzida antes e ambos os testes passam depois.
- Vinte caudas prolongadas passaram: peak máximo 0,537752 e RMS máximo
  0,058994, sem leituras inválidas ou picos isolados.
  [Métricas por taxa e ratio](long-summary.csv).
- Fuzz/extremos existente passou: [log](fuzz.txt).
- WASM estático e AudioWorklet: compilados localmente com as flags e exports
  dos scripts existentes; `cgv_set_preset` confirmado nos dois módulos.
- JUCE 8.0.0: configuração tentada com MinGW GCC 14.2; juceaide falha nos
  headers DirectX/JUCE (`multithread` / `juce_CompilerWarnings.h`). MSVC não
  está instalado. Os testes JUCE/APVTS/realtime e VST ficam para o job Windows
  existente no PR; não são declarados aprovados localmente.
- ASan/UBSan local: Clang 19.1.1 não dispõe de `libclang_rt.asan_dynamic`;
  GCC 14.2 não dispõe de `libasan`/`libubsan`. Ambos os links foram tentados
  e falharam. O novo job Linux executa a suíte com ambos os sanitizers e
  halt-on-error, além da suíte sem instrumentação.
- Deployment Pages não foi disparado: publica o site e não é necessário
  para validar a correção. Seus dois builds WASM foram executados.
- Hardware MCU, audição humana e tempo real com host VST não foram medidos
  nesta máquina; os quatro perfis DSP são compilados e testados localmente.

Não há novas alocações, locks ou logging no caminho de produção. A expansão
dos probes e o contador de leituras são compilados somente com
`CGV_DSP_SAFETY_TESTING`. `sizeof(CloudGreyVerb)` permanece 2040 bytes no
desktop x64, e o pool a 48 kHz permanece em 1517637 floats (6070548 bytes).
Apenas código de normalização foi acrescentado; nenhuma RAM de áudio extra.
O teste conta alocações C++ enquanto alterna parâmetros e processa blocos de
257 samples nas quatro taxas: **zero alocações**.

Benchmark desktop GCC 14.2 `-O3`, cinco repetições por mediana, dois pares
sequenciais depois dos testes. A máquina mostrou variação considerável de
tempo; não se deve interpretar esses valores como um ganho garantido de CPU.
Não se observou regressão nos dois pares. O caminho comum substitui `fmodf`
por um único wrap, sem adicionar trabalho ao restante da arquitetura.

| Caminho | Antes, ns/sample (par 1 / 2) | Depois, ns/sample (par 1 / 2) |
|---|---:|---:|
| Granular estático | 239,877 / 264,384 | 194,814 / 130,643 |
| Core completo | 2976,29 / 1630,04 | 2655,51 / 1231,47 |
| Granular automatizado | 382,078 / 192,543 | 306,251 / 144,559 |

[Antes](bench-before.txt), [depois](bench-after.txt),
[antes repetido](bench-before-repeat.txt), [depois repetido](bench-after-repeat.txt).
Os checksums dos dois caminhos granulares são idênticos; o core completo
muda porque as leituras de regiões adjacentes ao ring deixam de contaminar
o áudio. Não foi obtida uma medição de ciclos em MCU nem um benchmark isolado
do host VST.

## Reprodução

```sh
cmake -S src/dsp -B build/m6 -DCMAKE_BUILD_TYPE=Release
cmake --build build/m6 --parallel 4
ctest --test-dir build/m6 --output-on-failure --parallel 2
python tools/dsp_safety_compare.py
```

O comparador recompila o mesmo harness e probes com os três leitores da base
e com os atuais, exige falha antes/sucesso depois e salva os snapshots.
Também pode receber `--compiler` e `--output`.

Em ambiente com runtimes de sanitizers:

```sh
cmake -S src/dsp -B build/safety-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build/safety-sanitize --parallel 4
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/safety-sanitize --output-on-failure --parallel 2
```
