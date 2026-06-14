// main.js - CloudGreyVerb WASM Host

let wasmModule = null;
let audioCtx = new (window.AudioContext || window.webkitAudioContext)();
let originalAudioBuffer = null;
let processedAudioBuffer = null;
let currentSource = null;

// UI Elements
const fileInput = document.getElementById('audioInput');
const btnPlayOrig = document.getElementById('btnPlayOrig');
const btnStopOrig = document.getElementById('btnStopOrig');
const btnProcess = document.getElementById('btnProcess');
const btnPlayProc = document.getElementById('btnPlayProc');
const btnStopProc = document.getElementById('btnStopProc');
const btnDownload = document.getElementById('btnDownload');
const loadStatus = document.getElementById('loadStatus');
const processStatus = document.getElementById('processStatus');
const presetSelect = document.getElementById('presetSelect');
const peakLevel = document.getElementById('peakLevel');

const paramInputs = [
    { el: document.getElementById('param_0'), val: document.getElementById('val_mix') },
    { el: document.getElementById('param_1'), val: document.getElementById('val_texture') },
    { el: document.getElementById('param_2'), val: document.getElementById('val_freeze') },
    { el: document.getElementById('param_3'), val: document.getElementById('val_feedback') },
    { el: document.getElementById('param_4'), val: document.getElementById('val_size') },
    { el: document.getElementById('param_5'), val: document.getElementById('val_diffusion') },
    { el: document.getElementById('param_6'), val: document.getElementById('val_moddepth') },
    { el: document.getElementById('param_7'), val: document.getElementById('val_modrate') },
    { el: document.getElementById('param_8'), val: document.getElementById('val_damping') },
    { el: document.getElementById('param_9'), val: document.getElementById('val_tone') },
    { el: document.getElementById('param_10'), val: document.getElementById('val_ingain') },
    { el: document.getElementById('param_11'), val: document.getElementById('val_outgain') }
];

// Initialize WASM
if (typeof CloudGreyModule === 'function') {
    CloudGreyModule().then(Module => {
        wasmModule = Module;
        console.log("WASM Module Loaded!");
        // We will initialize DSP when we know the file's sample rate (or default)
    });
} else {
    loadStatus.textContent = "Error: WASM module script not loaded. Run build.sh first.";
    loadStatus.style.color = "red";
}

// 1. Load Audio File
fileInput.addEventListener('change', async (e) => {
    if (e.target.files.length === 0) return;
    const file = e.target.files[0];
    
    loadStatus.textContent = "Decoding audio...";
    
    try {
        const arrayBuffer = await file.arrayBuffer();
        originalAudioBuffer = await audioCtx.decodeAudioData(arrayBuffer);
        
        loadStatus.textContent = `Loaded: ${originalAudioBuffer.duration.toFixed(1)}s @ ${originalAudioBuffer.sampleRate}Hz`;
        btnPlayOrig.disabled = false;
        btnProcess.disabled = false;
        
        // Ensure DSP is initialized for this sample rate
        initDSP(originalAudioBuffer.sampleRate);
    } catch (err) {
        console.error(err);
        loadStatus.textContent = "Error decoding audio file.";
        loadStatus.style.color = "red";
    }
});

// Sync Sliders to C++ state
function syncSlidersToDSP() {
    if (!wasmModule || !wasmModule._cgv_is_initialized()) return;
    for (let i = 0; i < paramInputs.length; i++) {
        let val = wasmModule._cgv_get_param(i);
        paramInputs[i].el.value = val;
        paramInputs[i].val.textContent = val.toFixed(2);
    }
}

// Init DSP core
function initDSP(sampleRate) {
    if (!wasmModule) return;
    // Allocate 3 seconds of equivalent mono memory (48000 * 3 = 144000 floats * sampleRate ratio)
    const memoryFloats = Math.floor(sampleRate * 3.0);
    const success = wasmModule._cgv_init(sampleRate, memoryFloats);
    if (!success) {
        console.error("DSP Init Failed - Memory Error");
        return;
    }
    
    // Set initial preset
    wasmModule._cgv_set_preset(parseInt(presetSelect.value));
    syncSlidersToDSP();
    console.log(`DSP Initialized at ${sampleRate}Hz`);
}

// UI param listeners
paramInputs.forEach((item, index) => {
    item.el.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        item.val.textContent = val.toFixed(2);
        if (wasmModule && wasmModule._cgv_is_initialized()) {
            wasmModule._cgv_set_param(index, val);
            
            // If freezing manually, switch preset dropdown to custom to avoid confusion
            presetSelect.value = "-1"; // Out of bounds but visually clears selection logic
        }
    });
});

presetSelect.addEventListener('change', (e) => {
    if (!wasmModule || !wasmModule._cgv_is_initialized()) return;
    let presetId = parseInt(e.target.value);
    if (presetId >= 0) {
        wasmModule._cgv_set_preset(presetId);
        syncSlidersToDSP();
    }
});

// Processing Audio
btnProcess.addEventListener('click', () => {
    if (!originalAudioBuffer || !wasmModule) return;
    
    processStatus.textContent = "Processing offline...";
    btnProcess.disabled = true;
    
    // Process async to avoid freezing UI for large files
    setTimeout(() => {
        processOffline();
    }, 10);
});

function processOffline() {
    const sr = originalAudioBuffer.sampleRate;
    const frames = originalAudioBuffer.length;
    const hasRight = originalAudioBuffer.numberOfChannels > 1;
    
    // Get Audio Data
    const inL = originalAudioBuffer.getChannelData(0);
    const inR = hasRight ? originalAudioBuffer.getChannelData(1) : inL;
    
    // Allocate Output Buffer in JS
    processedAudioBuffer = audioCtx.createBuffer(2, frames, sr);
    const outL = processedAudioBuffer.getChannelData(0);
    const outR = processedAudioBuffer.getChannelData(1);
    
    // Reset DSP state
    wasmModule._cgv_reset();
    
    // Process in Chunks to save WASM Heap Memory
    const BLOCK_SIZE = 1024;
    const wasmPtrL = wasmModule._malloc(BLOCK_SIZE * 4);
    const wasmPtrR = wasmModule._malloc(BLOCK_SIZE * 4);
    
    let maxPeak = 0.0;
    
    for (let offset = 0; offset < frames; offset += BLOCK_SIZE) {
        const len = Math.min(BLOCK_SIZE, frames - offset);
        
        // Copy to WASM heap
        const heapL = new Float32Array(wasmModule.HEAPF32.buffer, wasmPtrL, len);
        const heapR = new Float32Array(wasmModule.HEAPF32.buffer, wasmPtrR, len);
        
        heapL.set(inL.subarray(offset, offset + len));
        heapR.set(inR.subarray(offset, offset + len));
        
        // Process block
        wasmModule._cgv_process(wasmPtrL, wasmPtrR, len);
        
        // Copy back to output buffer
        outL.set(heapL, offset);
        outR.set(heapR, offset);
        
        // UI Peak logic
        const blockPeak = wasmModule._cgv_get_peak();
        if (blockPeak > maxPeak) maxPeak = blockPeak;
    }
    
    // Free WASM structures
    wasmModule._free(wasmPtrL);
    wasmModule._free(wasmPtrR);
    
    // Update Peak Meter visually
    let db = 20 * Math.log10(maxPeak + 1e-6);
    let pct = Math.max(0, Math.min(100, (db + 60) / 60 * 100));
    peakLevel.style.width = pct + '%';
    peakLevel.style.backgroundColor = db > 0 ? '#EF4444' : '#10B981';
    
    processStatus.textContent = `Completed! Max Peak: ${db.toFixed(1)} dB`;
    btnProcess.disabled = false;
    btnPlayProc.disabled = false;
    btnDownload.disabled = false;
}

// Playback Logic
function playBuffer(buffer) {
    stopPlayback();
    if(audioCtx.state === 'suspended') audioCtx.resume();
    currentSource = audioCtx.createBufferSource();
    currentSource.buffer = buffer;
    currentSource.connect(audioCtx.destination);
    currentSource.start(0);
    currentSource.onended = () => stopPlayback();
}

function stopPlayback() {
    if (currentSource) {
        currentSource.stop();
        currentSource.disconnect();
        currentSource = null;
    }
    btnStopOrig.disabled = true;
    btnStopProc.disabled = true;
}

btnPlayOrig.addEventListener('click', () => {
    playBuffer(originalAudioBuffer);
    btnStopOrig.disabled = false;
});
btnPlayProc.addEventListener('click', () => {
    playBuffer(processedAudioBuffer);
    btnStopProc.disabled = false;
});
btnStopOrig.addEventListener('click', stopPlayback);
btnStopProc.addEventListener('click', stopPlayback);

// WAV Export
btnDownload.addEventListener('click', () => {
    if (!processedAudioBuffer) return;
    const wavBlob = audioBufferToWav(processedAudioBuffer);
    const url = URL.createObjectURL(wavBlob);
    const a = document.createElement('a');
    a.href = url;
    a.download = "cloudgrey_processed.wav";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
});

function audioBufferToWav(buffer) {
    const numChannels = buffer.numberOfChannels;
    const sampleRate = buffer.sampleRate;
    const format = 1; // PCM
    const bitDepth = 16;
    const bytesPerSample = bitDepth / 8;
    const blockAlign = numChannels * bytesPerSample;
    const dataByteLength = buffer.length * blockAlign;
    
    const arrayBuffer = new ArrayBuffer(44 + dataByteLength);
    const view = new DataView(arrayBuffer);
    
    const writeString = (view, offset, string) => {
        for (let i = 0; i < string.length; i++) {
            view.setUint8(offset + i, string.charCodeAt(i));
        }
    };
    
    // RIFF chunk descriptor
    writeString(view, 0, 'RIFF');
    view.setUint32(4, 36 + dataByteLength, true);
    writeString(view, 8, 'WAVE');
    
    // FMT sub-chunk
    writeString(view, 12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, format, true);
    view.setUint16(22, numChannels, true);
    view.setUint32(24, sampleRate, true);
    view.setUint32(28, sampleRate * blockAlign, true);
    view.setUint16(32, blockAlign, true);
    view.setUint16(34, bitDepth, true);
    
    // Data sub-chunk
    writeString(view, 36, 'data');
    view.setUint32(40, dataByteLength, true);
    
    // Interleave and Write Data
    let offset = 44;
    const inL = buffer.getChannelData(0);
    const inR = numChannels > 1 ? buffer.getChannelData(1) : inL;
    
    for (let i = 0; i < buffer.length; i++) {
        // Left
        let sampleL = Math.max(-1, Math.min(1, inL[i])); // clamp
        view.setInt16(offset, sampleL < 0 ? sampleL * 0x8000 : sampleL * 0x7FFF, true);
        offset += 2;
        // Right
        if (numChannels > 1) {
            let sampleR = Math.max(-1, Math.min(1, inR[i])); // clamp
            view.setInt16(offset, sampleR < 0 ? sampleR * 0x8000 : sampleR * 0x7FFF, true);
            offset += 2;
        }
    }
    
    return new Blob([view], { type: 'audio/wav' });
}
