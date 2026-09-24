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

Metrics render(CloudGreyVerb::Preset preset, const std::vector<float>& input,
               std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr) {
    std::vector<float> memory(memoryFor()); CloudGreyVerb fx; fx.init(kSr, memory.data(), memory.size());
    auto p = CloudGreyVerb::getPreset(preset); p.mix=1; p.clipOutput=false; fx.setParams(p); fx.reset();
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
    const CloudGreyVerb::Preset ps[]={CloudGreyVerb::Preset::SmallCloudRoom,CloudGreyVerb::Preset::AlwaysOnSubtle,CloudGreyVerb::Preset::BassAmbientWash,CloudGreyVerb::Preset::BrightCloud,CloudGreyVerb::Preset::GreyholeDelayVerb,CloudGreyVerb::Preset::DarkLongCloud};
    const char* names[]={"SmallCloudRoom","AlwaysOnSubtle","BassAmbientWash","BrightCloud","GreyholeDelayVerb","DarkLongCloud"};
    std::vector<float> ir(kFrames);ir[0]=.70710678f;
    for(int i=0;i<6;++i){std::vector<float>l,r;auto m=render(ps[i],ir,&l,&r);print(names[i],m);writeWav(root/(std::string(names[i])+"_ir.wav"),l,r);if(!std::isfinite(m.peak)||m.peak>8||m.minSafety<0.34)return 1;}
    for(const auto& kind: {std::string("transient"),std::string("pluck"),std::string("chord")}) { std::vector<float> in(kFrames); for(int i=0;i<kFrames;++i){float t=i/kSr;if(kind=="transient")in[i]=(i%2400==0?.8f:0); else if(kind=="pluck")in[i]=t<.12f?.6f*std::sin(2*3.14159265f*220*t)*std::exp(-18*t):0; else in[i]=t<1.0f?.22f*(std::sin(2*3.14159265f*220*t)+std::sin(2*3.14159265f*277.18f*t)+std::sin(2*3.14159265f*329.63f*t)):0; } std::vector<float>l,r;render(CloudGreyVerb::Preset::SmallCloudRoom,in,&l,&r);writeWav(root/(kind+".wav"),l,r); }
}
