/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import { useState } from 'react';
import { Download, Code2, CircuitBoard, Cpu, CheckCircle, FileText, FlaskConical } from 'lucide-react';
import ReactMarkdown from 'react-markdown';

import dspUtilsRaw from './dsp/dsp_utils.hpp?raw';
import cloudGreyHppRaw from './dsp/cloud_grey_verb.hpp?raw';
import cloudGreyCppRaw from './dsp/cloud_grey_verb.cpp?raw';
import migrationDocsRaw from './docs/STM32H5_MIGRATION.md?raw';
import smokeTestRaw from './dsp/cloud_grey_verb_smoke_test.cpp?raw';
import coreDocsRaw from './docs/CLOUD_GREY_VERB_CORE.md?raw';

const arquivos = [
  { name: 'cloud_grey_verb.hpp', content: cloudGreyHppRaw, lang: 'C++' },
  { name: 'cloud_grey_verb.cpp', content: cloudGreyCppRaw, lang: 'C++' },
  { name: 'dsp_utils.hpp', content: dspUtilsRaw, lang: 'C++' },
  { name: 'cloud_grey_verb_smoke_test.cpp', content: smokeTestRaw, lang: 'C++', icon: FlaskConical },
  { name: 'STM32H5_MIGRATION.md', content: migrationDocsRaw, lang: 'Markdown', icon: FileText },
  { name: 'CLOUD_GREY_VERB_CORE.md', content: coreDocsRaw, lang: 'Markdown', icon: FileText }
];

export default function App() {
  const [activeFile, setActiveFile] = useState(arquivos[0]);
  const [copied, setCopied] = useState(false);

  const handleCopy = async () => {
    await navigator.clipboard.writeText(activeFile.content);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const handleDownload = () => {
    const blob = new Blob([activeFile.content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = activeFile.name;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  return (
    <div className="min-h-screen bg-[#0E1117] text-gray-200 font-sans flex flex-col md:flex-row">
      
      {/* Sidebar Navegação */}
      <aside className="w-full md:w-64 bg-[#161B22] border-r border-[#30363D] flex flex-col shrink-0 overflow-y-auto">
        <div className="p-6">
          <div className="flex items-center gap-3 mb-2">
            <CircuitBoard className="text-emerald-400" size={28} />
            <h1 className="text-xl font-semibold tracking-tight text-white">Nimbus Reverb</h1>
          </div>
          <p className="text-xs text-gray-400 font-medium mb-6 uppercase tracking-widest">
            Embedded DSP Core
          </p>

          <div className="space-y-4">
            <div>
              <h2 className="text-xs font-semibold text-gray-500 uppercase tracking-wider mb-2 px-2">C++ Source Files</h2>
              <ul className="space-y-1">
                {arquivos.map(file => (
                  <li key={file.name}>
                    <button
                      onClick={() => setActiveFile(file)}
                      className={`w-full flex items-center gap-3 px-3 py-2 rounded-md transition-colors text-sm font-medium ${
                        activeFile.name === file.name 
                        ? 'bg-[#21262D] text-white shadow-sm' 
                        : 'text-gray-400 hover:text-gray-200 hover:bg-[#21262D]/50'
                      }`}
                    >
                      {file.icon 
                          ? <file.icon size={16} className={activeFile.name === file.name ? 'text-emerald-400' : ''} /> 
                          : <Code2 size={16} className={activeFile.name === file.name ? 'text-emerald-400' : ''} />}
                      {file.name}
                    </button>
                  </li>
                ))}
              </ul>
            </div>
            
            <div className="px-2 pt-4 border-t border-[#30363D]">
               <h2 className="text-xs font-semibold text-gray-500 uppercase tracking-wider mb-3">Specs</h2>
               <div className="space-y-2 text-xs text-gray-400">
                  <div className="flex items-center gap-2"><Cpu size={14}/> No Din alloc / RTOS Safe</div>
                  <div className="flex items-center gap-2"><Cpu size={14}/> 32-bit Float Processing</div>
                  <div className="flex items-center gap-2"><Cpu size={14}/> Cortex-M Target (H7/ESP32)</div>
               </div>
            </div>
          </div>
        </div>
      </aside>

      {/* Main Content Area */}
      <main className="flex-1 flex flex-col min-w-0 max-h-screen">
        
        {/* Editor Toolbar */}
        <header className="h-14 border-b border-[#30363D] bg-[#0E1117] flex items-center justify-between px-6 shrink-0 shadow-sm z-10">
          <div className="flex items-center gap-2">
            <span className="text-sm font-mono text-emerald-400">{activeFile.name}</span>
          </div>
          <div className="flex items-center gap-3">
            <button
              onClick={handleCopy}
              className="flex items-center gap-2 px-3 py-1.5 rounded bg-transparent hover:bg-[#21262D] text-sm text-gray-300 transition-colors"
            >
              {copied ? <CheckCircle size={16} className="text-emerald-400"/> : <Code2 size={16} />}
              {copied ? 'Copied' : 'Copy'}
            </button>
            <button
              onClick={handleDownload}
              className="flex items-center gap-2 px-3 py-1.5 rounded bg-emerald-600 hover:bg-emerald-500 text-sm text-white font-medium transition-colors cursor-pointer"
            >
              <Download size={16} />
              Download
            </button>
          </div>
        </header>

        {/* Code Content */}
        <div className="flex-1 overflow-auto bg-[#0d1117] p-6">
          <div className="max-w-4xl mx-auto rounded-lg border border-[#30363D] bg-[#161B22] shadow-xl overflow-hidden">
             {/* MacOS style window dots - purely aesthetic */}
            <div className="h-8 border-b border-[#30363D] bg-[#0d1117] flex items-center px-4 gap-2">
               <div className="w-3 h-3 rounded-full bg-red-500/20"></div>
               <div className="w-3 h-3 rounded-full bg-amber-500/20"></div>
               <div className="w-3 h-3 rounded-full bg-green-500/20"></div>
            </div>
            
            {activeFile.lang === 'Markdown' ? (
              <div className="p-8 text-gray-300 prose prose-invert prose-emerald max-w-none">
                 <ReactMarkdown>{activeFile.content}</ReactMarkdown>
              </div>
            ) : (
                <pre className="p-6 text-sm font-mono text-gray-300 overflow-x-auto whitespace-pre leading-relaxed">
                  <code>{activeFile.content}</code>
                </pre>
            )}
          </div>
        </div>

      </main>
    </div>
  );
}
