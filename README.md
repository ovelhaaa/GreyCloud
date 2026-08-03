# Nimbus Reverb / GreyCloud

Reverb granular estéreo em C++ que combina nuvens de grãos, difusão por all-pass e uma Feedback Delay Network (FDN) 4×4 modulada. **CloudGreyVerb** é o núcleo DSP compartilhado; **Nimbus** é o nome do plugin e de sua interface atual.

O projeto está em **beta funcional**: o core, o plugin JUCE e as duas bancadas WebAssembly já estão implementados, mas ainda passam por validação auditiva, testes em hosts/DAWs e ajustes de desempenho para hardware embarcado.

> [Abrir o Nimbus no navegador — modo AudioWorklet ao vivo](https://ovelhaaa.github.io/GreyCloud/web_wasm/live.html)

## Estado atual

| Componente | Estado | O que está disponível |
| --- | --- | --- |
| Core C++ | Beta funcional | Processamento `float` 32-bit, FDN Hadamard 4×4, buffer externo, freeze suave e hard freeze, granular forward/reverse, shimmer, pre-delay, modulação, tone/damping e controle de energia no feedback |
| Plugin Nimbus | Em desenvolvimento | JUCE 8, VST3, AU e Standalone configurados; interface própria, 10 presets, automação de parâmetros, estado do host, sync por BPM, modo HQ com oversampling 2x e troca de preset com fade/reset da cauda |
| WebAssembly offline | Funcional | Importa áudio, renderiza pelo mesmo core C++, mostra telemetria e exporta WAV |
| WebAssembly ao vivo | Funcional | AudioWorklet com arquivo ou microfone/interface, freeze momentâneo/latch, telemetria, presets locais, importação/exportação, Web MIDI e renderização para WAV |
| STM32H5/H7 | Core pronto para integração | Perfis de CPU/memória e guia de I2S/DMA; este repositório ainda não inclui firmware, BSP ou driver de codec de uma placa específica |
| Catálogo React | Funcional | Visualizador dos fontes e documentos do core; não é a interface de processamento de áudio |

## Recursos implementados

- Motor granular estéreo com textura, varredura dos grãos e mistura forward/reverse.
- FDN 4×4 com matriz Hadamard normalizada, quatro tempos/modulações decorrelacionados e um all-pass por linha.
- Freeze suavizado e hard freeze.
- Shimmer com razões de `-1 oitava`, `+5ª`, `+1 oitava`, `+1 oitava + 5ª` e `+2 oitavas`.
- Pre-delay de até 200 ms, largura estéreo, filtros de damping/low damping e tilt de tonalidade.
- Suavização de parâmetros, soft clipping, ducking e Safety Energy Guard contra feedback descontrolado.
- Perfis de compilação para H5 de baixo consumo, H5 balanceado, H7 de alta qualidade e desktop.
- Presets de fábrica: `SmallCloudRoom`, `BassAmbientWash`, `FrozenOrganPad`, `GreyholeDelayVerb`, `DarkLongCloud`, `GlitchSmear`, `AlwaysOnSubtle`, `BrightCloud` e `ShimmerCloud`; o plugin também inclui `ReverseSmear`.

O core não aloca memória durante o processamento. A aplicação hospedeira fornece o buffer persistente na inicialização e é responsável por I/O, DMA, conversão de amostras e sincronização entre threads.

## Estrutura do repositório

```text
src/dsp/       Núcleo CloudGreyVerb, utilitários e testes C++
src/docs/      Notas do core e guia de integração no STM32H5
vst/           Plugin Nimbus em JUCE (VST3/AU/Standalone)
web_wasm/      Interfaces offline e AudioWorklet + wrapper Emscripten
src/           Catálogo React dos fontes e documentos
.github/       Builds do plugin para Windows e deploy do GitHub Pages
```

## Plugin Nimbus

### Pré-requisitos

- CMake 3.20 ou mais recente.
- Compilador C++17 (MSVC, Clang ou GCC).
- Acesso à internet na primeira configuração; o CMake baixa o JUCE 8.0.0 com `FetchContent`.

### Build

Na raiz do repositório:

```bash
cmake -S vst -B vst/build -DCMAKE_BUILD_TYPE=Release
cmake --build vst/build --config Release --parallel 4
```

Os artefatos ficam sob `vst/build/NimbusReverbPlugin_artefacts/Release/`. O formato AU só é gerado em macOS. O workflow `Build VST Plugins` compila, no Windows, o VST3 e o aplicativo Standalone e os publica como artefatos da execução.

## WebAssembly

### Pré-requisitos

- Emscripten SDK ativo, com `emcc` no `PATH`.
- Bash para executar os scripts.
- Um servidor HTTP local, como o módulo `http.server` do Python.

### Compilar e executar

```bash
cd web_wasm
./build.sh
./build_live.sh
python -m http.server 8080
```

Abra uma das interfaces:

- `http://localhost:8080/index.html` — renderização offline.
- `http://localhost:8080/live.html` — processamento em tempo real com AudioWorklet.

As duas builds usam `SINGLE_FILE=1`: o payload WASM fica incorporado em `cloud_grey.js` e `cloud_grey_live.js`. Esses arquivos são gerados localmente ou pelo workflow de deploy e não são versionados.

AudioWorklet, microfone e Web MIDI exigem um contexto seguro (`localhost` ou HTTPS). Ao testar microfone/instrumento, use fones de ouvido para evitar realimentação acústica.

## Catálogo de código e documentação

A aplicação React na raiz permite navegar, copiar e baixar os principais arquivos do core e ler a documentação no navegador. Ela não usa a API Gemini e não requer chave de API.

```bash
npm install
npm run dev
```

Outros comandos disponíveis:

```bash
npm run lint
npm run build
npm run preview
```

## Testes do DSP

Os testes C++ cobrem ortogonalidade e conservação de energia da matriz FDN, abertura da cauda estéreo, limites do buffer externo, impulso/silêncio, entradas extremas, mudanças bruscas de parâmetros, NaN/Inf e os quatro perfis de compilação. No Windows, com `g++` disponível:

```powershell
cd src/dsp
.\run_all_profiles.bat
```

Também existem programas de teste independentes para smoke test, abuse test e verificação A/B em `src/dsp/`.

### Comparativo de respostas ao impulso da FDN

A bancada de IR compila o mesmo perfil H5 Balanced duas vezes, alterando somente a ordem da FDN, e gera WAVs e métricas para todos os presets:

```powershell
python src/dsp/run_fdn_ir_analysis.py
```

Por padrão, a análise usa 48 kHz, oito segundos por preset e o mesmo buffer externo de três segundos-mono usado pelas interfaces WASM. Os artefatos ficam em `build/fdn-ir/`:

- `comparison.md`: relatório comparativo com links para audição.
- `comparison.csv`: valores 2×2, 4×4 e deltas em formato legível por máquina.
- `fdn2/` e `fdn4/`: respostas ao impulso WAV float32 e métricas individuais.

As medidas incluem RT60 broadband e por bandas, curva de cauda, correlação estéreo, energia lateral M/S, C80, densidade inicial, pico e atuação do Safety Guard. Use `--help` para selecionar preset, duração, sample rate, memória ou desativar a gravação dos WAVs.

## Perfis do core

| Macro | Grãos | Difusores | Ordem da FDN | All-pass por linha | Shimmer padrão |
| --- | ---: | ---: | ---: | ---: | --- |
| `CLOUD_GREY_PROFILE_H5_LOW_CPU` | 3 | 2 | 2×2 | 0 | Desativado |
| `CLOUD_GREY_PROFILE_H5_BALANCED` | 4 | 4 | 4×4 | 1 | Desativado |
| `CLOUD_GREY_PROFILE_H7_HIGH_QUALITY` | 4 | 4 | 4×4 | 1 | Ativado |
| `CLOUD_GREY_PROFILE_DESKTOP_STUDIO` | 6 | 4 | 4×4 | 1 | Ativado |

Sem uma macro explícita, o core usa `CLOUD_GREY_PROFILE_H5_BALANCED`. As builds WebAssembly usam esse perfil, mas ativam o shimmer explicitamente; o plugin usa `CLOUD_GREY_PROFILE_DESKTOP_STUDIO`. Apenas `H5_LOW_CPU` conserva a antiga malha ortogonal 2×2 para reduzir leituras, filtros e uso de memória.

## Limitações conhecidas

- Não há instalador nem release binária versionada no repositório; os artefatos são produzidos localmente ou pelo GitHub Actions.
- A integração embarcada depende do firmware, layout de memória, codec e pipeline DMA de cada placa.
- O modo ao vivo depende do suporte do navegador a AudioWorklet; Web MIDI e captura de áudio também dependem de permissões do navegador.

## Documentação complementar

- [Guia do modo AudioWorklet](web_wasm/AUDIOWORKLET.md)
- [Build WebAssembly e renderização offline](web_wasm/README_WASM.md)
- [Integração no STM32H5](src/docs/STM32H5_MIGRATION.md)
- [Uso e testes de escuta do core](src/docs/CLOUD_GREY_VERB_CORE.md)
- [Detalhes do plugin JUCE](vst/README.md)

Este README resume o estado atual do código. Em caso de divergência com seções de roadmap nos documentos internos, considere o código e este status como referência mais recente.
