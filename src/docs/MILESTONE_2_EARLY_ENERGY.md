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

M2.2 mantém uma única `DelayLine` por canal. A capacidade é derivada do maior
tempo L/R do prefixo de taps ativo, multiplicado por `1,18`, arredondado para
cima e acrescido de três frames de guarda para Hermite. Cada sample faz duas
escritas (uma por canal) e duas leituras por tap (uma por canal).

| Profile | taps | máximo early (ms) | frames/canal @48 kHz | bytes (L+R) @48 kHz | frames/canal @96 kHz | bytes (L+R) @96 kHz | reads/sample | writes/sample |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| H5 Low CPU | 2 | 11,918 | 576 | 4.608 | 1.148 | 9.184 | 4 | 2 |
| H5 Balanced | 3 | 23,954 | 1.153 | 9.224 | 2.303 | 18.424 | 6 | 2 |
| H7 High Quality | 4 | 43,778 | 2.105 | 16.840 | 4.206 | 33.648 | 8 | 2 |
| Desktop Studio | 4 | 43,778 | 2.105 | 16.840 | 4.206 | 33.648 | 8 | 2 |

A implementação M2 original alocava um par de buffers por tap, baseado na
soma dos delays (Desktop/H7: 7.920 floats / 30,9 KiB a 48 kHz e 61,9 KiB a
96 kHz) e escrevia uma vez por tap/canal. A M2.1/M2.2 usa somente o histórico
necessário do maior tap: Desktop/H7 passam a 4.210 floats (16.840 bytes) a
48 kHz e 8.412 floats (33.648 bytes) a 96 kHz, com duas escritas por sample.
Não há buffers dinâmicos, loops adicionais de feedback ou mudança na
FDN/Safety Guard.

## Contrato de teste M2.2

O bloco early é medido isoladamente por um adaptador `friend` de teste; a API
de produção não expõe `processEarly()`. Ele aceita somente sinal pós-pre-delay,
`Size` e `Diffusion`: não depende de `Texture`, modulação, shimmer, feedback ou
freeze granular. Os testes por profile verificam capacidade, ordem e escala dos
taps em 44,1/48/96 kHz, finitude, chegada inicial, correlação e energia Side.

Isso é o contrato temporal determinístico. Métricas de IR do engine inteiro
(granular, modulação e tail) são estocásticas e continuam sendo avaliadas com
tolerâncias próprias; nenhuma tolerância global ampla deve ser interpretada
como permissão para variar timings da Early Layer.

## Trade-off

O early field aumenta deliberadamente o pico wet de IRs, sobretudo em
AlwaysOnSubtle. Isso é energia espacial concentrada antes de 20 ms, não ganho
no late loop; `min_safety_gain` ficou em 1,0 nos renders nominais. Avaliação
auditiva em material musical ainda é recomendada antes de alterar níveis por
preset.
