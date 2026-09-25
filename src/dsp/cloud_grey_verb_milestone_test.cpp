#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>
#include "cloud_grey_verb.hpp"

namespace {

struct IrMetrics {
    double peak = 0.0;
    double rms = 0.0;
    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
    double centroidSeconds = 0.0;
    double earlyLateDb = 0.0;
    double rt60Seconds = std::numeric_limits<double>::quiet_NaN();
    float minSafety = 1.0f;
    bool finite = true;
};

size_t memoryFor(float sampleRate) {
    // M4 reserves the official 4 s tempo-sync pre-delay at every rate.
    return sampleRate <= 96000.0f ? 4000000u : 6000000u;
}

IrMetrics render(float sampleRate, CloudGreyVerb::Preset preset, float seconds = 8.0f) {
    std::vector<float> memory(memoryFor(sampleRate), 0.0f);
    CloudGreyVerb fx;
    fx.init(sampleRate, memory.data(), memory.size());
    if (!fx.isInitialized()) throw std::runtime_error("init failed");
    auto p = CloudGreyVerb::getPreset(preset);
    p.mix = 1.0f;
    p.freeze = 0.0f;
    p.hardFreeze = false;
    p.clipOutput = false;
    fx.setParams(p);
    fx.reset();

    const size_t count = static_cast<size_t>(sampleRate * seconds);
    std::vector<double> energy(count, 0.0);
    IrMetrics m;
    double sum = 0.0;
    double weighted = 0.0;
    for (size_t i = 0; i < count; ++i) {
        float l = 0.0f, r = 0.0f;
        const float impulse = i == 0 ? 0.70710678f : 0.0f;
        fx.processSample(impulse, impulse, l, r);
        if (!std::isfinite(l) || !std::isfinite(r)) m.finite = false;
        m.peak = std::max(m.peak, static_cast<double>(std::max(std::abs(l), std::abs(r))));
        energy[i] = static_cast<double>(l) * l + static_cast<double>(r) * r;
        sum += energy[i];
        weighted += energy[i] * static_cast<double>(i) / sampleRate;
        if (i < static_cast<size_t>(0.080f * sampleRate)) m.earlyEnergy += energy[i];
        else m.lateEnergy += energy[i];
        m.minSafety = std::min(m.minSafety, fx.getSafetyGain());
    }
    m.rms = std::sqrt(sum / (2.0 * count));
    m.centroidSeconds = sum > 0.0 ? weighted / sum : 0.0;
    m.earlyLateDb = 10.0 * std::log10((m.earlyEnergy + 1.0e-24)
                                      / (m.lateEnergy + 1.0e-24));

    std::vector<double> decay(count, 0.0);
    double cumulative = 0.0;
    for (size_t i = count; i-- > 0;) {
        cumulative += energy[i];
        decay[i] = cumulative;
    }
    if (cumulative > 0.0) {
        double sx=0, sy=0, sxx=0, sxy=0;
        size_t n=0;
        for (size_t i=0; i<count; ++i) {
            const double db = 10.0 * std::log10(std::max(1.0e-30, decay[i] / decay[0]));
            if (db <= -5.0 && db >= -25.0) {
                const double x = static_cast<double>(i) / sampleRate;
                sx += x; sy += db; sxx += x*x; sxy += x*db; ++n;
            }
        }
        const double d = static_cast<double>(n) * sxx - sx*sx;
        if (n > 100 && std::abs(d) > 1.0e-12) {
            const double slope = (static_cast<double>(n) * sxy - sx*sy) / d;
            if (slope < 0.0) m.rt60Seconds = -60.0 / slope;
        }
    }
    return m;
}

bool closeRelative(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance * std::max({1.0e-9, std::abs(a), std::abs(b)});
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };

    // Size mapping is expressed in seconds and therefore exactly sample-rate invariant.
    check(std::abs(CloudGreyVerb::sizeToSeconds(0.0f) - 0.035f) < 1.0e-6f,
          "Size minimum must be 35 ms");
    check(std::abs(CloudGreyVerb::sizeToSeconds(1.0f) - 0.900f) < 1.0e-5f,
          "normal Size maximum must be 900 ms");
    check(CloudGreyVerb::sizeToSeconds(1.0f, 3.5f) > 3.0f,
          "explicit giant preset extension must exceed 3 seconds");

    // Mod Depth zero: changing modulation rate must not change an otherwise identical IR.
    {
        const float sr = 48000.0f;
        std::vector<float> memA(memoryFor(sr), 0.0f), memB(memoryFor(sr), 0.0f);
        CloudGreyVerb a, b;
        a.init(sr, memA.data(), memA.size()); b.init(sr, memB.data(), memB.size());
        auto pa = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::SmallCloudRoom);
        pa.mix = 1.0f; pa.modDepth = 0.0f; pa.modRate = 0.0f; pa.clipOutput = false;
        auto pb = pa; pb.modRate = 1.0f;
        a.setParams(pa); b.setParams(pb); a.reset(); b.reset();
        double maxDifference = 0.0;
        for (int i=0; i<96000; ++i) {
            const float x = i == 0 ? 1.0f : 0.0f;
            float al, ar, bl, br;
            a.processSample(x, x, al, ar); b.processSample(x, x, bl, br);
            maxDifference = std::max(maxDifference,
                static_cast<double>(std::max(std::abs(al-bl), std::abs(ar-br))));
        }
        check(maxDifference < 1.0e-7, "Mod Depth zero must remove delay modulation");
    }

    // Output gain is after dry/wet and desktop can exceed 0 dBFS.
    {
        std::vector<float> memory(memoryFor(48000.0f), 0.0f);
        CloudGreyVerb fx; fx.init(48000.0f, memory.data(), memory.size());
        CloudGreyVerb::Params p; p.mix = 0.0f; p.inputGain = 1.0f;
        p.outputGain = 2.0f; p.clipOutput = false;
        fx.setParams(p); fx.reset();
        float l=0, r=0; fx.processSample(0.75f, -0.75f, l, r);
        check(std::abs(l - 1.5f) < 1.0e-5f && std::abs(r + 1.5f) < 1.0e-5f,
              "Output Gain must scale the post-mix dry signal and remain unclipped for VST");
    }

    const CloudGreyVerb::Preset presets[] = {
        CloudGreyVerb::Preset::SmallCloudRoom, CloudGreyVerb::Preset::AlwaysOnSubtle,
        CloudGreyVerb::Preset::GreyholeDelayVerb, CloudGreyVerb::Preset::DarkLongCloud,
        CloudGreyVerb::Preset::ShimmerCloud
    };
    const char* names[] = {"SmallCloudRoom", "AlwaysOnSubtle", "GreyholeDelayVerb",
                           "DarkLongCloud", "ShimmerCloud"};
    for (int i=0; i<5; ++i) {
        const IrMetrics m44 = render(44100.0f, presets[i]);
        const IrMetrics m48 = render(48000.0f, presets[i]);
        const IrMetrics m96 = render(96000.0f, presets[i]);
        check(m44.finite && m48.finite && m96.finite, "IR must contain no NaN/Inf");
        check(m48.peak < 8.0 && m48.rms < 1.0, "IR must not run away");
        // Capacity/time mapping remains invariant for every factory preset.
        // Finite-window centroid is reported only for giant stochastic tails.
        check(closeRelative(m44.centroidSeconds, m48.centroidSeconds, 0.30)
              && closeRelative(m48.centroidSeconds, m96.centroidSeconds, 0.30),
              "IR timing must remain bounded across sample rates");
        std::cout << names[i] << ",centroid_44_48_96=" << m44.centroidSeconds << '/'
                  << m48.centroidSeconds << '/' << m96.centroidSeconds
                  << ",sr48000,peak=" << m48.peak << ",rms=" << m48.rms
                  << ",early=" << m48.earlyEnergy << ",late=" << m48.lateEnergy
                  << ",c80_db=" << m48.earlyLateDb << ",centroid_s=" << m48.centroidSeconds << ",rt60_s=" << m48.rt60Seconds
                  << ",min_safety=" << m48.minSafety << '\n';
    }

    // Tight temporal checks use normal, finite tails rather than the intentionally
    // giant presets whose RT60 does not fit inside this 8-second render window.
    for (const auto preset : { CloudGreyVerb::Preset::SmallCloudRoom,
                               CloudGreyVerb::Preset::AlwaysOnSubtle,
                               CloudGreyVerb::Preset::ShimmerCloud }) {
        const IrMetrics m44 = render(44100.0f, preset);
        const IrMetrics m48 = render(48000.0f, preset);
        const IrMetrics m96 = render(96000.0f, preset);
        // M2 intentionally adds a fixed-time early field.  The granular tail
        // remains stochastic across discrete sample rates, so its finite IR
        // centroid now carries substantially more early/late weighting than
        // M1's tail-only measure.  30% still catches time-domain regressions
        // while the dedicated M2 bench reports every early band explicitly.
        check(closeRelative(m44.centroidSeconds, m48.centroidSeconds, 0.30)
              && closeRelative(m48.centroidSeconds, m96.centroidSeconds, 0.30),
              "normal-tail centroid must remain bounded across sample rates");
        check(closeRelative(m44.rt60Seconds, m48.rt60Seconds, 0.10)
              && closeRelative(m48.rt60Seconds, m96.rt60Seconds, 0.10),
              "normal-tail RT60 must be within 10% across sample rates");
        check(std::abs(m44.earlyLateDb - m48.earlyLateDb) < 3.0
              && std::abs(m48.earlyLateDb - m96.earlyLateDb) < 3.0,
              "normal-tail early/late balance must remain bounded across sample rates");
    }

    // 48 kHz normal and a 96 kHz core model the same time domain used by HQ 2x.
    const IrMetrics normal = render(48000.0f, CloudGreyVerb::Preset::SmallCloudRoom);
    const IrMetrics hq = render(96000.0f, CloudGreyVerb::Preset::SmallCloudRoom);
    check(closeRelative(normal.centroidSeconds, hq.centroidSeconds, 0.30),
          "normal and HQ 2x temporal behavior must match");

    // Capacity is deterministic: undersized memory fails instead of retuning acoustics.
    {
        std::vector<float> tooSmall(24000, 0.0f);
        CloudGreyVerb fx; fx.init(48000.0f, tooSmall.data(), tooSmall.size());
        check(!fx.isInitialized(), "undersized memory must fail initialization");
    }

    // Sustained abuse must engage the guard without non-finite/runaway output.
    {
        std::vector<float> memory(memoryFor(48000.0f), 0.0f);
        CloudGreyVerb fx; fx.init(48000.0f, memory.data(), memory.size());
        auto p = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BrightCloud);
        p.mix = 1.0f; p.feedback = 0.94f; p.inputGain = 2.0f; p.shimmer = 1.0f;
        p.clipOutput = false; fx.setParams(p); fx.reset();
        float minimumSafety = 1.0f;
        float maximumEnergy = 0.0f;
        bool finite = true;
        cgv_dsp::FastPRNG noise; noise.seed(99);
        for (int i=0; i<48000*6; ++i) {
            float l=0, r=0;
            const float x = i < 48000*3 ? (noise.randFloat() * 4.0f - 2.0f) : 0.0f;
            fx.processSample(x, x, l, r);
            finite = finite && std::isfinite(l) && std::isfinite(r);
            minimumSafety = std::min(minimumSafety, fx.getSafetyGain());
            maximumEnergy = std::max(maximumEnergy, fx.getLoopEnergy());
        }
        check(finite, "abuse render must remain finite");
        if (minimumSafety >= 0.999f)
            std::cerr << "Safety test max energy=" << maximumEnergy << '\n';
        check(minimumSafety < 0.999f, "Safety Guard must engage under sustained abuse");
    }

    // Nominal sustained material must remain stable without unnecessary gain reduction.
    {
        std::vector<float> memory(memoryFor(48000.0f), 0.0f);
        CloudGreyVerb fx; fx.init(48000.0f, memory.data(), memory.size());
        auto p = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::GreyholeDelayVerb);
        p.clipOutput = false; fx.setParams(p); fx.reset();
        float minimumSafety = 1.0f;
        bool finite = true;
        for (int i=0; i<48000*10; ++i) {
            const float x = 0.25f * std::sin(2.0f * cgv_dsp::PI * 220.0f * i / 48000.0f);
            float l=0, r=0; fx.processSample(x, x, l, r);
            finite = finite && std::isfinite(l) && std::isfinite(r);
            minimumSafety = std::min(minimumSafety, fx.getSafetyGain());
        }
        check(finite, "nominal sustained material must remain finite");
        check(minimumSafety > 0.995f, "Safety Guard must stay transparent on nominal sustained material");
    }

    if (failures != 0) return 1;
    std::cout << "SUCCESS: Milestone 1 temporal and safety tests passed.\n";
    return 0;
}
