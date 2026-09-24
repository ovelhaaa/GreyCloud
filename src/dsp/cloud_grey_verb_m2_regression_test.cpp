#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#include "cloud_grey_verb.hpp"

// Friend adapter: production retains a private processEarly() API while this
// translation unit measures the real early network without granular, FDN, tone
// or stereo-width processing.
struct CloudGreyVerbEarlyTestAccess {
    static constexpr size_t tapCount() { return CGV_NUM_EARLY_TAPS; }
    static constexpr const CloudGreyVerb::EarlyTapSpec& tap(size_t i) { return CloudGreyVerb::kEarlyTaps[i]; }
    static constexpr float minTimeScale() { return CloudGreyVerb::kEarlyMinTimeScale; }
    static constexpr float maxTimeScale() { return CloudGreyVerb::kEarlyMaxTimeScale; }
    static float delaySeconds(size_t i, bool right, float size) {
        const auto& spec = tap(i);
        return (right ? spec.delayRSeconds : spec.delayLSeconds) * cgv_dsp::lerp(minTimeScale(), maxTimeScale(), size);
    }
    static void process(CloudGreyVerb& verb, float inL, float inR, float diffusion, float size, float& outL, float& outR) {
        verb.processEarly(inL, inR, diffusion, size, outL, outR);
    }
};

namespace {
size_t memoryFor(float sr) { return sr <= 96000.0f ? 1600000u : 3200000u; }
struct Render { std::vector<float> l, r; };

Render renderEarly(float sr, CloudGreyVerb::Params params, float seconds = 0.06f) {
    std::vector<float> memory(memoryFor(sr), 0.0f);
    CloudGreyVerb verb; verb.init(sr, memory.data(), memory.size());
    if (!verb.isInitialized()) throw std::runtime_error("early test init failed");
    verb.setParams(params); verb.reset();
    Render result; result.l.resize(static_cast<size_t>(seconds * sr)); result.r.resize(result.l.size());
    for (size_t i = 0; i < result.l.size(); ++i)
        CloudGreyVerbEarlyTestAccess::process(verb, i == 0 ? 0.70710678f : 0.0f, i == 0 ? 0.70710678f : 0.0f,
                                               params.diffusion, params.size, result.l[i], result.r[i]);
    return result;
}
double correlation(const Render& x, float sr, float beginMs, float endMs) {
    const size_t begin = static_cast<size_t>(beginMs * sr / 1000.0f);
    const size_t end = std::min(x.l.size(), static_cast<size_t>(endMs * sr / 1000.0f));
    double ll = 0, rr = 0, lr = 0;
    for (size_t i = begin; i < end; ++i) { ll += x.l[i]*x.l[i]; rr += x.r[i]*x.r[i]; lr += x.l[i]*x.r[i]; }
    return lr / std::sqrt(std::max(1e-30, ll * rr));
}
double sideEnergy(const Render& x, float sr, float beginMs, float endMs) {
    const size_t begin = static_cast<size_t>(beginMs * sr / 1000.0f);
    const size_t end = std::min(x.l.size(), static_cast<size_t>(endMs * sr / 1000.0f));
    double sum = 0; for (size_t i = begin; i < end; ++i) { const double side = 0.5 * (x.l[i] - x.r[i]); sum += side * side; }
    return sum;
}
double firstArrivalMs(const Render& x, float sr) {
    for (size_t i = 0; i < x.l.size(); ++i) if (x.l[i]*x.l[i] + x.r[i]*x.r[i] > 1e-12) return i * 1000.0 / sr;
    return std::numeric_limits<double>::infinity();
}
constexpr const char* profileName() {
#if CLOUD_GREY_PROFILE_H5_LOW_CPU
    return "H5 Low CPU";
#elif CLOUD_GREY_PROFILE_H7_HIGH_QUALITY
    return "H7 High Quality";
#elif CLOUD_GREY_PROFILE_DESKTOP_STUDIO
    return "Desktop Studio";
#else
    return "H5 Balanced";
#endif
}
}

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { std::cerr << "FAIL [" << profileName() << "]: " << message << '\n'; ++failures; } };
    const auto preset = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::SmallCloudRoom);

    // Deterministic time-domain contract: capacity comes from active acoustic taps plus the Hermite guard.
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        float largest = 0.0f;
        for (size_t i = 0; i < CloudGreyVerbEarlyTestAccess::tapCount(); ++i) {
            largest = std::max(largest, CloudGreyVerbEarlyTestAccess::delaySeconds(i, false, 1.0f));
            largest = std::max(largest, CloudGreyVerbEarlyTestAccess::delaySeconds(i, true, 1.0f));
        }
        check(std::abs(largest - CloudGreyVerb::earlyMaxRequestedSeconds()) < 1e-9f, "capacity request must derive from active tap specification");
        check(CloudGreyVerb::earlyDelayCapacityFrames(sr) >= static_cast<size_t>(std::ceil(largest * sr)) + 3u, "no valid early tap may reach DelayLine's physical clamp");
    }

    for (size_t tap = 0; tap < CloudGreyVerbEarlyTestAccess::tapCount(); ++tap) for (bool right : {false, true}) {
        float previous = 0.0f;
        for (float size : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float seconds = CloudGreyVerbEarlyTestAccess::delaySeconds(tap, right, size);
            check(seconds > previous, "Size must increase each early-tap delay monotonically"); previous = seconds;
            for (float sr : {44100.0f, 48000.0f, 96000.0f}) check(seconds * sr > 1.0f, "no early tap may hit DelayLine's one-sample clamp");
        }
    }
    for (bool right : {false, true}) for (float size : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        for (size_t tap = 1; tap < CloudGreyVerbEarlyTestAccess::tapCount(); ++tap)
            check(CloudGreyVerbEarlyTestAccess::delaySeconds(tap-1, right, size) < CloudGreyVerbEarlyTestAccess::delaySeconds(tap, right, size), "Size must not invert tap order");

    const Render early48 = renderEarly(48000.0f, preset);
    check(firstArrivalMs(early48, 48000.0f) > 2.5 && firstArrivalMs(early48, 48000.0f) < 3.5, "first isolated reflection must arrive near 3 ms");
    const double firstCorr = correlation(early48, 48000.0f, 2.4f, 5.5f);
    const float finalMs = 1000.0f * CloudGreyVerb::earlyMaxRequestedSeconds() + 1.0f;
    const double middleCorr = correlation(early48, 48000.0f, 6.0f, std::min(20.0f, finalMs));
    const double firstSide = sideEnergy(early48, 48000.0f, 2.4f, 5.5f);
    const double fullSide = sideEnergy(early48, 48000.0f, 5.5f, finalMs);
    check(firstCorr > 0.995, "first reflection must be highly correlated for mono input");
    check(middleCorr < firstCorr - 0.05 && middleCorr > -0.20, "intermediate reflections must decorrelate without phase inversion");
    check(fullSide > firstSide * 20.0 + 1e-12, "completed early field must have materially more Side energy than its first reflection");
    if (CloudGreyVerbEarlyTestAccess::tapCount() >= 4)
        check(sideEnergy(early48, 48000.0f, 20.0f, 50.0f) > firstSide * 5.0 + 1e-12, "20-50 ms field must be wider than first reflection");
    for (float sample : early48.l) check(std::isfinite(sample), "isolated early output must be finite");
    for (float sample : early48.r) check(std::isfinite(sample), "isolated early output must be finite");

    // Texture, modulation, shimmer and granular freeze/feedback cannot affect early processing.
    auto texture0 = preset; texture0.texture = 0.0f; texture0.modDepth = texture0.modRate = texture0.shimmer = texture0.feedback = texture0.freeze = 0.0f;
    auto texture1 = texture0; texture1.texture = texture1.modDepth = texture1.modRate = texture1.shimmer = texture1.feedback = texture1.freeze = 1.0f; texture1.hardFreeze = true;
    const Render t0 = renderEarly(48000.0f, texture0), t1 = renderEarly(48000.0f, texture1);
    for (size_t i = 0; i < t0.l.size(); ++i) check(t0.l[i] == t1.l[i] && t0.r[i] == t1.r[i], "granular-only controls must not alter isolated early output");

    // Whole-engine stochastic IR tolerances belong to other tests; this isolated timing contract allows only sample quantisation.
    const Render r44 = renderEarly(44100.0f, preset), r96 = renderEarly(96000.0f, preset);
    const double a44 = firstArrivalMs(r44, 44100.0f), a48 = firstArrivalMs(early48, 48000.0f), a96 = firstArrivalMs(r96, 96000.0f);
    check(std::isfinite(a44) && std::isfinite(a48) && std::isfinite(a96), "all rates must produce early output");
    check(std::abs(a44-a48) < 0.05 && std::abs(a48-a96) < 0.05, "early timing must agree across 44.1/48/96 kHz within sample quantisation");

    if (failures) return 1;
    std::cout << "SUCCESS [" << profileName() << "]: isolated M2.2 early-layer contract passed.\n";
}
