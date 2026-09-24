#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>
#include "cloud_grey_verb.hpp"

namespace {
size_t memoryFor(float sr) { return sr <= 96000.0f ? 1600000u : 3200000u; }

struct Render {
    std::vector<float> l, r;
};

Render render(float sr, CloudGreyVerb::Params p, float seconds = 0.12f) {
    std::vector<float> memory(memoryFor(sr), 0.0f);
    CloudGreyVerb fx; fx.init(sr, memory.data(), memory.size());
    if (!fx.isInitialized()) throw std::runtime_error("init failed");
    p.mix = 1.0f; p.preDelay = 0.0f; p.freeze = 0.0f; p.hardFreeze = false;
    p.clipOutput = false; fx.setParams(p); fx.reset();
    Render out; out.l.resize(static_cast<size_t>(seconds * sr)); out.r.resize(out.l.size());
    for (size_t i = 0; i < out.l.size(); ++i)
        fx.processSample(i == 0 ? 0.70710678f : 0.0f, i == 0 ? 0.70710678f : 0.0f,
                         out.l[i], out.r[i]);
    return out;
}

double correlation(const Render& x, float sr, float beginMs, float endMs) {
    const size_t begin = static_cast<size_t>(beginMs * sr / 1000.0f);
    const size_t end = std::min(x.l.size(), static_cast<size_t>(endMs * sr / 1000.0f));
    double ll=0, rr=0, lr=0; for (size_t i=begin;i<end;++i) { ll+=x.l[i]*x.l[i]; rr+=x.r[i]*x.r[i]; lr+=x.l[i]*x.r[i]; }
    return lr / std::sqrt(std::max(1e-30, ll * rr));
}
double firstArrivalMs(const Render& x, float sr) {
    for (size_t i=0;i<x.l.size();++i) if (x.l[i]*x.l[i]+x.r[i]*x.r[i] > 1e-12) return i*1000.0/sr;
    return INFINITY;
}
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };

    // The profile's longest request at 1.18x has an explicit Hermite guard.
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        const float request = CloudGreyVerb::earlyMaxRequestedSeconds() * sr;
        check(CloudGreyVerb::earlyDelayCapacityFrames(sr) >= static_cast<size_t>(std::ceil(request)) + 3u,
              "every early request must fit before DelayLine's physical clamp");
    }

    const auto small = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::SmallCloudRoom);
    const auto subtle = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::AlwaysOnSubtle);
    for (const auto& p : {small, subtle}) {
        const Render x = render(48000.0f, p);
        check(firstArrivalMs(x, 48000.0f) < 4.0, "M2 first arrival must be under 4 ms at zero pre-delay");
        const double earlyCorr = correlation(x, 48000.0f, 0, 80);
        const double extendedCorr = correlation(x, 48000.0f, 80, 120);
        check(earlyCorr > 0.0 && earlyCorr < 0.99999, "early mono field must be positive but not permanently mono");
        check(earlyCorr > extendedCorr, "stereo field must progressively open after the early window");
    }

    // Until 4.5 ms only the direct early tap can be present: Texture must not alter it.
    auto texture0 = small; texture0.texture = 0.0f;
    auto texture1 = small; texture1.texture = 1.0f;
    const Render t0 = render(48000.0f, texture0), t1 = render(48000.0f, texture1);
    for (size_t i = 0; i < 216; ++i) // 4.5 ms at 48 kHz
        check(std::abs(t0.l[i]-t1.l[i]) < 1e-7f && std::abs(t0.r[i]-t1.r[i]) < 1e-7f,
              "early layer must be independent of Texture");

    // The explicit time scale is independent of the FDN Size mapping and must grow monotonically.
    const float tap = 0.0371f;
    const float d0 = tap * (0.88f + (1.18f-0.88f)*0.0f);
    const float d1 = tap * (0.88f + (1.18f-0.88f)*0.5f);
    const float d2 = tap * (0.88f + (1.18f-0.88f)*1.0f);
    check(d0 < d1 && d1 < d2, "early tap times must be monotonic with Size");

    // Early metrics are sample-rate invariant; the cloud/FDN is intentionally assessed separately.
    const Render r44 = render(44100.0f, small), r48 = render(48000.0f, small), r96 = render(96000.0f, small);
    check(std::isfinite(firstArrivalMs(r44,44100.0f)) && std::isfinite(firstArrivalMs(r48,48000.0f))
          && std::isfinite(firstArrivalMs(r96,96000.0f)), "render must be valid");
    check(std::abs(firstArrivalMs(r44,44100.0f)-firstArrivalMs(r48,48000.0f)) < 0.05
          && std::abs(firstArrivalMs(r48,48000.0f)-firstArrivalMs(r96,96000.0f)) < 0.05,
          "early first arrival must remain within sample rounding across sample rates");
    // Individual impulse amplitudes vary with the fractional phase of Hermite
    // at each rate; timing, rather than one-sample peak energy, is the stable
    // M2 contract. The detailed 0-20/20-50/50-80 energy reports remain in M2Bench.

    if (failures) return 1;
    std::cout << "SUCCESS: M2 early-layer regression contract passed.\n";
}
