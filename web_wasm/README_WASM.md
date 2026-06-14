# CloudGreyVerb - WebAssembly / Web Audio API Port

Este diretório contém a estrutura offline para compilar o núcleo DSP escrito em C++ (`cloud_grey_verb.cpp`) via Emscripten, rodando-o no navegador. 

Ele atua como uma bancada de testes de sonoridade 100% livre de microcontroladores (perfeito para modelagem e escuta).

## 1. O que este módulo faz
* Carrega qualquer áudio local na interface.
* Inicializa a predefinição (alloc) via WASM heap.
* Carrega o áudio e processa rapidamente em blocos de `1024` frames enviando entre o array JavaScript (`Float32Array`) e as funções mapeadas em `cloud_grey_wasm.cpp`.
* Permite equalizar os parâmetros e testar os presets originais (mapeados idênticos ao firmware baremetal).
* Exporta o resultado processado em disco (WAV 16-bit).

## 2. Como Buildar

Você precisará do **Emscripten SDK** ativo (emcc) no seu sistema local:

```bash
cd web_wasm
chmod +x build.sh
./build.sh
```
*(Arquivos pré-buildeados não vão no Git para evitar lixo binário, a geração será sempre local)*.

Isto vai gerar: `cloud_grey.js` e `cloud_grey.wasm`.

## 3. Como Rodar no Navegador
Devido a políticas de CORS em carregamento de arquivo `.wasm`, não abra o HTML diretamente com dois-cliques. Rode um servidor de arquivos nulo:

```bash
python3 -m http.server 8080
```
Em seguida, abra `http://localhost:8080/index.html`.

## 4. O Caminho para Modo Tempo-Real (Próxima Etapa)

O processador atual testa as "caudas" operando **Offline** (renderizando como processo de estúdio). Para tocar guitarra real-time plugeada na placa de som, usaremos **AudioWorklet**.

A estrutura WASM Wrapper atual já está projetada para isso. O que precisaremos na Versão 2:
1. `audio_worklet_processor.js` extending `AudioWorkletProcessor`.
2. O Worklet instancia a memória wasm dentro dele via `WebAssembly.instantiate()`.
3. O `process(inputs, outputs, params)` do worklet chama `_cgv_process` em blocos curtos (128 amostras fornecidas pelo WebAudio).
4. Sincronia de parâmetros UI enviando `port.postMessage({ id: x, val: y })` que invocarão `_cgv_set_param()` inter-threads sem estourar o clock.
