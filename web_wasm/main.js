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
const btnProcessFreezeDemo = document.getElementById('btnProcessFreezeDemo');
const btnPlayProc = document.getElementById('btnPlayProc');
const btnStopProc = document.getElementById('btnStopProc');
const btnDownload = document.getElementById('btnDownload');
const loadStatus = document.getElementById('loadStatus');
const processStatus = document.getElementById('processStatus');
const presetSelect = document.getElementById('presetSelect');
const peakLevel = document.getElementById('peakLevel');
const canvas = document.getElementById('visualizer');
const canvasCtx = canvas ? canvas.getContext('2d') : null;

let analyser = null;
let animationFrameId = null;

function drawVisualizer() {
    if (!analyser || !canvasCtx || !canvas) return;

    // Match internal resolution to CSS size
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
    }

    const bufferLength = analyser.frequencyBinCount;
    const dataArray = new Uint8Array(bufferLength);
    analyser.getByteTimeDomainData(dataArray);

    canvasCtx.fillStyle = '#0E1117';
    canvasCtx.fillRect(0, 0, width, height);

    canvasCtx.lineWidth = 2;
    canvasCtx.strokeStyle = '#10B981';
    canvasCtx.beginPath();

    const sliceWidth = width * 1.0 / bufferLength;
    let x = 0;

    for (let i = 0; i < bufferLength; i++) {
        const v = dataArray[i] / 128.0;
        const y = v * height / 2;

        if (i === 0) {
            canvasCtx.moveTo(x, y);
        } else {
            canvasCtx.lineTo(x, y);
        }
        x += sliceWidth;
    }
    canvasCtx.lineTo(width, height / 2);
    canvasCtx.stroke();

    animationFrameId = requestAnimationFrame(drawVisualizer);
}

const paramInputs = [
    { key: 'mix', el: document.getElementById('param_0'), val: document.getElementById('val_mix'), idx: 0 },
    { key: 'texture', el: document.getElementById('param_1'), val: document.getElementById('val_texture'), idx: 1 },
    { key: 'freeze', el: document.getElementById('param_2'), val: document.getElementById('val_freeze'), idx: 2 },
    { key: 'feedback', el: document.getElementById('param_3'), val: document.getElementById('val_feedback'), idx: 3 },
    { key: 'size', el: document.getElementById('param_4'), val: document.getElementById('val_size'), idx: 4 },
    { key: 'diffusion', el: document.getElementById('param_5'), val: document.getElementById('val_diffusion'), idx: 5 },
    { key: 'modDepth', el: document.getElementById('param_6'), val: document.getElementById('val_moddepth'), idx: 6 },
    { key: 'modRate', el: document.getElementById('param_7'), val: document.getElementById('val_modrate'), idx: 7 },
    { key: 'damping', el: document.getElementById('param_8'), val: document.getElementById('val_damping'), idx: 8 },
    { key: 'lowDamping', el: document.getElementById('param_15'), val: document.getElementById('val_lowdamping'), idx: 15 },
    { key: 'tone', el: document.getElementById('param_9'), val: document.getElementById('val_tone'), idx: 9 },
    { key: 'shimmer', el: document.getElementById('param_12'), val: document.getElementById('val_shimmer'), idx: 12 },
    { key: 'preDelay', el: document.getElementById('param_13'), val: document.getElementById('val_predelay'), idx: 13 },
    { key: 'stereoWidth', el: document.getElementById('param_14'), val: document.getElementById('val_stereowidth'), idx: 14 },
    { key: 'inputGain', el: document.getElementById('param_10'), val: document.getElementById('val_ingain'), idx: 10 },
    { key: 'outputGain', el: document.getElementById('param_11'), val: document.getElementById('val_outgain'), idx: 11 }
];

const USER_PRESETS_STORAGE_KEY = 'greycloud.userPresets.v1';

const FACTORY_PRESETS = {
  SmallCloudRoom: { mix: 0.4, texture: 0.3, freeze: 0.0, feedback: 0.5, size: 0.35, diffusion: 0.6, modDepth: 0.2, modRate: 0.15, damping: 0.5, lowDamping: 0.5, tone: 0.6, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0, preDelay: 0.0, stereoWidth: 1.0 },
  BassAmbientWash: { mix: 0.36, texture: 0.42, freeze: 0.0, feedback: 0.62, size: 0.56, diffusion: 0.52, modDepth: 0.14, modRate: 0.15, damping: 0.78, lowDamping: 0.2, tone: 0.40, inputGain: 0.90, outputGain: 0.92, shimmer: 0.0, preDelay: 0.1, stereoWidth: 1.5 },
  FrozenOrganPad: { mix: 0.7, texture: 0.85, freeze: 1.0, feedback: 0.65, size: 0.7, diffusion: 0.8, modDepth: 0.4, modRate: 0.05, damping: 0.4, lowDamping: 0.6, tone: 0.45, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0, preDelay: 0.0, stereoWidth: 1.2 },
  GreyholeDelayVerb: { mix: 0.6, texture: 0.55, freeze: 0.0, feedback: 0.76, size: 0.76, diffusion: 0.70, modDepth: 0.4, modRate: 0.25, damping: 0.65, lowDamping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 0.90, shimmer: 0.0, preDelay: 0.2, stereoWidth: 1.0 },
  DarkLongCloud: { mix: 0.55, texture: 0.75, freeze: 0.0, feedback: 0.76, size: 0.84, diffusion: 0.66, modDepth: 0.3, modRate: 0.1, damping: 0.3, lowDamping: 0.4, tone: 0.3, inputGain: 0.72, outputGain: 0.72, shimmer: 0.0, preDelay: 0.3, stereoWidth: 1.0 },
  GlitchSmear: { mix: 0.5, texture: 0.05, freeze: 0.0, feedback: 0.5, size: 0.25, diffusion: 0.2, modDepth: 0.9, modRate: 0.8, damping: 0.5, lowDamping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0, preDelay: 0.0, stereoWidth: 1.0 },
  AlwaysOnSubtle: { mix: 0.25, texture: 0.2, freeze: 0.0, feedback: 0.3, size: 0.2, diffusion: 0.4, modDepth: 0.1, modRate: 0.1, damping: 0.5, lowDamping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0, preDelay: 0.05, stereoWidth: 0.8 },
  BrightCloud: { mix: 0.5, texture: 0.6, freeze: 0.0, feedback: 0.75, size: 0.6, diffusion: 0.7, modDepth: 0.6, modRate: 0.4, damping: 0.7, lowDamping: 0.8, tone: 0.8, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0, preDelay: 0.1, stereoWidth: 1.2 },
  ShimmerCloud: { mix: 0.55, texture: 0.55, freeze: 0.0, feedback: 0.58, size: 0.62, diffusion: 0.70, modDepth: 0.20, modRate: 0.12, damping: 0.55, lowDamping: 0.6, tone: 0.62, inputGain: 0.80, outputGain: 0.85, shimmer: 0.20, preDelay: 0.15, stereoWidth: 1.4 }
};

// Initialize WASM
if (typeof CloudGreyModule === 'function') {
    CloudGreyModule().then(Module => {
        wasmModule = Module;
        console.log("WASM Module Loaded!");
        
        // If an audio file was already loaded while we were waiting for WASM
        if (originalAudioBuffer) {
            initDSP(originalAudioBuffer.sampleRate);
            if (btnProcess) btnProcess.disabled = false;
            if (btnProcessFreezeDemo) btnProcessFreezeDemo.disabled = false;
        } else {
            loadStatus.textContent = "WASM Ready. Waiting for file...";
        }
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
        
        // Ensure DSP is initialized if WASM is already present
        if (wasmModule) {
            initDSP(originalAudioBuffer.sampleRate);
            btnProcess.disabled = false;
            if (btnProcessFreezeDemo) btnProcessFreezeDemo.disabled = false;
        } else {
            loadStatus.textContent += " (Waiting for WASM to load before processing...)";
        }
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
        let val = wasmModule._cgv_get_param(paramInputs[i].idx); // Fix it to use accurate id
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
    refreshPresetSelect('BassAmbientWash');
    applyPreset('BassAmbientWash');
    console.log(`DSP Initialized at ${sampleRate}Hz`);
}

// UI param listeners
paramInputs.forEach((item) => {
    item.el.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        item.val.textContent = val.toFixed(2);
        if (wasmModule && wasmModule._cgv_is_initialized()) {
            wasmModule._cgv_set_param(item.idx, val);
            
            // If freezing manually, switch preset dropdown to custom to avoid confusion
            if (presetSelect.value && !presetSelect.value.startsWith('user:')) {
                presetSelect.value = ""; 
            }
        }
    });
});

presetSelect.addEventListener('change', (e) => {
    if (!wasmModule || !wasmModule._cgv_is_initialized()) return;
    applyPreset(e.target.value);
});

// PRESET MANAGER

function loadUserPresets() {
    try {
        const data = localStorage.getItem(USER_PRESETS_STORAGE_KEY);
        if (data) return JSON.parse(data);
    } catch(e) {
        console.error("Failed to load user presets:", e);
    }
    return [];
}

function saveUserPresets(presets) {
    localStorage.setItem(USER_PRESETS_STORAGE_KEY, JSON.stringify(presets));
}

function getAllPresets() {
    return {
        ...FACTORY_PRESETS,
        ...Object.fromEntries(loadUserPresets().map(p => [`user:${p.name}`, p.params]))
    };
}

function refreshPresetSelect(selectedVal = null) {
    presetSelect.innerHTML = '<option value="">-- Custom --</option>';
    let currentSel = selectedVal || presetSelect.value;
    
    const factGrp = document.createElement('optgroup');
    factGrp.label = 'Factory Presets';
    for (const key in FACTORY_PRESETS) {
        const opt = document.createElement('option');
        opt.value = key;
        opt.textContent = key;
        factGrp.appendChild(opt);
    }
    presetSelect.appendChild(factGrp);

    const usrGrp = document.createElement('optgroup');
    usrGrp.label = 'User Presets';
    const users = loadUserPresets();
    for (const p of users) {
        const opt = document.createElement('option');
        opt.value = `user:${p.name}`;
        opt.textContent = p.name;
        usrGrp.appendChild(opt);
    }
    presetSelect.appendChild(usrGrp);
    
    if (currentSel) presetSelect.value = currentSel;
}

function setPresetStatus(msg, error=false) {
    const el = document.getElementById('presetStatus');
    if (el) {
        el.textContent = msg;
        el.style.color = error ? '#EF4444' : '#10B981';
    }
}

function getCurrentOfflineParams() {
    const params = {};
    for (const item of paramInputs) {
        params[item.key] = parseFloat(item.el.value);
    }
    return params;
}

function applyPreset(name) {
    if (!name) return;
    const all = getAllPresets();
    let p = all[name];
    if (!p) return;
    if (p.params) p = p.params;
    
    const nameInput = document.getElementById('presetNameInput');
    if (nameInput && name.startsWith('user:')) {
        nameInput.value = name.substring(5);
    } else if (nameInput) {
        nameInput.value = '';
    }
    
    for (const item of paramInputs) {
        if (p[item.key] !== undefined) {
            const val = p[item.key];
            item.el.value = val;
            item.val.textContent = val.toFixed(2);
            if (wasmModule && wasmModule._cgv_is_initialized()) {
                wasmModule._cgv_set_param(item.idx, val);
            }
        }
    }
}

document.getElementById('btnSavePreset')?.addEventListener('click', () => {
    let name = document.getElementById('presetNameInput').value.trim();
    if (!name) return setPresetStatus("Please enter a preset name.", true);
    if (name.length > 40) name = name.substring(0, 40);
    
    let presets = loadUserPresets();
    let existingIndex = presets.findIndex(p => p.name === name);
    if (existingIndex !== -1) {
        if (!confirm(`Overwrite existing preset "${name}"?`)) return;
    }
    
    const newPreset = {
        name: name,
        createdAt: new Date().toISOString(),
        updatedAt: new Date().toISOString(),
        params: getCurrentOfflineParams()
    };

    if (existingIndex !== -1) {
        presets[existingIndex] = newPreset;
    } else {
        presets.push(newPreset);
    }
    
    saveUserPresets(presets);
    refreshPresetSelect(`user:${name}`);
    setPresetStatus(`Saved user preset "${name}".`);
});

document.getElementById('btnUpdatePreset')?.addEventListener('click', () => {
    let sel = presetSelect.value;
    if (!sel || !sel.startsWith('user:')) {
        return setPresetStatus("Cannot overwrite Factory Presets. Save as a new name.", true);
    }
    let name = sel.substring(5);
    let presets = loadUserPresets();
    let idx = presets.findIndex(p => p.name === name);
    if (idx === -1) return setPresetStatus("Preset not found.", true);

    presets[idx].params = getCurrentOfflineParams();
    presets[idx].updatedAt = new Date().toISOString();
    saveUserPresets(presets);
    setPresetStatus(`Updated user preset "${name}".`);
});

document.getElementById('btnDeletePreset')?.addEventListener('click', () => {
    let sel = presetSelect.value;
    if (!sel || !sel.startsWith('user:')) {
        return setPresetStatus("Cannot delete Factory Presets.", true);
    }
    let name = sel.substring(5);
    if (!confirm(`Delete preset "${name}"?`)) return;

    let presets = loadUserPresets();
    presets = presets.filter(p => p.name !== name);
    saveUserPresets(presets);
    refreshPresetSelect('SmallCloudRoom');
    applyPreset('SmallCloudRoom');
    setPresetStatus(`Deleted user preset "${name}".`);
});

document.getElementById('btnExportPresets')?.addEventListener('click', () => {
    const list = loadUserPresets();
    if (list.length === 0) return setPresetStatus("No user presets to export.", true);
    
    const data = { app: "GreyCloud", version: 1, exportedAt: new Date().toISOString(), presets: list };
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'greycloud-presets.json';
    a.click();
    URL.revokeObjectURL(url);
    setPresetStatus("Presets exported.");
});

document.getElementById('presetImportInput')?.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    try {
        const text = await file.text();
        const data = JSON.parse(text);
        if (data.app !== "GreyCloud" || !Array.isArray(data.presets)) {
            throw new Error("Invalid preset file format.");
        }
        
        let existing = loadUserPresets();
        let imported = 0;
        for (const p of data.presets) {
            if (p.name && p.params) {
                let safeParams = {};
                for (const item of paramInputs) {
                    if (typeof p.params[item.key] === 'number') {
                        let v = p.params[item.key];
                        if (['inputGain','outputGain'].includes(item.key)) v = Math.max(0, Math.min(2, v));
                        else if (item.key === 'feedback') v = Math.max(0, Math.min(0.94, v));
                        else v = Math.max(0, Math.min(1, v));
                        safeParams[item.key] = v;
                    }
                }
                safeParams.freeze = typeof p.params.freeze === 'number' ? Math.max(0, Math.min(1, p.params.freeze)) : 0;
                
                let baseName = p.name;
                let finalName = baseName;
                let counter = 1;
                while (existing.some(req => req.name === finalName)) {
                    finalName = `${baseName} (imported ${counter++})`;
                }
                existing.push({ name: finalName, createdAt: p.createdAt, updatedAt: p.updatedAt, params: safeParams });
                imported++;
            }
        }
        saveUserPresets(existing);
        refreshPresetSelect();
        setPresetStatus(`Imported ${imported} user preset(s).`);
    } catch(err) {
        setPresetStatus(`Import failed: ${err.message}`, true);
    }
    e.target.value = ''; 
});

// Processing Audio
btnProcess.addEventListener('click', () => {
    if (!originalAudioBuffer || !wasmModule) return;
    
    processStatus.textContent = "Processing offline...";
    btnProcess.disabled = true;
    if (btnProcessFreezeDemo) btnProcessFreezeDemo.disabled = true;
    
    // Process async to avoid freezing UI for large files
    setTimeout(() => {
        processOffline(false);
    }, 10);
});

if (btnProcessFreezeDemo) {
    btnProcessFreezeDemo.addEventListener('click', () => {
        if (!originalAudioBuffer || !wasmModule) return;
        
        processStatus.textContent = "Processing Freeze Demo (0-2s off, 2-6s on)...";
        btnProcess.disabled = true;
        btnProcessFreezeDemo.disabled = true;
        
        setTimeout(() => {
            processOffline(true);
        }, 10);
    });
}

function processOffline(isFreezeDemo = false) {
    if (!wasmModule._cgv_is_initialized()) {
        processStatus.textContent = "Error: DSP not initialized.";
        processStatus.style.color = "red";
        btnProcess.disabled = false;
        if (btnProcessFreezeDemo) btnProcessFreezeDemo.disabled = false;
        return;
    }

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
    const BLOCK_SIZE = isFreezeDemo ? 256 : 1024;
    const wasmPtrL = wasmModule._malloc(BLOCK_SIZE * 4);
    const wasmPtrR = wasmModule._malloc(BLOCK_SIZE * 4);
    
    const ptrL_f32 = wasmPtrL >>> 2;
    const ptrR_f32 = wasmPtrR >>> 2;
    
    let maxPeak = 0.0;
    
    for (let offset = 0; offset < frames; offset += BLOCK_SIZE) {
        if (isFreezeDemo) {
            const timeS = offset / sr;
            let freezeValue = 0.0;
            if (timeS >= 2.0 && timeS < 6.0) {
                freezeValue = 1.0;
            }
            wasmModule._cgv_set_param(2, freezeValue);
            const uiFreezeVal = document.getElementById('val_freeze');
            const uiFreezeParam = document.getElementById('param_2');
            if (uiFreezeVal) uiFreezeVal.textContent = freezeValue.toFixed(1);
            if (uiFreezeParam) uiFreezeParam.value = freezeValue;
            
            // Note: switching parameters programmatically might unset preset conceptually,
            // but for offline render demonstration it's fine.
        }

        const len = Math.min(BLOCK_SIZE, frames - offset);
        
        // Copy to WASM heap securely
        const heapL = wasmModule.HEAPF32.subarray(ptrL_f32, ptrL_f32 + len);
        const heapR = wasmModule.HEAPF32.subarray(ptrR_f32, ptrR_f32 + len);
        
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
    
    // Ler Loop Energy e Safety
    const finalLoopEnergy = wasmModule._cgv_get_loop_energy();
    const finalSafetyGain = wasmModule._cgv_get_safety_gain();
    const elEnergy = document.getElementById('val_loopenergy');
    const elSafety = document.getElementById('val_safetygain');
    if(elEnergy) elEnergy.textContent = finalLoopEnergy.toFixed(3);
    if(elSafety) {
        elSafety.textContent = finalSafetyGain.toFixed(2);
        elSafety.style.color = finalSafetyGain < 0.95 ? '#EF4444' : '#10B981';
    }

    // Normalization logic
    const chkNorm = document.getElementById('chkNormalize');
    let normGain = 1.0;
    if (chkNorm && chkNorm.checked && maxPeak > 0) {
        // Target -0.1 dB ~ 0.9885
        const targetPeak = 0.9885;
        if (maxPeak > 0.0001) {
            normGain = targetPeak / maxPeak;
            for (let i = 0; i < frames; i++) {
                outL[i] *= normGain;
                outR[i] *= normGain;
            }
            maxPeak = targetPeak; // adjust max peak for display
            db = 20 * Math.log10(maxPeak + 1e-6);
        }
    }
    
    // Safety check: is it actually mute?
    let sumL = 0;
    for (let i = 0; i < Math.min(10000, outL.length); i++) sumL += outL[i] * outL[i];
    console.log("Processed. Max Peak:", maxPeak, "RMS of first 10000:", Math.sqrt(sumL / Math.min(10000, outL.length)));

    let pct = Math.max(0, Math.min(100, (db + 60) / 60 * 100));
    peakLevel.style.width = pct + '%';
    peakLevel.style.backgroundColor = db > 0 ? '#EF4444' : '#10B981';
    
    let wMsg = "";
    if (maxPeak >= 0.98) wMsg += " [Warning: near clipping]";
    if (finalSafetyGain < 0.95) wMsg += " [Safety limiter was active]";

    if (chkNorm && chkNorm.checked && normGain !== 1.0) {
        processStatus.textContent = `Completed! Max Peak: ${db.toFixed(1)} dB (Normalized, Gain: ${normGain.toFixed(2)}x)${wMsg}`;
    } else {
        processStatus.textContent = `Completed! Max Peak: ${db.toFixed(1)} dB${wMsg}`;
    }
    
    btnProcess.disabled = false;
    if (btnProcessFreezeDemo) btnProcessFreezeDemo.disabled = false;
    btnPlayProc.disabled = false;
    btnDownload.disabled = false;
}

// Playback Logic
function playBuffer(buffer) {
    stopPlayback();
    if(audioCtx.state === 'suspended') audioCtx.resume();
    
    currentSource = audioCtx.createBufferSource();
    currentSource.buffer = buffer;
    
    if (!analyser) {
        analyser = audioCtx.createAnalyser();
        analyser.fftSize = 2048;
    }
    
    currentSource.connect(analyser);
    analyser.connect(audioCtx.destination);
    
    currentSource.start(0);
    currentSource.onended = () => stopPlayback();
    
    if (animationFrameId) cancelAnimationFrame(animationFrameId);
    drawVisualizer();
}

function stopPlayback() {
    if (animationFrameId) {
        cancelAnimationFrame(animationFrameId);
        animationFrameId = null;
        if (canvasCtx && canvas) {
            canvasCtx.fillStyle = '#0E1117';
            canvasCtx.fillRect(0, 0, canvas.width, canvas.height);
            canvasCtx.lineWidth = 2;
            canvasCtx.strokeStyle = '#30363D';
            canvasCtx.beginPath();
            canvasCtx.moveTo(0, canvas.height / 2);
            canvasCtx.lineTo(canvas.width, canvas.height / 2);
            canvasCtx.stroke();
        }
    }
    
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
    
    // In strict iframes, a.click() may be blocked. Expose a real link inside the panel:
    btnDownload.style.display = "none";
    
    let existingLink = document.getElementById("directDownloadLink");
    if (existingLink) { existingLink.remove(); }
    
    const a = document.createElement('a');
    a.id = "directDownloadLink";
    a.href = url;
    a.download = "cloudgrey_processed.wav";
    a.textContent = "✅ Ready! Click here to Download WAV";
    a.style.display = "inline-block";
    a.style.backgroundColor = "#059669";
    a.style.color = "white";
    a.style.padding = "0.5rem 1rem";
    a.style.borderRadius = "4px";
    a.style.textDecoration = "none";
    a.style.fontWeight = "bold";
    a.style.marginLeft = "1rem";
    
    btnDownload.parentElement.appendChild(a);
    
    // We cannot immediately revoke because the user needs to manually click the link
    // URL.revokeObjectURL(url);
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
