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
};

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
    std::vector<float> memory(memoryFor()); CloudGreyVerb fx; fx.init(kSr, memory.data(), memory.size());
    auto p = preset.dsp; if (wetOnly) p.mix=1; p.clipOutput=false; fx.setParams(p); fx.reset();
    std::vector<float> l(input.size()), r(input.size()); std::vector<double> e(input.size()); Metrics m;
    double sum=0, weighted=0, eeL=0,eeR=0,eeLR=0, leL=0,leR=0,leLR=0, em=0,es=0,lm=0,ls=0;
    const int edges[7]={0,20,50,80,200,1000,8000};
    for(size_t i=0;i<input.size();++i) {
        fx.processSample(input[i],input[i],l[i],r[i]);
        const double x=l[i], y=r[i], en=x*x+y*y; e[i]=en; sum+=en; weighted+=en*i/kSr;
        m.peak=std::max(m.peak,std::max(std::abs(x),std::abs(y))); m.minSafety=std::min(m.minSafety,(double)fx.getSafetyGain());
        if (m.firstMs < 0 && en > 1e-12) m.firstMs=i*1000.0/kSr;
        const double ms=i*1000.0/kSr; for(int b=0;b<6;++b) if(ms>=edges[b]&&ms<edges[b+1]) {m.bands[b]+=en;break;}
        const double mid=(x+y)*.5, side=(x-y)*.5;
        if(ms < 80) {eeL+=x*x;eeR+=y*y;eeLR+=x*y;em+=mid*mid;es+=side*side;}
        else {leL+=x*x;leR+=y*y;leLR+=x*y;lm+=mid*mid;ls+=side*side;}
    }
    m.rms=std::sqrt(sum/(2*input.size())); m.centroid=sum?weighted/sum:0;
    m.corrEarly=eeLR/std::sqrt(std::max(1e-30,eeL*eeR)); m.corrLate=leLR/std::sqrt(std::max(1e-30,leL*leR));
    m.msEarly=es/std::max(1e-30,em); m.msLate=ls/std::max(1e-30,lm);
    std::vector<double> decay(e.size()); double tail=0; for(size_t i=e.size();i--;) {tail+=e[i];decay[i]=tail;}
    double sx=0,sy=0,sxx=0,sxy=0; int n=0; for(size_t i=0;i<e.size();++i) { double db=10*std::log10(std::max(1e-30,decay[i]/tail)); if(db<=-5&&db>=-25){double t=i/kSr;sx+=t;sy+=db;sxx+=t*t;sxy+=t*db;++n;} }
    const double d=n*sxx-sx*sx; if(n>100&&std::abs(d)>1e-12){double slope=(n*sxy-sx*sy)/d;if(slope<0)m.rt60=-60/slope;}
    if(outL)*outL=std::move(l); if(outR)*outR=std::move(r); return m;
}
void print(const char* name,const Metrics&m){
    const double early=m.bands[0]+m.bands[1]+m.bands[2], late=m.bands[3]+m.bands[4]+m.bands[5];
    std::cout<<name<<",first_arrival_ms="<<m.firstMs<<",energy_0_20ms="<<m.bands[0]<<",energy_20_50ms="<<m.bands[1]<<",energy_50_80ms="<<m.bands[2]<<",energy_80_200ms="<<m.bands[3]<<",energy_200_1000ms="<<m.bands[4]<<",energy_after_1s="<<m.bands[5]<<",early_late_ratio_db="<<10*std::log10((early+1e-30)/(late+1e-30))<<",energy_centroid_s="<<m.centroid<<",stereo_correlation_early="<<m.corrEarly<<",stereo_correlation_late="<<m.corrLate<<",mid_side_ratio_early="<<m.msEarly<<",mid_side_ratio_late="<<m.msLate<<",peak="<<m.peak<<",RMS="<<m.rms<<",RT60_s="<<m.rt60<<",min_safety_gain="<<m.minSafety<<'\n';
}
}
int main(int argc,char**argv){
    const std::filesystem::path root=argc>1?argv[1]:"m2_audio"; std::filesystem::create_directories(root);
    const bool renderAudio = argc < 3 || std::string(argv[2]) != "--metrics";
    std::vector<float> ir(kFrames);ir[0]=.70710678f;
    for(size_t i=0;i<CloudGreyVerb::factoryPresetCount();++i){const auto& preset=CloudGreyVerb::getFactoryPreset(i);std::vector<float>l,r;auto m=render(preset,ir,&l,&r,true);print(preset.name,m);writeWav(root/"metrics"/(std::string(preset.name)+"_ir.wav"),l,r);if(!std::isfinite(m.peak)||m.peak>8||m.minSafety<0.34)return 1;}
    if (renderAudio) { const std::string signalNames[]={"01_dry_vocal","02_piano_chord","03_piano_staccato","04_acoustic_guitar","05_electric_guitar_clean","06_snare","07_synth_pluck","08_synth_pad","09_bass_notes","10_full_mix_excerpt"};
    for(int s=0;s<10;++s) { std::vector<float> in(kFrames); uint32_t noise=0x9e3779b9u; for(int i=0;i<kFrames;++i){ const float t=i/kSr; const float chord=.12f*(std::sin(2*3.14159265f*220*t)+.65f*std::sin(2*3.14159265f*277.18f*t)+.42f*std::sin(2*3.14159265f*329.63f*t)); const float pluck=.55f*(std::sin(2*3.14159265f*220*t)+.35f*std::sin(2*3.14159265f*440*t)+.12f*std::sin(2*3.14159265f*660*t))*std::exp(-18.0f*t); noise=noise*1664525u+1013904223u; const float n=(static_cast<float>((noise>>8)&0xffff)/32767.5f-1.f); if(s==0) in[i]=t<1.4f?.16f*(std::sin(2*3.14159265f*(196.f+.8f*std::sin(2*3.14159265f*2*t))*t)+.28f*std::sin(2*3.14159265f*392*t))*std::exp(-1.5f*t):0; else if(s==1) in[i]=t<1.8f?chord*std::exp(-1.25f*t):0; else if(s==2) in[i]=(t<.16f?chord*std::exp(-9*t):0)+(t>.42f&&t<.58f?chord*std::exp(-9*(t-.42f)):0); else if(s==3) in[i]=t<.7f?pluck:0; else if(s==4) in[i]=t<.55f?.38f*(std::sin(2*3.14159265f*164.81f*t)+.18f*std::sin(2*3.14159265f*329.63f*t))*std::exp(-7*t):0; else if(s==5) in[i]=t<.12f?(.22f*n+.16f*std::sin(2*3.14159265f*190*t))*std::exp(-32*t):0; else if(s==6) in[i]=t<.38f?.52f*std::sin(2*3.14159265f*293.66f*t)*std::exp(-14*t):0; else if(s==7) in[i]=t<2.8f?chord*(.75f+.25f*std::sin(2*3.14159265f*.17f*t)):0; else if(s==8) in[i]=t<1.4f?.26f*std::sin(2*3.14159265f*(t<.7f?55.f:82.41f)*t):0; else in[i]=(t<1.6f?(.42f*chord+.35f*pluck+.16f*std::sin(2*3.14159265f*82.41f*t)+.04f*n):0); } std::vector<float> dryR=in; writeWav(root/"dry"/(signalNames[s]+".wav"),in,dryR); for(size_t p=0;p<CloudGreyVerb::factoryPresetCount();++p){const auto& preset=CloudGreyVerb::getFactoryPreset(p);std::vector<float>l,r;auto m=render(preset,in,&l,&r);print((std::string(preset.name)+"/"+signalNames[s]).c_str(),m);writeWav(root/"after"/preset.name/(signalNames[s]+".wav"),l,r);} } }
}
