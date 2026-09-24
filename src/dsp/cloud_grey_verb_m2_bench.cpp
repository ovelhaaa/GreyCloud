#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "cloud_grey_verb.hpp"

namespace {
constexpr float kSr = 48000.0f;
constexpr int kFrames = 48000 * 8;
size_t memoryFor() { return 1600000u; }

struct Metrics {
    double firstMs = -1, bands[6] = {}, centroid = 0, peak = 0, rms = 0;
    double corrEarly = 1, corrLate = 1, msEarly = 0, msLate = 0, rt60 = NAN;
    double minSafety = 1;
    double inputPeak = 0, inputRms = 0, outputPeak = 0, outputRms = 0, gainDeltaDb = 0;
    double midRms = 0, sideRms = 0, sideMidRatio = 0, monoFoldRms = 0, monoDeltaDb = 0;
    double spectralCentroidHz = 0, spectralBands[5] = {};
};

// Deterministic offline DFT of the first 1024 stereo samples. Values are RMS
// energy per named band (not audio-thread DSP), and make report comparisons reproducible.
void analyseSpectrum(const std::vector<float>& l, const std::vector<float>& r, Metrics& m) {
    constexpr int n = 1024; constexpr double pi = 3.14159265358979323846;
    const int limits[6] = {20,80,200,1000,4000,16000}; double total=0, weighted=0;
    for (int k=1; k<n/2; ++k) { double re=0, im=0; for(int i=0;i<n;++i) { const double x=i < (int)l.size() ? .5*(l[i]+r[i]) : 0; const double a=2*pi*k*i/n; re+=x*std::cos(a); im-=x*std::sin(a); }
        const double hz=k*kSr/n, energy=(re*re+im*im)/(double(n)*n); total+=energy; weighted+=energy*hz;
        for(int b=0;b<5;++b) if(hz>=limits[b]&&hz<limits[b+1]) { m.spectralBands[b]+=energy; break; }
    }
    m.spectralCentroidHz = total > 0 ? weighted/total : 0;
    for (double& band : m.spectralBands) band=std::sqrt(band);
}

void writeWav(const std::filesystem::path& path, const std::vector<float>& l,
              const std::vector<float>& r) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    const uint32_t dataBytes = static_cast<uint32_t>(l.size() * 2 * sizeof(float));
    const uint32_t riffSize = 36 + dataBytes, fmtSize = 16, byteRate = 8 * static_cast<uint32_t>(kSr);
    const uint16_t format = 3, channels = 2, bits = 32, block = 8;
    f.write("RIFF",4); f.write(reinterpret_cast<const char*>(&riffSize),4); f.write("WAVEfmt ",8);
    f.write(reinterpret_cast<const char*>(&fmtSize),4); f.write(reinterpret_cast<const char*>(&format),2);
    f.write(reinterpret_cast<const char*>(&channels),2); f.write(reinterpret_cast<const char*>(&kSr),4);
    f.write(reinterpret_cast<const char*>(&byteRate),4); f.write(reinterpret_cast<const char*>(&block),2);
    f.write(reinterpret_cast<const char*>(&bits),2); f.write("data",4); f.write(reinterpret_cast<const char*>(&dataBytes),4);
    for (size_t i=0;i<l.size();++i) { f.write(reinterpret_cast<const char*>(&l[i]),4); f.write(reinterpret_cast<const char*>(&r[i]),4); }
}

// The bench takes the full canonical factory state, never an enum followed by
// an incomplete local preset lookup. wetOnly is used solely for IR analysis;
// musical renders retain the exact user-facing mix/gain state.
Metrics render(const CloudGreyVerb::FactoryPreset& preset, const std::vector<float>& input,
               std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr, bool wetOnly = false) {
    // Mirrors the plugin's 2x HQ topology. The offline interpolator is a
    // deterministic linear reference (JUCE uses a higher-quality filter), so
    // it deliberately selects HQ timing/rate rather than treating hqMode as metadata.
    const bool hq = preset.hqMode;
    std::vector<float> memory(memoryFor()); CloudGreyVerb fx; fx.init(hq ? kSr * 2.0f : kSr, memory.data(), memory.size());
    auto p = preset.dsp; if (wetOnly) p.mix=1; p.clipOutput=false; fx.setParams(p); fx.reset();
    std::vector<float> l(input.size()), r(input.size()); std::vector<double> e(input.size()); Metrics m;
    double sum=0, weighted=0, eeL=0,eeR=0,eeLR=0, leL=0,leR=0,leLR=0, em=0,es=0,lm=0,ls=0;
    const int edges[7]={0,20,50,80,200,1000,8000};
    float previousInput = 0;
    for(size_t i=0;i<input.size();++i) {
        if (hq) { float discardL, discardR; const float halfway=.5f*(previousInput+input[i]); fx.processSample(halfway,halfway,discardL,discardR); fx.processSample(input[i],input[i],l[i],r[i]); }
        else fx.processSample(input[i],input[i],l[i],r[i]);
        previousInput=input[i];
        const double x=l[i], y=r[i], en=x*x+y*y; e[i]=en; sum+=en; weighted+=en*i/kSr;
        m.peak=std::max(m.peak,std::max(std::abs(x),std::abs(y))); m.minSafety=std::min(m.minSafety,(double)fx.getSafetyGain());
        if (m.firstMs < 0 && en > 1e-12) m.firstMs=i*1000.0/kSr;
        const double ms=i*1000.0/kSr; for(int b=0;b<6;++b) if(ms>=edges[b]&&ms<edges[b+1]) {m.bands[b]+=en;break;}
        const double mid=(x+y)*.5, side=(x-y)*.5;
        if(ms < 80) {eeL+=x*x;eeR+=y*y;eeLR+=x*y;em+=mid*mid;es+=side*side;}
        else {leL+=x*x;leR+=y*y;leLR+=x*y;lm+=mid*mid;ls+=side*side;}
    }
    m.rms=std::sqrt(sum/(2*input.size())); m.centroid=sum?weighted/sum:0;
    double inputSum=0, midSum=0, sideSum=0, monoSum=0;
    for (size_t i=0;i<input.size();++i) { inputSum+=input[i]*input[i]; const double mid=.5*(l[i]+r[i]), side=.5*(l[i]-r[i]); midSum+=mid*mid; sideSum+=side*side; monoSum+=mid*mid; }
    m.inputPeak=0; for(float x:input)m.inputPeak=std::max(m.inputPeak,std::abs((double)x));
    m.inputRms=std::sqrt(inputSum/input.size()); m.outputPeak=m.peak; m.outputRms=m.rms;
    m.gainDeltaDb=20*std::log10(std::max(1e-30,m.outputRms)/std::max(1e-30,m.inputRms));
    m.midRms=std::sqrt(midSum/input.size()); m.sideRms=std::sqrt(sideSum/input.size());
    m.sideMidRatio=m.sideRms/std::max(1e-30,m.midRms); m.monoFoldRms=std::sqrt(monoSum/input.size());
    m.monoDeltaDb=20*std::log10(std::max(1e-30,m.monoFoldRms)/std::max(1e-30,m.outputRms)); analyseSpectrum(l,r,m);
    m.corrEarly=eeLR/std::sqrt(std::max(1e-30,eeL*eeR)); m.corrLate=leLR/std::sqrt(std::max(1e-30,leL*leR));
    m.msEarly=es/std::max(1e-30,em); m.msLate=ls/std::max(1e-30,lm);
    std::vector<double> decay(e.size()); double tail=0; for(size_t i=e.size();i--;) {tail+=e[i];decay[i]=tail;}
    double sx=0,sy=0,sxx=0,sxy=0; int n=0; for(size_t i=0;i<e.size();++i) { double db=10*std::log10(std::max(1e-30,decay[i]/tail)); if(db<=-5&&db>=-25){double t=i/kSr;sx+=t;sy+=db;sxx+=t*t;sxy+=t*db;++n;} }
    const double d=n*sxx-sx*sx; if(n>100&&std::abs(d)>1e-12){double slope=(n*sxy-sx*sy)/d;if(slope<0)m.rt60=-60/slope;}
    if(outL)*outL=std::move(l); if(outR)*outR=std::move(r); return m;
}
void print(const char* name,const Metrics&m){
    const double early=m.bands[0]+m.bands[1]+m.bands[2], late=m.bands[3]+m.bands[4]+m.bands[5];
    std::cout<<name<<",input_peak="<<m.inputPeak<<",input_rms="<<m.inputRms<<",output_peak="<<m.outputPeak<<",output_rms="<<m.outputRms<<",gain_delta_db="<<m.gainDeltaDb<<",mid_rms="<<m.midRms<<",side_rms="<<m.sideRms<<",side_mid_ratio="<<m.sideMidRatio<<",mono_fold_rms="<<m.monoFoldRms<<",mono_delta_db="<<m.monoDeltaDb<<",spectral_centroid_hz="<<m.spectralCentroidHz<<",band_20_80="<<m.spectralBands[0]<<",band_80_200="<<m.spectralBands[1]<<",band_200_1000="<<m.spectralBands[2]<<",band_1000_4000="<<m.spectralBands[3]<<",band_4000_16000="<<m.spectralBands[4]<<",min_safety_gain="<<m.minSafety<<",RT60_s="<<m.rt60<<'\n';
}
void writeMetricRow(std::ofstream& csv, const CloudGreyVerb::FactoryPreset& p, const std::string& source, const Metrics& m) {
    csv << p.name << ',' << source << ',' << (p.hqMode ? 1 : 0) << ',' << p.dsp.preDelay * 200.0f << ',' << p.dsp.stereoWidth << ','
        << m.inputPeak << ',' << m.inputRms << ',' << m.outputPeak << ',' << m.outputRms << ',' << m.gainDeltaDb << ','
        << m.midRms << ',' << m.sideRms << ',' << m.sideMidRatio << ',' << m.monoFoldRms << ',' << m.monoDeltaDb << ','
        << m.spectralCentroidHz << ',' << m.spectralBands[0] << ',' << m.spectralBands[1] << ',' << m.spectralBands[2] << ','
        << m.spectralBands[3] << ',' << m.spectralBands[4] << ',' << m.minSafety << '\n';
}
}
int main(int argc,char**argv){
    const std::filesystem::path root=argc>1?argv[1]:"m3_analysis"; std::filesystem::create_directories(root/"metrics");
    std::ofstream musicalCsv(root/"metrics"/"musical_metrics.csv");
    musicalCsv << "preset,source,hq,preDelay_ms,width,input_peak,input_rms,output_peak,output_rms,gain_delta_db,mid_rms,side_rms,side_mid_ratio,mono_fold_rms,mono_delta_db,spectral_centroid_hz,band_20_80,band_80_200,band_200_1000,band_1000_4000,band_4000_16000,min_safety_gain\n";
    const bool renderAudio = argc < 3 || std::string(argv[2]) != "--metrics";
    std::vector<float> ir(kFrames);ir[0]=.70710678f;
    for(size_t i=0;i<CloudGreyVerb::factoryPresetCount();++i){const auto& preset=CloudGreyVerb::getFactoryPreset(i);std::vector<float>l,r;auto m=render(preset,ir,&l,&r,true);print(preset.name,m);writeMetricRow(musicalCsv,preset,"impulse",m);writeWav(root/"metrics"/(std::string(preset.name)+"_ir.wav"),l,r);if(!std::isfinite(m.peak)||m.peak>8||m.minSafety<0.34)return 1;}
    if (renderAudio) { const std::string signalNames[]={"01_dry_vocal","02_piano_chord","03_piano_staccato","04_acoustic_guitar","05_electric_guitar_clean","06_snare","07_synth_pluck","08_synth_pad","09_bass_notes","10_full_mix_excerpt"};
    for(int s=0;s<10;++s) { std::vector<float> in(kFrames); uint32_t noise=0x9e3779b9u; for(int i=0;i<kFrames;++i){ const float t=i/kSr; const float chord=.12f*(std::sin(2*3.14159265f*220*t)+.65f*std::sin(2*3.14159265f*277.18f*t)+.42f*std::sin(2*3.14159265f*329.63f*t)); const float pluck=.55f*(std::sin(2*3.14159265f*220*t)+.35f*std::sin(2*3.14159265f*440*t)+.12f*std::sin(2*3.14159265f*660*t))*std::exp(-18.0f*t); noise=noise*1664525u+1013904223u; const float n=(static_cast<float>((noise>>8)&0xffff)/32767.5f-1.f); if(s==0) in[i]=t<1.4f?.16f*(std::sin(2*3.14159265f*(196.f+.8f*std::sin(2*3.14159265f*2*t))*t)+.28f*std::sin(2*3.14159265f*392*t))*std::exp(-1.5f*t):0; else if(s==1) in[i]=t<1.8f?chord*std::exp(-1.25f*t):0; else if(s==2) in[i]=(t<.16f?chord*std::exp(-9*t):0)+(t>.42f&&t<.58f?chord*std::exp(-9*(t-.42f)):0); else if(s==3) in[i]=t<.7f?pluck:0; else if(s==4) in[i]=t<.55f?.38f*(std::sin(2*3.14159265f*164.81f*t)+.18f*std::sin(2*3.14159265f*329.63f*t))*std::exp(-7*t):0; else if(s==5) in[i]=t<.12f?(.22f*n+.16f*std::sin(2*3.14159265f*190*t))*std::exp(-32*t):0; else if(s==6) in[i]=t<.38f?.52f*std::sin(2*3.14159265f*293.66f*t)*std::exp(-14*t):0; else if(s==7) in[i]=t<2.8f?chord*(.75f+.25f*std::sin(2*3.14159265f*.17f*t)):0; else if(s==8) in[i]=t<1.4f?.26f*std::sin(2*3.14159265f*(t<.7f?55.f:82.41f)*t):0; else in[i]=(t<1.6f?(.42f*chord+.35f*pluck+.16f*std::sin(2*3.14159265f*82.41f*t)+.04f*n):0); } std::vector<float> dryR=in; writeWav(root/"dry"/(signalNames[s]+".wav"),in,dryR); for(size_t p=0;p<CloudGreyVerb::factoryPresetCount();++p){const auto& preset=CloudGreyVerb::getFactoryPreset(p);std::vector<float>l,r;auto m=render(preset,in,&l,&r);print((std::string(preset.name)+"/"+signalNames[s]).c_str(),m);writeMetricRow(musicalCsv,preset,signalNames[s],m);writeWav(root/"after"/preset.name/(signalNames[s]+".wav"),l,r);} } }
}
