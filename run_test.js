import { readFileSync } from 'fs';
import { resolve } from 'path';

// Emulate the wasm loading
const wasmBuffer = readFileSync(resolve(__dirname, 'web_wasm/cloud_grey.wasm'));
// Actually, without Emscripten, we don't have cloud_grey.wasm!
