#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>
#include "cloud_grey_verb.hpp"
#include "offline_metrics.hpp"

namespace {
constexpr float kSr = 48000.0f;
constexpr int kFrames = 48000 * 8;
constexpr double kPi = 3.14159265358979323846;
size_t memoryFor() { return 1600000u; }
const char* renderMode(const CloudGreyVerb::FactoryPreset& p) { return p.hqMode ? "hq_approx" : "normal"; }

struct Metrics {
    double firstMs=-1, timeBands[6]{}, energyCentroid=0, peak=0, rms=0, rt60=NAN, minSafety=1;
    double corrEarly=1,corrLate=1,msEarly=0,msLate=0;
    double inputPeak=0,inputRms=0,gainDeltaDb=0;
    OfflineMetrics::Stereo stereo;
    OfflineMetrics::Spectrum full,active,tail,dryFull,dryActive;
    OfflineMetrics::Regions regions;
};

void writeWav(const std::filesystem::path& path,const std::vector<float>& l,const std::vector<float>& r){
    std::filesystem::create_directories(path.parent_path()); std::ofstream f(path,std::ios::binary);
    const uint32_t dataBytes=static_cast<uint32_t>(l.size()*8),riffSize=36+dataBytes,fmtSize=16,byteRate=8*static_cast<uint32_t>(kSr);
    const uint16_t format=3,channels=2,bits=32,block=8;
    f.write("RIFF",4);f.write(reinterpret_cast<const char*>(&riffSize),4);f.write("WAVEfmt ",8);f.write(reinterpret_cast<const char*>(&fmtSize),4);
    f.write(reinterpret_cast<const char*>(&format),2);f.write(reinterpret_cast<const char*>(&channels),2);f.write(reinterpret_cast<const char*>(&kSr),4);
    f.write(reinterpret_cast<const char*>(&byteRate),4);f.write(reinterpret_cast<const char*>(&block),2);f.write(reinterpret_cast<const char*>(&bits),2);
    f.write("data",4);f.write(reinterpret_cast<const char*>(&dataBytes),4);for(size_t i=0;i<l.size();++i){f.write(reinterpret_cast<const char*>(&l[i]),4);f.write(reinterpret_cast<const char*>(&r[i]),4);}
}

Metrics render(const CloudGreyVerb::FactoryPreset& preset,const std::vector<float>& input,std::vector<float>* outL=nullptr,std::vector<float>* outR=nullptr,bool wetOnly=false){
    // hq_approx validates core rate/timing only. It is deliberately not JUCE/VST spectral ground truth.
    const bool hq=preset.hqMode; std::vector<float> memory(memoryFor()); CloudGreyVerb fx;
    fx.init(hq?kSr*2:kSr,memory.data(),memory.size()); auto p=preset.dsp;if(wetOnly)p.mix=1;p.clipOutput=false;fx.setParams(p);fx.reset();
    std::vector<float> l(input.size()),r(input.size());std::vector<double> energy(input.size());Metrics m;
    double sum=0,weighted=0,eeL=0,eeR=0,eeLR=0,leL=0,leR=0,leLR=0,em=0,es=0,lm=0,ls=0,inputSum=0;float previous=0;
    const int edges[7]={0,20,50,80,200,1000,8000};
    for(size_t i=0;i<input.size();++i){
        if(hq){float dl,dr;fx.processSample(.5f*(previous+input[i]),.5f*(previous+input[i]),dl,dr);fx.processSample(input[i],input[i],l[i],r[i]);}else fx.processSample(input[i],input[i],l[i],r[i]);previous=input[i];
        const double x=l[i],y=r[i],en=x*x+y*y,ms=i*1000.0/kSr,mid=.5*(x+y),side=.5*(x-y);energy[i]=en;sum+=en;weighted+=en*i/kSr;inputSum+=input[i]*input[i];
        m.peak=std::max({m.peak,std::abs(x),std::abs(y)});m.inputPeak=std::max(m.inputPeak,std::abs(static_cast<double>(input[i])));m.minSafety=std::min(m.minSafety,static_cast<double>(fx.getSafetyGain()));if(m.firstMs<0&&en>1e-12)m.firstMs=ms;
        for(int b=0;b<6;++b)if(ms>=edges[b]&&ms<edges[b+1]){m.timeBands[b]+=en;break;}
        if(ms<80){eeL+=x*x;eeR+=y*y;eeLR+=x*y;em+=mid*mid;es+=side*side;}else{leL+=x*x;leR+=y*y;leLR+=x*y;lm+=mid*mid;ls+=side*side;}
    }
    m.rms=std::sqrt(sum/(2*input.size()));m.inputRms=std::sqrt(inputSum/input.size());m.energyCentroid=sum?weighted/sum:0;m.gainDeltaDb=OfflineMetrics::dbRatio(m.rms,m.inputRms);
    m.stereo=OfflineMetrics::stereoMetrics(l,r,{0,l.size()});m.regions=OfflineMetrics::findRegions(input,kSr);
    m.full=OfflineMetrics::welchStereo(l,r,kSr,m.regions.full);m.active=OfflineMetrics::welchStereo(l,r,kSr,m.regions.active);m.tail=OfflineMetrics::welchStereo(l,r,kSr,m.regions.tail);
    m.dryFull=OfflineMetrics::welchStereo(input,input,kSr,m.regions.full);m.dryActive=OfflineMetrics::welchStereo(input,input,kSr,m.regions.active);
    m.corrEarly=eeLR/std::sqrt(std::max(1e-30,eeL*eeR));m.corrLate=leLR/std::sqrt(std::max(1e-30,leL*leR));m.msEarly=es/std::max(1e-30,em);m.msLate=ls/std::max(1e-30,lm);
    std::vector<double> decay(energy.size());double total=0;for(size_t i=energy.size();i--;){total+=energy[i];decay[i]=total;}double sx=0,sy=0,sxx=0,sxy=0;int n=0;
    if(total>0)for(size_t i=0;i<decay.size();++i){double db=10*std::log10(std::max(1e-30,decay[i]/total));if(db<=-5&&db>=-25){double t=i/kSr;sx+=t;sy+=db;sxx+=t*t;sxy+=t*db;++n;}}
    const double d=n*sxx-sx*sx;if(n>100&&std::abs(d)>1e-12){double slope=(n*sxy-sx*sy)/d;if(slope<0)m.rt60=-60/slope;}
    if(outL)*outL=std::move(l);if(outR)*outR=std::move(r);return m;
}

double earlyLate(const Metrics&m){return 10*std::log10(std::max(1e-30,m.timeBands[0]+m.timeBands[1]+m.timeBands[2])/std::max(1e-30,m.timeBands[3]+m.timeBands[4]+m.timeBands[5]));}
void writeIrRow(std::ofstream&csv,const CloudGreyVerb::FactoryPreset&p,const Metrics&m){csv<<p.name<<','<<(p.hqMode?1:0)<<','<<renderMode(p)<<','<<p.dsp.preDelay*200<<','<<p.dsp.stereoWidth<<','<<m.firstMs;for(double b:m.timeBands)csv<<','<<b;csv<<','<<earlyLate(m)<<','<<m.energyCentroid<<','<<m.corrEarly<<','<<m.corrLate<<','<<m.msEarly<<','<<m.msLate<<','<<m.peak<<','<<m.rms<<','<<m.rt60<<','<<m.minSafety<<'\n';}
void bands(std::ofstream&csv,const OfflineMetrics::Spectrum&s){for(double b:s.bandRms)csv<<','<<b;}
void deltas(std::ofstream&csv,const OfflineMetrics::Spectrum&a,const OfflineMetrics::Spectrum&dry){for(int b=0;b<5;++b)csv<<','<<OfflineMetrics::dbRatio(a.bandRms[b],dry.bandRms[b]);}
void writeMusicalRow(std::ofstream&csv,const CloudGreyVerb::FactoryPreset&p,const std::string&source,const Metrics&m){
    csv<<p.name<<','<<source<<','<<(p.hqMode?1:0)<<','<<renderMode(p)<<','<<p.dsp.preDelay*200<<','<<p.dsp.stereoWidth<<','<<m.regions.active.begin/kSr<<','<<m.regions.active.end/kSr<<','<<m.regions.tail.begin/kSr<<','<<m.regions.tail.end/kSr<<','<<m.inputPeak<<','<<m.inputRms<<','<<m.peak<<','<<m.rms<<','<<m.gainDeltaDb<<','<<m.stereo.midRms<<','<<m.stereo.sideRms<<','<<m.stereo.sideMidRatio<<','<<m.stereo.midRms<<','<<m.stereo.monoDeltaDb<<','<<m.full.centroidHz<<','<<m.active.centroidHz<<','<<m.tail.centroidHz;
    bands(csv,m.full);bands(csv,m.active);bands(csv,m.tail);deltas(csv,m.full,m.dryFull);deltas(csv,m.active,m.dryActive);csv<<','<<m.minSafety<<'\n';
}
std::vector<float> signal(int s){std::vector<float>in(kFrames);uint32_t noise=0x9e3779b9u;for(int i=0;i<kFrames;++i){const float t=i/kSr,chord=.12f*(std::sin(2*kPi*220*t)+.65f*std::sin(2*kPi*277.18*t)+.42f*std::sin(2*kPi*329.63*t)),pluck=.55f*(std::sin(2*kPi*220*t)+.35f*std::sin(2*kPi*440*t)+.12f*std::sin(2*kPi*660*t))*std::exp(-18*t);noise=noise*1664525u+1013904223u;const float n=(float((noise>>8)&0xffff)/32767.5f-1.f);if(s==0)in[i]=t<1.4f?.16f*(std::sin(2*kPi*(196.f+.8f*std::sin(2*kPi*2*t))*t)+.28f*std::sin(2*kPi*392*t))*std::exp(-1.5f*t):0;else if(s==1)in[i]=t<1.8f?chord*std::exp(-1.25f*t):0;else if(s==2)in[i]=(t<.16f?chord*std::exp(-9*t):0)+(t>.42f&&t<.58f?chord*std::exp(-9*(t-.42f)):0);else if(s==3)in[i]=t<.7f?pluck:0;else if(s==4)in[i]=t<.55f?.38f*(std::sin(2*kPi*164.81f*t)+.18f*std::sin(2*kPi*329.63f*t))*std::exp(-7*t):0;else if(s==5)in[i]=t<.12f?(.22f*n+.16f*std::sin(2*kPi*190*t))*std::exp(-32*t):0;else if(s==6)in[i]=t<.38f?.52f*std::sin(2*kPi*293.66f*t)*std::exp(-14*t):0;else if(s==7)in[i]=t<2.8f?chord*(.75f+.25f*std::sin(2*kPi*.17f*t)):0;else if(s==8)in[i]=t<1.4f?.26f*std::sin(2*kPi*(t<.7f?55.f:82.41f)*t):0;else in[i]=t<1.6f?(.42f*chord+.35f*pluck+.16f*std::sin(2*kPi*82.41f*t)+.04f*n):0;}return in;}
double median(std::vector<double>v){std::sort(v.begin(),v.end());return .5*(v[(v.size()-1)/2]+v[v.size()/2]);}
}
int main(int argc,char**argv){
    const std::filesystem::path root=argc>1?argv[1]:"build/m3";std::filesystem::create_directories(root/"metrics");const bool renderAudio=argc<3||std::string(argv[2])!="--metrics";
    std::ofstream irCsv(root/"metrics"/"ir_metrics.csv"),musical(root/"metrics"/"musical_metrics.csv"),summary(root/"metrics"/"preset_summary.csv");
    irCsv<<"preset,hq,render_mode,preDelay_ms,width,first_arrival_ms,energy_0_20ms,energy_20_50ms,energy_50_80ms,energy_80_200ms,energy_200_1000ms,energy_after_1s,early_late_ratio_db,energy_centroid_s,stereo_correlation_early,stereo_correlation_late,mid_side_ratio_early,mid_side_ratio_late,peak,rms,rt60_s,min_safety_gain\n";
    musical<<"preset,source,hq,render_mode,preDelay_ms,width,active_start_s,active_end_s,tail_start_s,tail_end_s,input_peak,input_rms,output_peak,output_rms,gain_delta_db,mid_rms,side_rms,side_mid_ratio,mono_fold_rms,mono_delta_db,spectral_centroid_full,spectral_centroid_active,spectral_centroid_tail,full_20_80,full_80_200,full_200_1000,full_1000_4000,full_4000_16000,active_20_80,active_80_200,active_200_1000,active_1000_4000,active_4000_16000,tail_20_80,tail_80_200,tail_200_1000,tail_1000_4000,tail_4000_16000,full_20_80_delta_db,full_80_200_delta_db,full_200_1000_delta_db,full_1000_4000_delta_db,full_4000_16000_delta_db,active_20_80_delta_db,active_80_200_delta_db,active_200_1000_delta_db,active_1000_4000_delta_db,active_4000_16000_delta_db,min_safety_gain\n";
    summary<<"preset,render_mode,median_gain_delta_db,min_gain_delta_db,max_gain_delta_db,median_mono_delta_db,worst_mono_delta_db,median_side_mid_ratio,median_tail_20_80,median_tail_80_200,median_tail_200_1000,median_tail_1000_4000,median_tail_4000_16000,median_tail_centroid_hz,max_output_peak,min_safety_gain\n";
    std::vector<float>impulse(kFrames);impulse[0]=.70710678f;for(size_t p=0;p<CloudGreyVerb::factoryPresetCount();++p){const auto&preset=CloudGreyVerb::getFactoryPreset(p);std::vector<float>l,r;auto m=render(preset,impulse,&l,&r,true);writeIrRow(irCsv,preset,m);if(renderAudio)writeWav(root/"metrics"/(std::string(preset.name)+"_ir.wav"),l,r);if(!std::isfinite(m.peak)||m.peak>8||m.minSafety<.34)return 1;}
    const std::string names[]={"01_dry_vocal","02_piano_chord","03_piano_staccato","04_acoustic_guitar","05_electric_guitar_clean","06_snare","07_synth_pluck","08_synth_pad","09_bass_notes","10_full_mix_excerpt"};
    for(size_t p=0;p<CloudGreyVerb::factoryPresetCount();++p){const auto&preset=CloudGreyVerb::getFactoryPreset(p);std::vector<Metrics>all;for(int s=0;s<10;++s){auto in=signal(s);if(renderAudio)writeWav(root/"dry"/(names[s]+".wav"),in,in);std::vector<float>l,r;auto m=render(preset,in,&l,&r);all.push_back(m);writeMusicalRow(musical,preset,names[s],m);if(renderAudio)writeWav(root/"after"/preset.name/(names[s]+".wav"),l,r);}
        std::vector<double>gain,mono,side,centroid,peaks,safety;std::array<std::vector<double>,5>tail;for(const auto&m:all){gain.push_back(m.gainDeltaDb);mono.push_back(m.stereo.monoDeltaDb);side.push_back(m.stereo.sideMidRatio);centroid.push_back(m.tail.centroidHz);peaks.push_back(m.peak);safety.push_back(m.minSafety);for(int b=0;b<5;++b)tail[b].push_back(m.tail.bandRms[b]);}
        summary<<preset.name<<','<<renderMode(preset)<<','<<median(gain)<<','<<*std::min_element(gain.begin(),gain.end())<<','<<*std::max_element(gain.begin(),gain.end())<<','<<median(mono)<<','<<*std::min_element(mono.begin(),mono.end())<<','<<median(side);for(auto&b:tail)summary<<','<<median(b);summary<<','<<median(centroid)<<','<<*std::max_element(peaks.begin(),peaks.end())<<','<<*std::min_element(safety.begin(),safety.end())<<'\n';
    }
    std::cout<<"Wrote "<<(root/"metrics").string()<<" (FFT 4096, Hann, 50% overlap; HQ is explicitly hq_approx)\n";
}
