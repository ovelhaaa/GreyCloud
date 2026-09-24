# Milestone 2 — Early Energy / Dry-Wet Cohesion

## Decisão de arquitetura

A camada early é uma rede **feed-forward de multi-taps estéreo** alimentada
diretamente pelo sinal pós-pre-delay. Ela não lê a saída granular, não tem
feedback e não é modulada. Isso mantém o ataque estável e permite que
`Texture = 0` ainda produza uma ligação espacial entre fonte e cauda.

Os taps iniciais usam o mesmo tempo nos dois canais (3,2 ms), oferecendo uma
âncora de alta correlação. Os taps seguintes têm tempos primos/não
proporcionais e crossfeed pequeno e crescente. Portanto, a imagem abre após a
ancoragem, sem um primeiro reflexo anti-fásico ou um slapback lateral.

`Size` move os tempos early apenas de 0,88× a 1,18× e reduz o nível de 0,52 a
0,28; salas pequenas ganham coesão e as clouds longas mantêm a FDN como
protagonista. `Diffusion` aumenta a densidade e o crossfeed somente nos taps
posteriores. `Texture` não participa da camada early. O pre-delay global é
respeitado por ambos os caminhos.

O `wetGlue` granular histórico foi reduzido de 0,38–0,50 para 0,08–0,14. A
early layer substitui sua função de união dry/wet; a pequena parcela restante
preserva a entrada característica do smear para a cloud.

## Baseline e resultado (Desktop Studio, 48 kHz, IR wet-only)

Os CSVs e WAVs reproduzíveis ficam em `build/m2/before` e `build/m2/after2`;
gere-os compilando `cloud_grey_verb_m2_bench.cpp` com e sem
`CGV_DISABLE_EARLY_LAYER`.

| Preset | chegada antes → depois | correlação early antes → depois |
| --- | --- | --- |
| SmallCloudRoom | 5,375 → 3,104 ms | -0,03 → 0,82 |
| AlwaysOnSubtle | 5,375 → 2,958 ms | -0,03 → 0,86 |
| BassAmbientWash | 5,375 → 3,292 ms | -0,02 → 0,86 |
| BrightCloud | 5,375 → 3,333 ms | -0,02 → 0,75 |
| GreyholeDelayVerb | 5,375 → 3,500 ms | -0,01 → 0,75 |
| DarkLongCloud | 5,375 → 3,563 ms | 0,02 → 0,76 |

O benchmark também reporta as seis bandas de energia, razão early/late,
centróide, correlação e M/S early/late, pico, RMS, RT60 e safety gain, além de
renderizar IR e exemplos `transient`, `pluck` e `chord` em WAV float estéreo.

## Custo

Cada tap faz uma escrita e uma leitura Hermite por canal. Desktop/H7 usam 4
taps/canal (8 leituras + 8 escritas por amostra); H5 Balanced usa 3/canal e H5
Low CPU 2/canal. A memória extra máxima é aproximadamente 2 canais ×
`(4,5 + 12 + 24 + 42) ms` = 7920 floats: 30,9 KiB em 48 kHz e 61,9 KiB em
96 kHz (metade no Low CPU, 3/4 no Balanced). Não há buffers dinâmicos, loops
adicionais de feedback ou mudança na FDN/Safety Guard.

## Trade-off

O early field aumenta deliberadamente o pico wet de IRs, sobretudo em
AlwaysOnSubtle. Isso é energia espacial concentrada antes de 20 ms, não ganho
no late loop; `min_safety_gain` ficou em 1,0 nos renders nominais. Avaliação
auditiva em material musical ainda é recomendada antes de alterar níveis por
preset.
