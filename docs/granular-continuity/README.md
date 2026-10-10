# Nimbus: continuidade granular

Validação em 10 de outubro de 2026. Base comparativa: `38085c994e87a09b9e4f30fb87f486e8a65c2aaf`.

## Causas confirmadas

As linhas abaixo referem-se ao commit base, em `src/dsp/cloud_grey_verb.cpp`.

- **Interpolação de referências incompatíveis (linha 766).** O tap acompanha o índice circular do write head, mas a âncora permanece na referência do início do grain. No wrap, a interpolação intermediária percorre uma fração indevida do comprimento do ring. O caso 325 (48 kHz, Scan 0.5, Reverse 0, Texture 1) registra deslocamentos de até 12.001 frames por amostra, 42 eventos de salto e um degrau de saída de 0.333749. Com controles estáticos, os saltos coincidem com wraps.
- **Histórico Reverse sobrescrito (linha 761).** A idade cresce como `atraso + tempo decorrido + span reverso`. O array não detecta esse erro: os índices continuam circulares e dentro da alocação. O mesmo caso registra 439.138 observações de idade inválida; o caso 326 torna o Reverse audível e apresenta segunda diferença de 0.174072. Os contadores incluem heads com ganho zero; não representam uma contagem de cliques audíveis.
- **Timing variável durante o grain (linhas 715–724).** Texture recalculava a duração de um grain ativo. Além disso, os multiplicadores eram atualizados depois do cálculo da duração no reinício, criando outra troca de span na amostra seguinte. A automação de Texture e os reinícios podiam deslocar os heads com janela aberta.
- **Ganhos sem transição (linha 780).** Mudanças abruptas de Reverse alteravam imediatamente os ganhos de sinais distintos; Grain Scan deslocava diretamente a posição. O caso 327, com mudanças abruptas, apresenta degrau 0.398515 e segunda diferença 0.413059 na base.
- **Arredondamento no wrap (linha 657).** Somar um resto negativo minúsculo ao tamanho float do ring pode resultar em exatamente `size`; a conversão passa a ler a primeira amostra do canal/área seguinte. O teste `tinyNegativeWrap` reproduz o defeito usando valores diferentes nos dois canais. É uma leitura indevida dentro do pool, que um guard apenas no fim do pool não captura.

## Implementação

`cloud_grey_verb.cpp:657` normaliza o endereço físico e trata o arredondamento para `size`. `latchGrainTiming`, na linha 678, fixa duração, offset e drift para a vida do grain e calcula a margem necessária para ambos os heads. O processamento deriva a fase dos frames decorridos; evita acumular incrementos fracionários durante dezenas de milhares de amostras. As coordenadas de leitura são idades relativas ao próximo write, e somente o acesso físico faz wrap.

O perfil Desktop reserva 1,2 s por canal. Os perfis H5 Low CPU, Balanced/WASM e H7 preservam os 500 ms existentes. Neles, o próximo grain é limitado por `frames <= (capacidade - atraso - margem) / (1 + 1/densidade)`, mesmo quando Reverse está em zero, para permitir automação sem trocar o timing ativo. Com pouco atraso e densidade próxima de 1, o limite é aproximadamente 250 ms de span; atrasos maiores reduzem esse valor. Desktop também aplica a mesma garantia se o drift de Freeze consumir a margem disponível. A duração efetiva da janela é `frames/densidade`.

Grain Scan mantém a interpolação temporal dos endpoints estáticos. A automação usa duas trajetórias estáveis e crossfade linear de áudio por 20 ms. Um novo alvo recebido durante o fade fica pendente até seu término: resposta final em até aproximadamente 40 ms após a última alteração. Reverse interpola os dois ganhos de áudio com slew limitado, alcançando os ganhos equal-power finais exatos em até 20 ms. Não interpola velocidades/direções e não introduz Doppler de automação. Durante a transição, os ganhos não ficam necessariamente sobre a curva equal-power, evitando o salto de derivada da raiz quadrada nos endpoints.

Jitter, pan e multiplicadores continuam mudando no reinício; a janela parabólica permanece. O drift de Freeze passa a ser capturado por grain, evitando deslocamentos de offset com janela aberta. A variação orgânica de densidade existente (0.92–1.08) permanece: não se promete reprodução bit-exact nem eliminação dessa dispersão original de pitch. Texture passa a afetar o próximo grain, como necessário para estabilizar sua trajetória.

`vst/PluginProcessor.cpp:191` inicializa os cores com o snapshot de parâmetros antes de reset. As transições de preset/HQ (linha 508) e restauração anterior ao primeiro áudio (linha 729) também aplicam o alvo antes do reset. O envelope wet existente permanece em uso. IDs, intervalos APVTS e valores dos presets BrightCloud e ReverseSmear permanecem inalterados. Não há mudanças na FDN, Shimmer, difusão, modulação estocástica ou Spectral Feedback Guard.

## Evidência comparativa

Os arquivos [before.csv](before.csv) e [after.csv](after.csv) têm as mesmas 331 linhas de cenários, na mesma ordem. Amplitudes são float em escala de áudio. Segunda diferença é `x[n]-2*x[n-1]+x[n-2]`; sua energia é uma medida ponderada de altas frequências, não uma FFT de banda nem uma prova isolada de audibilidade.

| Cenário (48 kHz, Texture 1) | Métrica | Antes | Depois |
|---|---|---:|---:|
| Scan 0.5, Reverse 0, estático (325) | maior degrau L/R | 0.333749 | 0.0490041 |
| mesmo | maior segunda diferença L/R | 0.360887 | 0.00634223 |
| mesmo | maior degrau associado a wrap | 0.333749 | 0.0228262 |
| mesmo | RMS | 0.097594 | 0.0947047 |
| Scan 0.5, Reverse 1, estático (326) | maior segunda diferença L/R | 0.174072 | 0.00611052 |
| mesmo | energia da segunda diferença por frame | 7.37521e-6 | 2.43453e-6 |
| mudanças abruptas Scan/Reverse/Texture (327) | maior degrau L/R | 0.398515 | 0.055021 |
| mesmo | maior segunda diferença L/R | 0.413059 | 0.007135 |
| mesmo | picos acima de 12× RMS local | 4 | 0 |
| 331 cenários, todos os heads observados | idades inválidas | 87.153.526 | 0 |
| 331 cenários, controles estáticos | saltos de trajetória >1.2 frames/amostra | 7.434 | 0 |
| 225 combinações estáticas de senoide | maior segunda diferença L/R | 0.434406 | 0.0105619 |

RMS local usa envelope de 10 ms e piso de amplitude. Fontes com transientes legítimos e ruído não são submetidas ao limiar de segunda diferença de senoide. Por exemplo, o caso 328 registra 2 picos antes e 6 depois: estes eventos, em material transitório com Freeze e processamento completo, não são classificados automaticamente como defeitos. Os testes de segurança temporal permanecem ativos para todas as fontes.

WAVs stereo float de 4 s são gerados para os casos 325–328. Na execução local ficam em `.codex/granular-comparison/{before,after}/`, com nomes `before_325.wav` / `after_325.wav`, etc. Não foram versionados para evitar incorporar binários regeneráveis ao código.

## Testes

[ctest.txt](ctest.txt): **12/12 passaram**, incluindo os testes existentes de Milestone, Factory Preset Parity, Reverse Mix, M2 em quatro perfis e Offline Metrics. Nenhuma asserção existente foi relaxada. O comentário de capacidade no Reverse Mix foi atualizado; suas asserções permanecem iguais.

O novo `CloudGreyVerbGranularContinuityTest` integra CTest no core e no build VST. Cada um dos quatro perfis executa:

- Matriz cartesiana estática: Scan e Reverse em 0/0.25/0.5/0.75/1, Texture em 0/0.5/1, sample rates 44.1/48/96 kHz, 4 s por combinação, incluindo wraps repetidos.
- 100 cenários complementares com senoide, ruído, impulso, transientes e sinal de 12 kHz, isolando GranularOnly, DiffuserOnly, FdnLateOnly e processamento completo. Incluem core estéreo/mono, Freeze/Hard Freeze, sweep lento, automação rápida, degraus e rates adicionais 88.2/192 kHz para o core HQ.
- Quatro cenários WAV e duas caudas completas de 16 s, com entrada terminando em 3 s.
- Paridade bit-exact entre `processSample` e blocos 1/17/64/257 nas três sample rates; reset e convergência exata dos controles; pools de tamanho exato com guards; teste de wrap negativo sub-ULP.

A senoide tem ataque/release de 10 ms em ambos os canais. Isso separa descontinuidades do DSP de um início/corte deliberadamente descontínuo da própria fonte. A matriz de estados adicionais é complementar, não o produto cartesiano de todos os estados e fontes. O core HQ foi testado nas rates acima; oversampling, troca de HQ no host e APVTS dependem dos testes JUCE.

A base retorna exit 1 no mesmo harness; a versão corrigida retorna exit 0. O harness usa acesso friend em testes; não acrescenta logging, alocação, mutex ou API de diagnóstico ao callback normal.

## CPU e RAM

Benchmark GCC 14.2, C++17, `-O3 -DNDEBUG`, Desktop, 48 kHz. Mediana de cinco repetições de 480.000 frames, com entrada preparada antes do cronômetro, sem métricas nem I/O no loop. Execuções antes/depois sequenciais, depois de encerrar os testes concorrentes. Resultados em [bench-before.txt](bench-before.txt) e [bench-after.txt](bench-after.txt).

| Caminho | Antes (ns/frame) | Depois (ns/frame) |
|---|---:|---:|
| Granular estático | 133.197 | 105.375 |
| Core completo | 1002.74 | 1001.68 |
| Granular com automação a cada 20 ms | 140.859 | 140.355 |

O ganho estático vem de remover fmod de atrasos e raízes por grain. Durante a automação de Scan há uma leitura interpolada adicional por grain; fora da transição continuam duas leituras. O core completo ficou dentro da variação normal de medição. São resultados deste PC, não um benchmark de MCU ou garantia de redução em qualquer host.

Desktop a 48 kHz: pool de 1.450.435 para 1.517.637 floats, **+67.202 floats / +268.808 bytes por core**. Normal + HQ a 48 kHz de host acrescentam aproximadamente 806 kB no total. `sizeof(CloudGreyVerb)` x64 passou de 1912 para 2040 bytes (+128). Os pools embarcados/WASM não aumentaram; o novo estado fixo custa aproximadamente `32 + 16*CGV_NUM_GRAINS` bytes, sujeito ao alinhamento/ABI do compilador. Nenhuma alocação ocorre em `processSample`, `processBlock` ou `processGranular`.

## Reprodução e limites

```sh
cmake -S src/dsp -B build/dsp -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/dsp --parallel 4
ctest --test-dir build/dsp --output-on-failure --parallel 4
python tools/granular_compare.py --out build/granular-comparison
```

O script compara com o commit base, gera CSV/WAV/logs e compila o benchmark com as mesmas flags. Para um perfil embarcado, acrescente `--profile H5_BALANCED`, `H5_LOW_CPU` ou `H7_HIGH_QUALITY`. Todos os arquivos de cada versão são copiados para a mesma pasta para evitar misturar layouts de classe entre translation units.

O build VST local não configurou: `juceaide` do JUCE 8.0 falhou com o toolchain MinGW disponível. Não foi feita alteração de dependência para contornar isso. A validação de VST3/Standalone, APVTS, transições reais Normal/HQ e compilação WASM deve ser acompanhada no CI do PR, que usa MSVC/Emscripten. AU e timing/CPU em hardware embarcado não foram executados nesta máquina. Os WAVs oferecem audição comparativa, mas não substituem revisão auditiva dos presets em um DAW. O aumento do ring muda o período da memória congelada em Desktop; a restrição embarcada e o drift capturado por grain também são diferenças intencionais de comportamento, documentadas acima.
