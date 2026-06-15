import CloudGreyModule from './cloud_grey_live.js';

// Mapeamento dos parâmetros do CloudGreyVerb
const PARAM_IDS = {
  mix: 0,
  texture: 1,
  freeze: 2,
  feedback: 3,
  size: 4,
  diffusion: 5,
  modDepth: 6,
  modRate: 7,
  damping: 8,
  tone: 9,
  inputGain: 10,
  outputGain: 11,
  shimmer: 12
};

class CloudGreyWorkletProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.module = null;
    this.leftPtr = null;
    this.rightPtr = null;
    this.bufferSize = 0;
    this.processCount = 0;
    this.lastParams = {};

    this.port.onmessage = async (event) => {
      const { type } = event.data;
      if (type === 'init') {
        try {
          const { wasmBytes, memoryFloats, presetId } = event.data;
          
          if (!wasmBytes || wasmBytes.byteLength === 0) {
            throw new Error('Missing or empty wasm bytes');
          }
          if (!memoryFloats || memoryFloats < 24000) {
            throw new Error('Invalid memoryFloats for CloudGreyVerb');
          }

          this.module = await CloudGreyModule({
            wasmBinary: wasmBytes
          });
          
          const initOk = this.module._cgv_init(sampleRate, memoryFloats);
          
          if (initOk === 1 && this.module._cgv_is_initialized()) {
            if (presetId !== undefined) {
              this.module._cgv_set_preset(presetId);
            }
            this.port.postMessage({ type: 'ready' });
          } else {
            this.port.postMessage({ type: 'error', message: 'DSP Engine failed to initialize in WASM' });
          }
        } catch (error) {
          this.port.postMessage({
            type: 'error',
            message: error.message,
            stack: error.stack
          });
        }
      } else if (type === 'preset' && this.module) {
        this.module._cgv_set_preset(event.data.presetId);
      } else if (type === 'reset' && this.module) {
        this.module._cgv_reset();
      } else if (type === 'setParam' && this.module) {
        const id = PARAM_IDS[event.data.name];
        if (id !== undefined) {
          this.module._cgv_set_param(id, event.data.value);
        }
      }
    };
  }

  static get parameterDescriptors() {
    return [
      { name: 'mix', defaultValue: 0.35, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'texture', defaultValue: 0.4, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'freeze', defaultValue: 0, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'feedback', defaultValue: 0.7, minValue: 0, maxValue: 0.94, automationRate: 'k-rate' },
      { name: 'size', defaultValue: 0.6, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'diffusion', defaultValue: 0.5, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'modDepth', defaultValue: 0.15, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'modRate', defaultValue: 0.15, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'damping', defaultValue: 0.8, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'tone', defaultValue: 0.4, minValue: 0, maxValue: 1, automationRate: 'k-rate' },
      { name: 'inputGain', defaultValue: 1, minValue: 0, maxValue: 2, automationRate: 'k-rate' },
      { name: 'outputGain', defaultValue: 1, minValue: 0, maxValue: 2, automationRate: 'k-rate' },
      { name: 'shimmer', defaultValue: 0, minValue: 0, maxValue: 1, automationRate: 'k-rate' }
    ];
  }

  reportError(stage, error) {
    this.port.postMessage({
      type: 'error',
      stage: stage,
      message: error.message
    });
  }

  freeBuffers() {
    if (!this.module) return;
    if (this.leftPtr) {
      this.module._free(this.leftPtr);
      this.leftPtr = null;
    }
    if (this.rightPtr) {
      this.module._free(this.rightPtr);
      this.rightPtr = null;
    }
    this.bufferSize = 0;
  }

  allocateBuffers(frames) {
    if (!this.module || frames <= 0) return false;

    if (
      this.bufferSize === frames &&
      this.leftPtr &&
      this.rightPtr
    ) {
      return true;
    }

    this.freeBuffers();

    const bytes = frames * 4;
    this.leftPtr = this.module._malloc(bytes);
    this.rightPtr = this.module._malloc(bytes);

    if (!this.leftPtr || !this.rightPtr) {
      this.freeBuffers();
      this.reportError('buffer-alloc', new Error(`Failed to allocate ${frames} frames`));
      return false;
    }

    this.bufferSize = frames;
    return true;
  }

  process(inputs, outputs, parameters) {
    const output = outputs[0];
    if (!output) return true;

    if (!this.module || !this.module._cgv_is_initialized()) {
      for (const ch of output) {
        if (ch) ch.fill(0);
      }
      return true;
    }

    const outL = output[0];
    const outR = output.length > 1 ? output[1] : output[0];

    // Se n\u00e3o h\u00e1 canal de sa\u00edda dispon\u00edvel
    if (!outL || !outR) return true;

    const input = inputs[0] || [];
    const inL = input[0];
    const inR = input.length > 1 ? input[1] : inL;

    const frames = outL.length;

    // Sincronizar par\u00e2metros
    const getParam = (name) => {
      const p = parameters[name];
      return p && p.length ? p[0] : 0;
    };

    for (const [name, id] of Object.entries(PARAM_IDS)) {
      const val = getParam(name);
      const prev = this.lastParams[name];

      if (prev === undefined || Math.abs(prev - val) > 1e-6) {
        this.module._cgv_set_param(id, val);
        this.lastParams[name] = val;
      }
    }

    if (!this.allocateBuffers(frames)) {
      return true; // continue silently rather than failing completely
    }
    const heap = this.module.HEAPF32;

    if (inL && inL.length > 0) {
      heap.set(inL, this.leftPtr >> 2);
      heap.set(inR || inL, this.rightPtr >> 2);
    } else {
      // Input sil\u00eancio
      heap.fill(0, this.leftPtr >> 2, (this.leftPtr >> 2) + frames);
      heap.fill(0, this.rightPtr >> 2, (this.rightPtr >> 2) + frames);
    }

    // Processamento
    this.module._cgv_process(this.leftPtr, this.rightPtr, frames);

    outL.set(heap.subarray(this.leftPtr >> 2, (this.leftPtr >> 2) + frames));
    outR.set(heap.subarray(this.rightPtr >> 2, (this.rightPtr >> 2) + frames));

    // Telemetria
    this.processCount++;
    if (this.processCount % 20 === 0) {
      this.port.postMessage({
        type: 'meter',
        peak: this.module._cgv_get_peak(),
        freezeState: this.module._cgv_get_freeze_state(),
        loopEnergy: this.module._cgv_get_loop_energy(),
        safetyGain: this.module._cgv_get_safety_gain()
      });
    }

    return true;
  }
}

registerProcessor('cloud-grey-worklet-processor', CloudGreyWorkletProcessor);
