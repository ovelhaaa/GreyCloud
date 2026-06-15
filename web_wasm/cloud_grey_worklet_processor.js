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
  outputGain: 11
};

class CloudGreyWorkletProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.module = null;
    this.leftPtr = null;
    this.rightPtr = null;
    this.bufferSize = 0;
    this.processCount = 0;

    this.port.onmessage = async (event) => {
      const { type } = event.data;
      if (type === 'init') {
        try {
          const { wasmBytes, memoryFloats, presetId } = event.data;
          this.module = await CloudGreyModule({
            wasmBinary: wasmBytes
          });
          
          this.module._cgv_init(sampleRate, memoryFloats);
          if (presetId !== undefined) {
            this.module._cgv_set_preset(presetId);
          }
          
          this.port.postMessage({ type: 'ready' });
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
      { name: 'outputGain', defaultValue: 1, minValue: 0, maxValue: 2, automationRate: 'k-rate' }
    ];
  }

  allocateBuffers(frames) {
    if (this.bufferSize !== frames) {
      if (this.leftPtr) this.module._free(this.leftPtr);
      if (this.rightPtr) this.module._free(this.rightPtr);

      this.leftPtr = this.module._malloc(frames * 4);
      this.rightPtr = this.module._malloc(frames * 4);
      this.bufferSize = frames;
    }
  }

  process(inputs, outputs, parameters) {
    if (!this.module || !this.module._cgv_is_initialized()) return true;

    const input = inputs[0] || [];
    const inL = input[0];
    const inR = input.length > 1 ? input[1] : inL;
    const output = outputs[0];
    const outL = output[0];
    const outR = output.length > 1 ? output[1] : output[0];

    // Se n\u00e3o h\u00e1 canal de sa\u00edda dispon\u00edvel
    if (!outL || !outR) return true;

    const frames = outL.length;

    // Sincronizar par\u00e2metros
    const getParam = (name) => {
      const p = parameters[name];
      return p && p.length ? p[0] : 0;
    };

    for (const [name, id] of Object.entries(PARAM_IDS)) {
      const val = getParam(name);
      // Freeze latch ou momentary (UI manda o value via AudioParam, o worklet processa)
      this.module._cgv_set_param(id, val);
    }

    this.allocateBuffers(frames);
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
