// GreyCloud - Live AudioWorklet Main

const WORKLET_URL = new URL('./cloud_grey_worklet_processor.js', import.meta.url).href;

let audioCtx = null;
let cloudNode = null;

let micStream = null;
let micSourceNode = null;

let fileBuffer = null;
let fileSourceNode = null;
let isPlayingFile = false;
let pauseTime = 0;
let startTime = 0;

// UI Elements
const btnPower = document.getElementById('btnPower');
const sourceRadios = document.getElementsByName('sourceMode');
const micAlert = document.getElementById('micAlert');
const fileControls = document.getElementById('fileControls');

const fileInput = document.getElementById('fileInput');
const btnPlayFile = document.getElementById('btnPlayFile');
const btnPauseFile = document.getElementById('btnPauseFile');
const btnStopFile = document.getElementById('btnStopFile');
const chkLoop = document.getElementById('chkLoop');

const btnFreezeHold = document.getElementById('btnFreezeHold');
const chkFreezeLatch = document.getElementById('chkFreezeLatch');
const btnResetTail = document.getElementById('btnResetTail');

const presetSelect = document.getElementById('presetSelect');
const presetAlert = document.getElementById('presetAlert');

const FACTORY_PRESETS = {
  SmallCloudRoom: { mix: 0.4, texture: 0.3, freeze: 0.0, feedback: 0.5, size: 0.35, diffusion: 0.6, modDepth: 0.2, modRate: 0.15, damping: 0.5, tone: 0.6, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0 },
  BassAmbientWash: { mix: 0.36, texture: 0.42, freeze: 0.0, feedback: 0.62, size: 0.56, diffusion: 0.52, modDepth: 0.14, modRate: 0.15, damping: 0.78, tone: 0.40, inputGain: 0.90, outputGain: 0.92, shimmer: 0.0 },
  FrozenOrganPad: { mix: 0.7, texture: 0.85, freeze: 1.0, feedback: 0.65, size: 0.7, diffusion: 0.8, modDepth: 0.4, modRate: 0.05, damping: 0.4, tone: 0.45, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0 },
  GreyholeDelayVerb: { mix: 0.6, texture: 0.55, freeze: 0.0, feedback: 0.76, size: 0.76, diffusion: 0.70, modDepth: 0.4, modRate: 0.25, damping: 0.65, tone: 0.5, inputGain: 1.0, outputGain: 0.90, shimmer: 0.0 },
  DarkLongCloud: { mix: 0.55, texture: 0.75, freeze: 0.0, feedback: 0.76, size: 0.84, diffusion: 0.66, modDepth: 0.3, modRate: 0.1, damping: 0.3, tone: 0.3, inputGain: 0.72, outputGain: 0.72, shimmer: 0.0 },
  GlitchSmear: { mix: 0.5, texture: 0.05, freeze: 0.0, feedback: 0.5, size: 0.25, diffusion: 0.2, modDepth: 0.9, modRate: 0.8, damping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0 },
  AlwaysOnSubtle: { mix: 0.25, texture: 0.2, freeze: 0.0, feedback: 0.3, size: 0.2, diffusion: 0.4, modDepth: 0.1, modRate: 0.1, damping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0 },
  BrightCloud: { mix: 0.5, texture: 0.6, freeze: 0.0, feedback: 0.75, size: 0.6, diffusion: 0.7, modDepth: 0.6, modRate: 0.4, damping: 0.7, tone: 0.8, inputGain: 1.0, outputGain: 1.0, shimmer: 0.0 },
  ShimmerCloud: { mix: 0.55, texture: 0.55, freeze: 0.0, feedback: 0.58, size: 0.62, diffusion: 0.70, modDepth: 0.20, modRate: 0.12, damping: 0.55, tone: 0.62, inputGain: 0.80, outputGain: 0.85, shimmer: 0.20 }
};

const sliders = ['mix', 'texture', 'feedback', 'size', 'diffusion', 'modDepth', 'modRate', 'damping', 'tone', 'shimmer', 'inputGain', 'outputGain'];

const USER_PRESETS_STORAGE_KEY = 'greycloud.userPresets.v1';

// Telemetry Elements
const peakLevel = document.getElementById('peakLevel');
const v_freezestate = document.getElementById('val_freezestate');
const v_loopenergy = document.getElementById('val_loopenergy');
const v_safetygain = document.getElementById('val_safetygain');

function setControlsEnabled(enabled) {
    document.querySelectorAll('[data-dsp-control]').forEach(el => {
        el.disabled = !enabled;
    });
}
    
function setEngineStatus(status) {
    const el = document.getElementById('engineStatus');
    if (el) {
        el.textContent = status;
        if (status === 'Running' || status === 'Ready') el.style.color = '#10B981';
        else if (status === 'Error') el.style.color = '#EF4444';
        else el.style.color = '#9CA3AF';
    }
}

function setStatus(message, level = 'info') {
    const el = document.getElementById('statusLog');
    if (!el) return;
    el.textContent = message;
    el.dataset.level = level;
    if (level === 'error') {
        alert(message); // Keep alert for fatal errors for visibility
    }
}

function setPresetStatus(msg, error=false) {
    const el = document.getElementById('presetStatus');
    if (el) {
        el.textContent = msg;
        el.style.color = error ? '#EF4444' : '#10B981';
    }
}

// Inicialmente desabilitar tudo
setControlsEnabled(false);
setEngineStatus('Engine Off');

async function initAudio() {
    try {
        btnPower.textContent = "Loading...";
        btnPower.disabled = true;
        setEngineStatus('Loading Worklet');

        audioCtx = new (window.AudioContext || window.webkitAudioContext)({
            latencyHint: 'interactive'
        });

        try {
            await audioCtx.audioWorklet.addModule(WORKLET_URL);
        } catch (err) {
            throw new Error(
                `Failed to load AudioWorklet module. Make sure cloud_grey_worklet_processor.js can import cloud_grey_live.js and that build_live.sh was run. Original error: ${err.message}`
            );
        }

        cloudNode = new AudioWorkletNode(audioCtx, 'cloud-grey-worklet-processor', {
            outputChannelCount: [2]
        });

        setEngineStatus('Initializing DSP');

        cloudNode.port.onmessage = (e) => {
            if (e.data.type === 'ready') {
                btnPower.textContent = "AUDIO ACTIVE";
                btnPower.classList.remove('primary');
                btnPower.style.backgroundColor = "#10B981";
                btnPower.style.borderColor = "#059669";
                
                setEngineStatus('Running');
                setStatus('DSP Engine initialized and running.', 'ok');
                setControlsEnabled(true);
                refreshPresetSelect();
                applyPreset(presetSelect.value);
                handleSourceChange();
            } else if (e.data.type === 'meter') {
                updateTelemetry(e.data);
            } else if (e.data.type === 'error') {
                console.error("Worklet Error:", e.data.message);
                setEngineStatus('Error');
                setStatus("Worklet Error: " + e.data.message, 'error');
            }
        };

        cloudNode.connect(audioCtx.destination);

        cloudNode.port.postMessage({
            type: 'init',
            memoryFloats: Math.floor(audioCtx.sampleRate * 3.0)
        });

    } catch (err) {
        console.error(err);
        setEngineStatus('Error');
        setStatus("Failed to init audio: " + err.message, 'error');
        btnPower.textContent = "START AUDIO";
        btnPower.disabled = false;
    }
}

const v_peakdb = document.getElementById('val_peakdb');
function updateTelemetry(data) {
    let db = 20 * Math.log10(data.peak + 1e-6);
    if(v_peakdb) v_peakdb.textContent = db.toFixed(1) + ' dB';
    let pct = Math.max(0, Math.min(100, (db + 60) / 60 * 100));
    peakLevel.style.width = pct + '%';
    peakLevel.style.backgroundColor = db > -0.1 ? '#EF4444' : '#10B981';

    v_freezestate.textContent = data.freezeState.toFixed(2);
    v_freezestate.style.color = data.freezeState > 0.5 ? '#22D3EE' : '#60A5FA';
    
    v_loopenergy.textContent = data.loopEnergy.toFixed(3);
    if (data.loopEnergy < 0.25) v_loopenergy.style.color = '#10B981';
    else if (data.loopEnergy < 0.45) v_loopenergy.style.color = '#FBBF24';
    else v_loopenergy.style.color = '#EF4444';

    v_safetygain.textContent = data.safetyGain.toFixed(2);
    if (data.safetyGain >= 0.98) v_safetygain.style.color = '#10B981';
    else if (data.safetyGain >= 0.90) v_safetygain.style.color = '#FBBF24';
    else v_safetygain.style.color = '#EF4444';
}

function setAudioParamSmooth(name, value, timeConstant = 0.02) {
    if (!cloudNode || !audioCtx) return;
    const param = cloudNode.parameters.get(name);
    if (!param) return;

    param.cancelScheduledValues(audioCtx.currentTime);
    param.setTargetAtTime(value, audioCtx.currentTime, timeConstant);
}

function applyParams(p) {
    if (!cloudNode) return;

    for (const key of sliders) {
        const el = document.getElementById(`p_${key}`);
        const valEl = document.getElementById(`v_${key}`);
        if(el && p[key] !== undefined) {
            el.value = p[key];
            if (valEl) valEl.textContent = p[key].toFixed(2);
            
            // Smooth transition for preset swaps
            setAudioParamSmooth(key, p[key], 0.02);
        }
    }
    
    // freeze
    setAudioParamSmooth('freeze', p.freeze, 0.02);
    chkFreezeLatch.checked = p.freeze > 0.5;

    if (presetAlert) {
        if (p.feedback >= 0.85 || p.size >= 0.85 || p.diffusion >= 0.75) {
            presetAlert.style.display = 'block';
        } else {
            presetAlert.style.display = 'none';
        }
    }
}

function applyPreset(name) {
    const all = getAllPresets();
    const p = all[name];
    if (!p) return;
    const nameInput = document.getElementById('presetNameInput');
    if (nameInput && name.startsWith('user:')) {
        nameInput.value = name.substring(5);
    } else if (nameInput) {
        nameInput.value = '';
    }
    applyParams(p.params || p); // user presets vs factory structure
}

function setParam(name, value, smooth = false) {
    if (!cloudNode) return;
    if (smooth) {
        setAudioParamSmooth(name, value, 0.01);
    } else {
        const param = cloudNode.parameters.get(name);
        if (param) param.value = value;
    }
}

// Sliders binding
for (const key of sliders) {
    const el = document.getElementById(`p_${key}`);
    const valEl = document.getElementById(`v_${key}`);
    if (el) {
        el.addEventListener('input', (e) => {
            const v = parseFloat(e.target.value);
            if (valEl) valEl.textContent = v.toFixed(2);
            setParam(key, v, true);
        });
    }
}

presetSelect.addEventListener('change', (e) => {
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
    presetSelect.innerHTML = '';
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
    if (presetSelect.selectedIndex < 0) presetSelect.selectedIndex = 0;
}

function getCurrentLiveParams() {
    const params = {};
    for (const key of sliders) {
        const el = document.getElementById(`p_${key}`);
        if(el) params[key] = parseFloat(el.value);
    }
    const freezeParam = cloudNode?.parameters?.get('freeze');
    params.freeze = freezeParam ? freezeParam.value : (chkFreezeLatch.checked ? 1.0 : 0.0);
    return params;
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
        params: getCurrentLiveParams()
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
    if (!sel.startsWith('user:')) {
        return setPresetStatus("Cannot overwrite Factory Presets. Save as a new name.", true);
    }
    let name = sel.substring(5);
    let presets = loadUserPresets();
    let idx = presets.findIndex(p => p.name === name);
    if (idx === -1) return setPresetStatus("Preset not found.", true);

    presets[idx].params = getCurrentLiveParams();
    presets[idx].updatedAt = new Date().toISOString();
    saveUserPresets(presets);
    setPresetStatus(`Updated user preset "${name}".`);
});

document.getElementById('btnDeletePreset')?.addEventListener('click', () => {
    let sel = presetSelect.value;
    if (!sel.startsWith('user:')) {
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
                // simple validation
                let safeParams = {};
                for (const key of sliders) {
                    if (typeof p.params[key] === 'number') {
                        let v = p.params[key];
                        // clamp roughly
                        if (['inputGain','outputGain'].includes(key)) v = Math.max(0, Math.min(2, v));
                        else if (key === 'feedback') v = Math.max(0, Math.min(0.94, v));
                        else v = Math.max(0, Math.min(1, v));
                        safeParams[key] = v;
                    }
                }
                safeParams.freeze = typeof p.params.freeze === 'number' ? Math.max(0, Math.min(1, p.params.freeze)) : 0;
                
                let baseName = p.name;
                let finalName = baseName;
                let counter = 1;
                while (existing.some(e => e.name === finalName)) {
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
    e.target.value = ''; // clear
});

// Freeze controls
function activateFreeze(e) {
    if (!cloudNode) return;
    if (e.pointerId !== undefined) {
        try { btnFreezeHold.setPointerCapture(e.pointerId); } catch(ex){}
    }
    btnFreezeHold.classList.add('active');
    setAudioParamSmooth('freeze', 1.0, 0.005);
}

function releaseFreeze(e) {
    if (!cloudNode) return;
    if (e.pointerId !== undefined && typeof btnFreezeHold.hasPointerCapture === 'function' && btnFreezeHold.hasPointerCapture(e.pointerId)) {
        try { btnFreezeHold.releasePointerCapture(e.pointerId); } catch(ex){}
    }
    btnFreezeHold.classList.remove('active');
    setAudioParamSmooth('freeze', chkFreezeLatch.checked ? 1.0 : 0.0, 0.02);
}

btnFreezeHold.addEventListener('pointerdown', activateFreeze);
btnFreezeHold.addEventListener('pointerup', releaseFreeze);
btnFreezeHold.addEventListener('pointercancel', releaseFreeze);

chkFreezeLatch.addEventListener('change', (e) => {
    setAudioParamSmooth('freeze', e.target.checked ? 1.0 : 0.0, 0.02);
});

btnResetTail.addEventListener('click', () => {
    if (cloudNode) cloudNode.port.postMessage({ type: 'reset' });
});


// Source Selection
for (const r of sourceRadios) {
    r.addEventListener('change', handleSourceChange);
}

async function handleSourceChange() {
    if (!audioCtx) return;
    const mode = document.querySelector('input[name="sourceMode"]:checked').value;
    
    if (mode === 'mic') {
        micAlert.style.display = 'block';
        fileControls.style.display = 'none';
        stopFile();
        await ensureMicSource();
    } else {
        micAlert.style.display = 'none';
        fileControls.style.display = 'block';
        if (micStream) {
            micStream.getTracks().forEach(t => t.stop());
            micStream = null;
        }
        if (micSourceNode) {
            micSourceNode.disconnect();
            micSourceNode = null;
        }
    }
}

async function ensureMicSource() {
    try {
        if (!micStream) {
            micStream = await navigator.mediaDevices.getUserMedia({
                audio: {
                    echoCancellation: false,
                    autoGainControl: false,
                    noiseSuppression: false,
                    latency: 0
                }
            });
        }
        if (!micSourceNode && audioCtx) {
            micSourceNode = audioCtx.createMediaStreamSource(micStream);
            micSourceNode.connect(cloudNode);
        }
    } catch (err) {
        console.error(err);
        setStatus("Microphone access denied: " + err.message, 'error');
    }
}

// File Playback
fileInput.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (!audioCtx) {
        setStatus("Start Audio first!", 'warn');
        return;
    }
    
    const arrayBuffer = await file.arrayBuffer();
    fileBuffer = await audioCtx.decodeAudioData(arrayBuffer);
    
    btnPlayFile.disabled = false;
    btnPauseFile.disabled = true;
    btnStopFile.disabled = true;
    stopFile();
});

function playFile() {
    if (!audioCtx || !fileBuffer || !cloudNode) return;
    if (isPlayingFile) return;

    fileSourceNode = audioCtx.createBufferSource();
    fileSourceNode.buffer = fileBuffer;
    fileSourceNode.loop = chkLoop.checked;
    fileSourceNode.connect(cloudNode);

    const safeOffset = fileBuffer.duration > 0
        ? Math.max(0, Math.min(pauseTime, fileBuffer.duration - 0.001))
        : 0;

    fileSourceNode.start(0, safeOffset);
    startTime = audioCtx.currentTime - safeOffset;
    isPlayingFile = true;

    btnPlayFile.disabled = true;
    btnPauseFile.disabled = false;
    btnStopFile.disabled = false;
    
    fileSourceNode.onended = () => {
        if (isPlayingFile && !fileSourceNode.loop) {
            stopFile();
        }
    };
}

function pauseFile() {
    if (!isPlayingFile || !fileSourceNode) return;
    try { fileSourceNode.stop(); } catch (e) {}
    
    let elapsed = audioCtx.currentTime - startTime;
    if (fileBuffer && fileBuffer.duration > 0) {
        elapsed = elapsed % fileBuffer.duration;
    }
    pauseTime = elapsed;
    
    isPlayingFile = false;
    
    btnPlayFile.disabled = false;
    btnPauseFile.disabled = true;
    btnStopFile.disabled = false;
}

function stopFile() {
    if (fileSourceNode) {
        try { fileSourceNode.stop(); } catch (e) {}
        try { fileSourceNode.disconnect(); } catch (e) {}
        fileSourceNode = null;
    }
    isPlayingFile = false;
    pauseTime = 0;
    
    if (fileBuffer) {
        btnPlayFile.disabled = false;
    }
    btnPauseFile.disabled = true;
    btnStopFile.disabled = true;
}

btnPlayFile.addEventListener('click', playFile);
btnPauseFile.addEventListener('click', pauseFile);
btnStopFile.addEventListener('click', stopFile);

btnPower.addEventListener('click', () => {
    if (!audioCtx) {
        initAudio();
    }
});

// MIDI
let midiAccess = null;
let currentMidiInput = null;
let midiLearnActive = false;
let midiMap = {};

const MIDI_MAP_STORAGE_KEY = 'greycloud.midiMap.v1';
const DEFAULT_MIDI_MAP = {
  "cc:1": "mix",
  "cc:2": "texture",
  "cc:3": "feedback",
  "cc:4": "size",
  "cc:5": "tone",
  "cc:64": "freeze"
};

const btnEnableMidi = document.getElementById('btnEnableMidi');
const midiInputSelect = document.getElementById('midiInputSelect');
const btnMidiLearn = document.getElementById('btnMidiLearn');
const midiLearnTarget = document.getElementById('midiLearnTarget');
const btnClearMidiMap = document.getElementById('btnClearMidiMap');
const midiLastMessage = document.getElementById('midiLastMessage');

function setMidiStatus(message, level = 'info') {
    const el = document.getElementById('midiStatus');
    if (!el) return;
    el.textContent = message;
    el.dataset.level = level;
}

function updateLastMidiMessage(command, channel, data1, data2) {
    if (midiLastMessage) {
        midiLastMessage.textContent = `Last MIDI: CMD 0x${command.toString(16).toUpperCase()} | CH ${channel} | Data1: ${data1} | Data2: ${data2}`;
    }
}

function loadMidiMap() {
    try {
        const stored = localStorage.getItem(MIDI_MAP_STORAGE_KEY);
        if (stored) {
            midiMap = JSON.parse(stored);
            setMidiStatus('MIDI Map loaded from Storage', 'info');
            return;
        }
    } catch(e) {
        console.warn("Failed to load MIDI map:", e);
    }
    midiMap = { ...DEFAULT_MIDI_MAP };
    setMidiStatus('Default MIDI Map applied', 'info');
}

function saveMidiMap(map) {
    midiMap = map;
    localStorage.setItem(MIDI_MAP_STORAGE_KEY, JSON.stringify(map));
    setMidiStatus('MIDI Map saved', 'ok');
}

function clearMidiMap() {
    saveMidiMap({});
    setMidiStatus('MIDI Map cleared', 'ok');
}

async function enableMidi() {
    try {
        setMidiStatus('Requesting MIDI access...', 'info');
        midiAccess = await navigator.requestMIDIAccess({ sysex: false });
        midiAccess.onstatechange = refreshMidiInputs;
        
        loadMidiMap();
        refreshMidiInputs();
        
        btnEnableMidi.disabled = true;
        midiInputSelect.disabled = false;
        
        // Let midi learn be available if DSP is running
        if (cloudNode) {
            btnMidiLearn.disabled = false;
            midiLearnTarget.disabled = false;
        }
    } catch (err) {
        setMidiStatus('MIDI Access denied or not supported: ' + err.message, 'error');
    }
}

function refreshMidiInputs() {
    if (!midiAccess) return;
    const inputs = midiAccess.inputs.values();
    const selectedId = midiInputSelect.value;
    
    midiInputSelect.innerHTML = '<option value="">-- Select MIDI Input --</option>';
    let foundCurrent = false;

    for (let input of inputs) {
        const opt = document.createElement('option');
        opt.value = input.id;
        opt.textContent = input.name || `Input ${input.id}`;
        midiInputSelect.appendChild(opt);
        if (input.id === selectedId) foundCurrent = true;
    }
    
    if (!foundCurrent && midiInputSelect.options.length > 1) {
        midiInputSelect.selectedIndex = 1;
    } else if (foundCurrent) {
        midiInputSelect.value = selectedId;
    }
    
    selectMidiInput(midiInputSelect.value);
}

function selectMidiInput(inputId) {
    if (currentMidiInput) {
        currentMidiInput.onmidimessage = null;
    }
    
    if (!inputId) {
        currentMidiInput = null;
        setMidiStatus('No MIDI Input selected', 'warn');
        return;
    }
    
    currentMidiInput = midiAccess.inputs.get(inputId);
    if (currentMidiInput) {
        currentMidiInput.onmidimessage = handleMidiMessage;
        setMidiStatus(`Listening to: ${currentMidiInput.name}`, 'ok');
    }
}

function handleMidiMessage(event) {
    const data = event.data;
    if (data.length < 2) return;
    
    const command = data[0] & 0xf0;
    const channel = (data[0] & 0x0f) + 1;
    const data1 = data[1];
    const data2 = data.length > 2 ? data[2] : 0;
    
    updateLastMidiMessage(command, channel, data1, data2);

    if (command === 0xB0) {
        handleMidiCC(channel, data1, data2);
    } else if (command === 0x90 && data2 > 0) {
        handleMidiNote(channel, data1, data2);
    } else if (command === 0x80 || (command === 0x90 && data2 === 0)) {
        handleMidiNoteOff(channel, data1, data2);
    } else if (command === 0xC0) {
        handleProgramChange(channel, data1);
    }
}

function assignMidiMapping(midiKey, target) {
    midiMap[midiKey] = target;
    saveMidiMap(midiMap);
    setMidiStatus(`Mapped ${midiKey} to ${target}`, 'ok');
}

function handleMidiCC(channel, cc, value) {
    const midiKey = `cc:${cc}`;
    if (midiLearnActive) {
        assignMidiMapping(midiKey, midiLearnTarget.value);
        stopMidiLearn();
        return;
    }
    
    const target = midiMap[midiKey];
    if (!target) return;
    
    const normalized = value / 127.0;
    applyMidiToTarget(target, normalized);
}

function handleMidiNote(channel, note, velocity) {
    const midiKey = `note:${note}`;
    if (midiLearnActive) {
        assignMidiMapping(midiKey, midiLearnTarget.value);
        stopMidiLearn();
        return;
    }
    
    const target = midiMap[midiKey];
    if (target === 'freeze') {
        setAudioParamSmooth('freeze', 1.0, 0.005);
        btnFreezeHold.classList.add('active'); // Visual feedback
    }
}

function handleMidiNoteOff(channel, note, velocity) {
    const midiKey = `note:${note}`;
    const target = midiMap[midiKey];
    if (target === 'freeze') {
        setAudioParamSmooth('freeze', chkFreezeLatch.checked ? 1.0 : 0.0, 0.02);
        btnFreezeHold.classList.remove('active');
    }
}

function handleProgramChange(channel, program) {
    if (midiLearnActive) {
        stopMidiLearn();
    }
    
    const factoryKeys = Object.keys(FACTORY_PRESETS);
    if (factoryKeys.length === 0) return;
    
    const index = program % factoryKeys.length;
    const targetPreset = factoryKeys[index];
    
    presetSelect.value = targetPreset;
    applyPreset(targetPreset);
    setMidiStatus(`Program Change: ${program} -> Loaded ${targetPreset}`, 'info');
}

function applyMidiToTarget(target, normalized) {
    if (target === 'freeze') {
        const value = normalized >= 0.5 ? 1.0 : (chkFreezeLatch.checked ? 1.0 : 0.0);
        setAudioParamSmooth('freeze', value, 0.01);
        if (normalized >= 0.5) btnFreezeHold.classList.add('active');
        else btnFreezeHold.classList.remove('active');
        return;
    }
    
    let scaled = normalized;
    if (target === 'feedback') scaled = normalized * 0.94;
    else if (target === 'inputGain' || target === 'outputGain') scaled = normalized * 2.0;
    
    // Update UI
    const slider = document.getElementById(`p_${target}`);
    if (slider) slider.value = scaled;
    
    const valEl = document.getElementById(`v_${target}`);
    if (valEl) valEl.textContent = scaled.toFixed(2);
    
    setParam(target, scaled, true);
}

function startMidiLearn() {
    midiLearnActive = true;
    btnMidiLearn.textContent = "Learning... (Move Controller)";
    btnMidiLearn.classList.add('warning');
    setMidiStatus(`Move a MIDI CC or press a note to map to: ${midiLearnTarget.value}`, 'warn');
}

function stopMidiLearn() {
    midiLearnActive = false;
    btnMidiLearn.textContent = "MIDI Learn";
    btnMidiLearn.classList.remove('warning');
}

if (btnEnableMidi) btnEnableMidi.addEventListener('click', enableMidi);
if (midiInputSelect) midiInputSelect.addEventListener('change', (e) => selectMidiInput(e.target.value));
if (btnMidiLearn) btnMidiLearn.addEventListener('click', () => {
    if (midiLearnActive) stopMidiLearn();
    else startMidiLearn();
});
if (btnClearMidiMap) btnClearMidiMap.addEventListener('click', clearMidiMap);

