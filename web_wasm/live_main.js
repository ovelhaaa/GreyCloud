// GreyCloud - Live AudioWorklet Main

const WORKLET_URL = './cloud_grey_worklet_processor.js';
// Module import will happen inside the worklet, but we also fetch it to pass the bytes
const WASM_URL = './cloud_grey_live.wasm';

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

const PRESETS = {
  SmallCloudRoom: { mix: 0.4, texture: 0.3, freeze: 0.0, feedback: 0.5, size: 0.35, diffusion: 0.6, modDepth: 0.2, modRate: 0.15, damping: 0.5, tone: 0.6, inputGain: 1.0, outputGain: 1.0 },
  BassAmbientWash: { mix: 0.35, texture: 0.4, freeze: 0.0, feedback: 0.70, size: 0.6, diffusion: 0.5, modDepth: 0.15, modRate: 0.15, damping: 0.8, tone: 0.4, inputGain: 1.0, outputGain: 1.0 },
  FrozenOrganPad: { mix: 0.7, texture: 0.85, freeze: 1.0, feedback: 0.65, size: 0.7, diffusion: 0.8, modDepth: 0.4, modRate: 0.05, damping: 0.4, tone: 0.45, inputGain: 1.0, outputGain: 1.0 },
  GreyholeDelayVerb: { mix: 0.6, texture: 0.55, freeze: 0.0, feedback: 0.85, size: 0.8, diffusion: 0.75, modDepth: 0.4, modRate: 0.25, damping: 0.65, tone: 0.5, inputGain: 1.0, outputGain: 1.0 },
  DarkLongCloud: { mix: 0.55, texture: 0.75, freeze: 0.0, feedback: 0.88, size: 0.9, diffusion: 0.70, modDepth: 0.3, modRate: 0.1, damping: 0.3, tone: 0.3, inputGain: 0.80, outputGain: 0.75 },
  GlitchSmear: { mix: 0.5, texture: 0.05, freeze: 0.0, feedback: 0.5, size: 0.25, diffusion: 0.2, modDepth: 0.9, modRate: 0.8, damping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0 },
  AlwaysOnSubtle: { mix: 0.25, texture: 0.2, freeze: 0.0, feedback: 0.3, size: 0.2, diffusion: 0.4, modDepth: 0.1, modRate: 0.1, damping: 0.5, tone: 0.5, inputGain: 1.0, outputGain: 1.0 },
  BrightCloud: { mix: 0.5, texture: 0.6, freeze: 0.0, feedback: 0.75, size: 0.6, diffusion: 0.7, modDepth: 0.6, modRate: 0.4, damping: 0.7, tone: 0.8, inputGain: 1.0, outputGain: 1.0 }
};

const sliders = ['mix', 'texture', 'feedback', 'size', 'diffusion', 'modDepth', 'modRate', 'damping', 'tone', 'inputGain', 'outputGain'];

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

        await audioCtx.audioWorklet.addModule(WORKLET_URL);

        setEngineStatus('Fetching WASM');
        const response = await fetch(WASM_URL);
        if (!response.ok) throw new Error("Failed to load WASM binary");
        const wasmBytes = await response.arrayBuffer();

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
                setControlsEnabled(true);
                applyPreset(presetSelect.value);
                handleSourceChange();
            } else if (e.data.type === 'meter') {
                updateTelemetry(e.data);
            } else if (e.data.type === 'error') {
                console.error("Worklet Error:", e.data.message);
                setEngineStatus('Error');
                alert("Worklet Error: " + e.data.message);
            }
        };

        cloudNode.connect(audioCtx.destination);

        cloudNode.port.postMessage({
            type: 'init',
            wasmBytes,
            memoryFloats: Math.floor(audioCtx.sampleRate * 3.0)
        });

    } catch (err) {
        console.error(err);
        setEngineStatus('Error');
        alert("Failed to init audio: " + err.message);
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

function applyPreset(name) {
    if (!cloudNode) return;
    const p = PRESETS[name];
    if (!p) return;

    for (const key of sliders) {
        const el = document.getElementById(`p_${key}`);
        const valEl = document.getElementById(`v_${key}`);
        if(el && p[key] !== undefined) {
            el.value = p[key];
            if (valEl) valEl.textContent = p[key].toFixed(2);
            
            const param = cloudNode.parameters.get(key);
            if (param) param.value = p[key];
        }
    }
    
    // freeze
    const fParam = cloudNode.parameters.get('freeze');
    if (fParam) fParam.value = p.freeze;
    chkFreezeLatch.checked = p.freeze > 0.5;
}

function setParam(name, value) {
    if (!cloudNode) return;
    const param = cloudNode.parameters.get(name);
    if (param) param.value = value;
}

// Sliders binding
for (const key of sliders) {
    const el = document.getElementById(`p_${key}`);
    const valEl = document.getElementById(`v_${key}`);
    if (el) {
        el.addEventListener('input', (e) => {
            const v = parseFloat(e.target.value);
            if (valEl) valEl.textContent = v.toFixed(2);
            setParam(key, v);
        });
    }
}

presetSelect.addEventListener('change', (e) => {
    applyPreset(e.target.value);
});

// Freeze controls
function activateFreeze(e) {
    if (e.pointerId) btnFreezeHold.setPointerCapture(e.pointerId);
    btnFreezeHold.style.backgroundColor = '#60A5FA';
    setParam('freeze', 1.0);
}

function releaseFreeze(e) {
    if (e.pointerId) {
        try { btnFreezeHold.releasePointerCapture(e.pointerId); } catch(ex){}
    }
    btnFreezeHold.style.backgroundColor = '';
    setParam('freeze', chkFreezeLatch.checked ? 1.0 : 0.0);
}

btnFreezeHold.addEventListener('pointerdown', activateFreeze);
btnFreezeHold.addEventListener('pointerup', releaseFreeze);
btnFreezeHold.addEventListener('pointercancel', releaseFreeze);
btnFreezeHold.addEventListener('lostpointercapture', releaseFreeze);

chkFreezeLatch.addEventListener('change', (e) => {
    setParam('freeze', e.target.checked ? 1.0 : 0.0);
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
        alert("Failed to access microphone: " + err.message);
    }
}

// File Playback
fileInput.addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    if (!audioCtx) {
        alert("Start Audio first!");
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
