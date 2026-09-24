#include "cloud_grey_verb.hpp"
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
bool same(float a, float b) { return std::abs(a - b) < 1.0e-7f; }
bool sameParams(const CloudGreyVerb::Params& a, const CloudGreyVerb::Params& b) {
    return same(a.mix,b.mix) && same(a.texture,b.texture) && same(a.freeze,b.freeze)
        && same(a.feedback,b.feedback) && same(a.size,b.size) && same(a.diffusion,b.diffusion)
        && same(a.modDepth,b.modDepth) && same(a.modRate,b.modRate) && same(a.damping,b.damping)
        && same(a.tone,b.tone) && same(a.shimmer,b.shimmer) && a.shimmerRatioIndex==b.shimmerRatioIndex
        && same(a.inputGain,b.inputGain) && same(a.outputGain,b.outputGain) && same(a.preDelay,b.preDelay)
        && same(a.stereoWidth,b.stereoWidth) && same(a.lowDamping,b.lowDamping)
        && a.stereoCore==b.stereoCore && a.hardFreeze==b.hardFreeze && a.clipOutput==b.clipOutput
        && same(a.sizeScale,b.sizeScale) && same(a.reverseMix,b.reverseMix) && same(a.grainScan,b.grainScan);
}
}

int main() {
    if (CloudGreyVerb::factoryPresetCount() != 10) return 1;
    for (size_t i=0; i<CloudGreyVerb::factoryPresetCount(); ++i) {
        const auto& factory = CloudGreyVerb::getFactoryPreset(i);
        if (!factory.name || !*factory.name || factory.syncDivisionIndex < 0 || factory.syncDivisionIndex > 12) return 2;
        if (!sameParams(factory.dsp, CloudGreyVerb::getPreset(static_cast<CloudGreyVerb::Preset>(i)))) return 3;
        for (size_t j=0; j<i; ++j)
            if (std::strcmp(factory.name, CloudGreyVerb::getFactoryPreset(j).name) == 0) return 4;
    }
    const auto& bass = CloudGreyVerb::getFactoryPreset(CloudGreyVerb::Preset::BassAmbientWash);
    const auto& greyhole = CloudGreyVerb::getFactoryPreset(CloudGreyVerb::Preset::GreyholeDelayVerb);
    const auto& shimmer = CloudGreyVerb::getFactoryPreset(CloudGreyVerb::Preset::ShimmerCloud);
    if (!same(bass.dsp.preDelay,.10f) || !same(bass.dsp.stereoWidth,1.5f)
        || !same(greyhole.dsp.preDelay,.20f) || !shimmer.hqMode || !same(shimmer.dsp.stereoWidth,1.4f)) return 5;
    std::cout << "Factory preset parity: 10 canonical states verified\n";
}
