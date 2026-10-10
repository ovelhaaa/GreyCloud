// Compile this same harness against the pristine core with
// -DCGV_CONTINUITY_BASELINE=1 to reproduce the old failures and produce A/B WAVs.
#include "cloud_grey_verb.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

struct CloudGreyVerbGranularTestAccess {
    float anchorAge[CGV_NUM_GRAINS] = {};
    float previousPosition[CGV_NUM_GRAINS] = {};
    bool primed = false;
    uint64_t invalid = 0, jumps = 0, restarts = 0, wraps = 0;
    float maxAge = 0, maxMotion = 0;
    static void granular(CloudGreyVerb &v, float x, float y, float &l, float &r) {
        v.processGranular(x, y, 0.0f, l, r);
    }
    void inspect(const CloudGreyVerb &v, const float *oldPhase, bool automate) {
        if (v.grainWritePos_ == 0)
            ++wraps;
        const float size = float(v.grainMemorySize_);
        for (int g = 0; g < CGV_NUM_GRAINS; ++g) {
            const bool restart = v.grainPhaseLocal_[g] < oldPhase[g];
            if (restart)
                ++restarts;
            float forward, reverse;
#ifdef CGV_CONTINUITY_BASELINE
            float frames = std::min(
                (0.015f + 0.385f * v.params_.texture) * v.sampleRate_ * v.grainLenMult_[g], size - 100.0f);
            float delay = std::max(2.0f, (v.grainOffsetMs_[g] + v.grainJitter_[g]) * v.sampleRate_ * 0.001f);
            const float offset = v.grainPhaseLocal_[g] * frames;
            anchorAge[g] += 1.0f;
            if (restart)
                anchorAge[g] = delay;
            reverse = anchorAge[g] + offset;
            const float write = float(v.grainWritePos_);
            forward =
                write - cgv_dsp::lerp(write - delay, v.grainAnchorPos_[g] + offset, v.params_.grainScan);
#else
            forward = v.grainForwardAge_[g];
            reverse = v.grainReverseAge_[g];
#endif
            if (reverse > size - 2.0f || reverse < 2.0f)
                ++invalid;
#ifndef CGV_CONTINUITY_BASELINE
            if (forward > size - 2.0f || forward < 2.0f) {
                ++invalid;
            }
#endif
            maxAge = std::max(maxAge, reverse);
            float position = float(v.grainWritePos_) - forward;
            float motion = position - previousPosition[g];
            motion -= size * std::round(motion / size);
            if (primed && !restart && !automate) {
                maxMotion = std::max(maxMotion, std::abs(motion));
                if (std::abs(motion) > 1.2f)
                    ++jumps;
            }
            previousPosition[g] = position;
        }
        primed = true;
    }
    static bool tinyNegativeWrap(CloudGreyVerb &v) {
        // Poison the following channel's first sample: an idx1==size read would
        // return it rather than the left ring's sample zero.
        v.grainMemoryL_[0] = 0.125f;
        v.grainMemoryL_[v.grainMemorySize_ - 1] = 0.125f;
        v.grainMemoryR_[0] = 0.75f;
        float l, r;
        v.readGrainHead(-0.00001f, l, r);
        return std::abs(l - 0.125f) < 0.001f;
    }
    static void phases(const CloudGreyVerb &v, float *p) {
        std::copy(v.grainPhaseLocal_, v.grainPhaseLocal_ + CGV_NUM_GRAINS, p);
    }
#ifndef CGV_CONTINUITY_BASELINE
    static bool settled(const CloudGreyVerb &v) {
        return v.scanFade_ == 1 && v.scanTo_ == v.params_.grainScan &&
               v.reverseForwardGain_ == v.reverseForwardTarget_ &&
               v.reverseBackwardGain_ == v.reverseBackwardTarget_;
    }
#endif
};

struct Metrics {
    double energy = 0, hf = 0, localEnergy = 0;
    float peak = 0, step = 0, curvature = 0, wrapStep = 0, restartStep = 0;
    uint64_t isolated = 0, n = 0;
    float last = 0, previous = 0, lastR = 0, previousR = 0;
    void add(float x, float y, bool wrap, bool restart, float sr) {
        float d = x - last, dd = d - (last - previous);
        float dR = y - lastR, ddR = dR - (lastR - previousR);
        float magnitude = std::max(std::abs(x), std::abs(y));
        // 10 ms local RMS with a fixed amplitude floor; silence cannot create
        // false spikes. Source onsets are reported, not used as failure gates.
        double a = 1 - std::exp(-1.0 / (0.010 * sr));
        localEnergy += a * ((double(x) * x + double(y) * y) * 0.5 - localEnergy);
        if (n > uint64_t(sr) && magnitude > 12 * std::sqrt(std::max(1e-6, localEnergy)))
            ++isolated;
        energy += (double(x) * x + double(y) * y) * 0.5;
        hf += (double(dd) * dd + double(ddR) * ddR) * 0.5;
        peak = std::max(peak, magnitude);
        step = std::max(step, std::max(std::abs(d), std::abs(dR)));
        curvature = std::max(curvature, std::max(std::abs(dd), std::abs(ddR)));
        if (wrap)
            wrapStep = std::max(wrapStep, std::max(std::abs(d), std::abs(dR)));
        if (restart)
            restartStep = std::max(restartStep, std::max(std::abs(d), std::abs(dR)));
        previous = last;
        last = x;
        previousR = lastR;
        lastR = y;
        ++n;
    }
};
void wav(const std::string &name, float sr, const std::vector<float> &audio) {
    std::ofstream f(name, std::ios::binary);
    auto u16 = [&](uint16_t x) {
        f.put(char(x));
        f.put(char(x >> 8));
    };
    auto u32 = [&](uint32_t x) {
        u16(uint16_t(x));
        u16(uint16_t(x >> 16));
    };
    f.write("RIFF", 4);
    u32(36 + uint32_t(audio.size() * sizeof(float)));
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(2);
    u32(uint32_t(sr));
    u32(uint32_t(sr) * 8);
    u16(8);
    u16(32);
    f.write("data", 4);
    u32(uint32_t(audio.size() * sizeof(float)));
    f.write(reinterpret_cast<const char *>(audio.data()), std::streamsize(audio.size() * sizeof(float)));
}
float source(int type, uint64_t n, float sr, uint32_t &rng) {
    double t = double(n) / sr;
    rng = rng * 1664525u + 1013904223u;
    switch (type) {
    case 1:
        return 0.25f * (float(rng >> 8) / 8388608.0f - 1);
    case 2:
        return n % uint64_t(sr * 0.31f) == 0 ? 0.5f : 0;
    case 3:
        return 0.3f * float(std::exp(-40 * std::fmod(t, 0.37)) * std::sin(2 * cgv_dsp::PI * 1700 * t));
    case 4:
        return 0.25f * float(std::sin(2 * cgv_dsp::PI * std::min(12000.0, double(sr) * 0.3) * t));
    default:
        return 0.3f * float(std::sin(2 * cgv_dsp::PI * 997 * t));
    }
}
int blockParity() {
    int failures = 0;
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        constexpr size_t n = 4096;
        std::vector<float> x(n), y(n), expectedL(n), expectedR(n);
        for (size_t i = 0; i < n; ++i) {
            x[i] = 0.2f * std::sin(float(i) * 0.17f);
            y[i] = 0.2f * std::sin(float(i) * 0.11f);
        }
        auto p = CloudGreyVerb::getPreset(CloudGreyVerb::Preset::BrightCloud);
        p.grainScan = .75f;
        p.reverseMix = .35f;
        p.clipOutput = false;
        std::vector<float> pool(CloudGreyVerb::requiredMemoryFloats(sr));
        CloudGreyVerb ref;
        ref.init(sr, pool.data(), pool.size());
        ref.setParams(p);
        ref.reset();
        for (size_t i = 0; i < n; ++i)
            ref.processSample(x[i], y[i], expectedL[i], expectedR[i]);
        for (size_t block : {size_t(1), size_t(17), size_t(64), size_t(257)}) {
            std::vector<float> mem(pool.size()), l = x, r = y;
            CloudGreyVerb v;
            v.init(sr, mem.data(), mem.size());
            v.setParams(p);
            v.reset();
            for (size_t i = 0; i < n; i += block)
                v.processBlock(l.data() + i, r.data() + i, std::min(block, n - i));
            if (l != expectedL || r != expectedR) {
                ++failures;
                std::cerr << "FAIL block parity sr=" << sr << " block=" << block << '\n';
            }
        }
    }
    return failures;
}
int main(int argc, char **argv) {
    const std::string prefix = argc > 1 ? argv[1] : "granular_after";
    const bool quick = argc > 2 && std::string(argv[2]) == "--quick";
    std::ofstream csv(prefix + ".csv");
    csv << "sr,scan,reverse,texture,source,mode,stereo,freeze,automation,rms,peak,max_step,max_curvature,hf_"
           "energy,wrap_step,restart_step,isolated_spikes,invalid_ages,trajectory_jumps,max_age,max_motion,"
           "ns_per_sample,memory_floats\n";
    int failures = blockParity(), cases = 0;
    uint64_t invalid = 0, jumps = 0;
    auto run = [&](float sr, float scan, float reverse, float texture, int src, int mode, bool stereo,
                   int freeze, int automation, bool capture = false, float seconds = 4.0f) {
        const int failuresBefore = failures;
        const size_t count = CloudGreyVerb::requiredMemoryFloats(sr);
        std::vector<float> memory(count + 16, 98765.0f);
        CloudGreyVerb v;
        v.init(sr, memory.data(), count);
        if (!v.isInitialized()) {
            ++failures;
            return;
        }
        if (!CloudGreyVerbGranularTestAccess::tinyNegativeWrap(v))
            ++failures;
        CloudGreyVerb::Params p;
        p.mix = 1;
        p.texture = texture;
        p.reverseMix = reverse;
        p.grainScan = scan;
        p.clipOutput = false;
        p.shimmer = 0;
        p.stereoCore = stereo;
        p.feedback = 0.6f;
        v.setParams(p);
        v.reset();
        CloudGreyVerbComponentTestAccess::setIsolation(v,
                                                       static_cast<CloudGreyVerb::ComponentIsolation>(mode));
        CloudGreyVerbGranularTestAccess probe;
        Metrics m;
        uint32_t rng = 1234567;
        std::vector<float> audio;
        const uint64_t frames = uint64_t(sr * (quick ? 1.4f : seconds));
        if (capture)
            audio.reserve(size_t(frames) * 2);
        auto begin = std::chrono::steady_clock::now();
        for (uint64_t n = 0; n < frames; ++n) {
            float t = float(n) / sr;
            if (automation && n % 31 == 0) {
                float control = automation == 1   ? 0.5f + 0.5f * std::sin(2 * cgv_dsp::PI * 0.25f * t)
                                : automation == 2 ? 0.5f + 0.5f * std::sin(2 * cgv_dsp::PI * 40 * t)
                                                  : float((n / uint64_t(sr * 0.13f)) % 2);
                p.grainScan = control;
                p.reverseMix = 1 - control;
                p.texture = texture == 1 ? control : texture;
                v.setParams(p);
            }
            if (freeze && n == uint64_t(sr * 0.8f)) {
                p.freeze = 1;
                p.hardFreeze = freeze == 2;
                v.setParams(p);
            }
            if (freeze && n == uint64_t(sr * 2.8f)) {
                p.freeze = 0;
                p.hardFreeze = false;
                v.setParams(p);
            }
            float x = source(src, n, sr, rng), y = source(src, n + 137, sr, rng);
            // A 10 ms source release avoids treating a deliberately cut sine
            // as a DSP click, while preserving the required silent tail.
            const float release = std::min(1.0f, std::max(0.0f, (3.0f - t) * 100.0f));
            const float attack = std::min(1.0f, t * 100.0f);
            x *= attack * release;
            y *= attack * release;
            float oldPhase[CGV_NUM_GRAINS];
            CloudGreyVerbGranularTestAccess::phases(v, oldPhase);
            float l = 0, r = 0;
            if (mode == 2)
                CloudGreyVerbGranularTestAccess::granular(v, x, y, l, r);
            else
                v.processSample(x, y, l, r);
            auto oldRestarts = probe.restarts, oldWraps = probe.wraps;
            probe.inspect(v, oldPhase, automation != 0);
            if (!std::isfinite(l) || !std::isfinite(r)) {
                ++failures;
                break;
            }
            m.add(l, r, probe.wraps != oldWraps, probe.restarts != oldRestarts, sr);
            if (capture) {
                audio.push_back(l);
                audio.push_back(r);
            }
        }
        const double ns =
            std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - begin).count() /
            frames;
        invalid += probe.invalid;
        jumps += probe.jumps;
        if (probe.invalid || probe.jumps)
            ++failures;
        for (size_t i = count; i < memory.size(); ++i)
            if (memory[i] != 98765.0f)
                ++failures;
        // Smooth sine has a bounded second difference; a discontinuous ring
        // jump exceeds this by orders of magnitude. Legitimate transient and
        // HF sources intentionally do not share that assertion.
        if (src == 0 && mode == 2 && m.curvature > 0.025f)
            ++failures;
        csv << sr << ',' << scan << ',' << reverse << ',' << texture << ',' << src << ',' << mode << ','
            << stereo << ',' << freeze << ',' << automation << ',' << std::sqrt(m.energy / m.n) << ','
            << m.peak << ',' << m.step << ',' << m.curvature << ',' << m.hf / m.n << ',' << m.wrapStep << ','
            << m.restartStep << ',' << m.isolated << ',' << probe.invalid << ',' << probe.jumps << ','
            << probe.maxAge << ',' << probe.maxMotion << ',' << ns << ',' << count << '\n';
        if (capture)
            wav(prefix + "_" + std::to_string(cases) + ".wav", sr, audio);
#ifndef CGV_CONTINUITY_BASELINE
        // Exact final targets, not an asymptotic smoother; reset seeds them.
        p.grainScan = 0.75f;
        p.reverseMix = 0.25f;
        v.setParams(p);
        for (int n = 0; n < int(sr * 0.045f); ++n) {
            float l, r;
            CloudGreyVerbGranularTestAccess::granular(v, 0, 0, l, r);
        }
        if (!CloudGreyVerbGranularTestAccess::settled(v))
            ++failures;
        v.reset();
        if (!CloudGreyVerbGranularTestAccess::settled(v))
            ++failures;
#endif
        if (failures != failuresBefore)
            std::cerr << "FAIL case=" << cases << " sr=" << sr << " scan=" << scan << " reverse=" << reverse
                      << " texture=" << texture << " source=" << src << " mode=" << mode
                      << " automation=" << automation << " invalid=" << probe.invalid
                      << " jumps=" << probe.jumps << " curvature=" << m.curvature << '\n';
        ++cases;
    };
    if (!quick) {
        for (float sr : {44100.f, 48000.f, 96000.f})
            for (float scan : {0.f, .25f, .5f, .75f, 1.f})
                for (float reverse : {0.f, .25f, .5f, .75f, 1.f})
                    for (float texture : {0.f, .5f, 1.f})
                        run(sr, scan, reverse, texture, 0, 2, true, 0, 0);
    }
    // Isolate every downstream component and exercise actual 2x HQ core rates.
    for (float sr : {44100.f, 48000.f, 96000.f, 88200.f, 192000.f})
        for (int mode : {0, 2, 3, 4})
            for (int src = 0; src < 5; ++src)
                run(sr, .5f, 1, 1, src, mode, src % 2 == 0, src % 3, src % 4);
    // A/B recordings: static circular wrap, rapid automation, and frozen tail.
    run(48000, .5f, 0, 1, 0, 2, true, 0, 0, true);
    run(48000, .5f, 1, 1, 0, 2, true, 0, 0, true);
    run(48000, .5f, .5f, 1, 0, 2, true, 0, 3, true);
    run(48000, 1, 1, 1, 3, 0, false, 2, 2, true);
    if (!quick) {
        // Long complete tails, after the source ends at 3 s, in both core modes.
        run(48000, .75f, 1, 1, 3, 0, true, 1, 3, false, 16.0f);
        run(96000, .25f, .5f, 0, 1, 0, false, 2, 2, false, 16.0f);
    }
    std::cout << "cases=" << cases << " invalid_ages=" << invalid << " trajectory_jumps=" << jumps
              << " failures=" << failures << " metrics=" << prefix << ".csv\n";
    return failures ? 1 : 0;
}
