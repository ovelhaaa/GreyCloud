#pragma once
// Included only by the instrumented regression executable. No audio-thread I/O
// or additional probe storage is compiled into the plugin, WASM or MCU builds.
#include "cloud_grey_verb.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

struct DspSafetyProbe {
    using Stages = CloudGreyVerb::ComponentProbe;
    Stages peakStages{}, maxima{}, referenceStages{};
    CloudGreyVerb::Params peakParams{};
    cgv_dsp::DelayReadProbe peakRead{};
    uint64_t peakFrame = 0, firstInvalidFrame = UINT64_MAX, invalidAtStart = 0;
    float peak = -1.0f;
    bool finite = true;

    DspSafetyProbe() : invalidAtStart(cgv_dsp::delayReadProbe.invalidReads) {}

    template <size_t N>
    void track(const float (&values)[N], float (&maxValues)[N]) {
        for (size_t i = 0; i < N; ++i) {
            finite &= std::isfinite(values[i]);
            maxValues[i] = std::max(maxValues[i], std::abs(values[i]));
        }
    }

    void observe(uint64_t frame, const CloudGreyVerb::Params& params, const Stages& s) {
        if (frame == 623509) referenceStages = s;
        track(s.input, maxima.input);
        track(s.predelay, maxima.predelay);
        track(s.injection, maxima.injection);
        track(s.rawRead, maxima.rawRead);
        track(s.filteredRead, maxima.filteredRead);
        track(s.matrix, maxima.matrix);
        track(s.beforeSaturation, maxima.beforeSaturation);
        track(s.afterSaturation, maxima.afterSaturation);
        track(s.afterGuard, maxima.afterGuard);
        track(s.loopWrite, maxima.loopWrite);
        track(s.final, maxima.final);
        float pairs[] = {s.earlyL, s.earlyR, s.granL, s.granR, s.diffL, s.diffR, s.tailL, s.tailR};
        for (float x : pairs) finite &= std::isfinite(x);
        maxima.granL = std::max(maxima.granL, std::abs(s.granL));
        maxima.granR = std::max(maxima.granR, std::abs(s.granR));
        maxima.diffL = std::max(maxima.diffL, std::abs(s.diffL));
        maxima.diffR = std::max(maxima.diffR, std::abs(s.diffR));
        maxima.loopEnergy = std::max(maxima.loopEnergy, s.loopEnergy);
        if (firstInvalidFrame == UINT64_MAX && cgv_dsp::delayReadProbe.invalidReads > invalidAtStart)
            firstInvalidFrame = frame;
        const float magnitude = std::max(std::abs(s.final[0]), std::abs(s.final[1]));
        if (magnitude > peak) {
            peak = magnitude;
            peakFrame = frame;
            peakStages = s;
            peakParams = params;
            peakRead = cgv_dsp::delayReadProbe;
        }
    }

    bool safe() const {
        // Existing gentleSaturate has an absolute ceiling 1/sqrt(0.15).
        // Hermite's maximum sum of absolute weights is 1.25, hence every
        // stored-line read must stay below 3.228 even with full loop headroom.
        // This checks the internal read, before any output clipping/filtering.
        for (float x : maxima.rawRead) if (x > 3.23f) return false;
        for (float x : maxima.afterSaturation) if (x > 2.583f) return false;
        return finite && cgv_dsp::delayReadProbe.invalidReads == invalidAtStart;
    }

    void report(const std::string& path, float sr) const {
        std::ofstream f(path);
        f << std::setprecision(10);
        f << "peak_frame," << peakFrame << "\ntime_seconds," << double(peakFrame) / sr
          << "\nfirst_invalid_frame," << firstInvalidFrame
          << "\ninvalid_reads," << cgv_dsp::delayReadProbe.invalidReads - invalidAtStart
          << "\nscan," << peakParams.grainScan << "\nreverse," << peakParams.reverseMix
          << "\ntexture," << peakParams.texture << "\nfreeze_target," << peakParams.freeze
          << "\nfreeze_smoothed," << peakStages.freeze << "\nfeedback," << peakStages.feedback
          << "\ninvalid_ring_size," << peakRead.size << "\ninvalid_index," << peakRead.index
          << "\ninvalid_position," << peakRead.position << "\ninvalid_delay," << peakRead.delay
          << "\ninvalid_fraction," << peakRead.fraction << '\n';
        auto row = [&](const char* name, const float* data, size_t count) {
            f << name;
            for (size_t i = 0; i < count; ++i) f << ',' << data[i];
            f << '\n';
        };
        row("input", peakStages.input, 2);
        row("predelay", peakStages.predelay, 2);
        float pair[] = {peakStages.granL, peakStages.granR}; row("granular", pair, 2);
        pair[0] = peakStages.diffL; pair[1] = peakStages.diffR; row("diffuser", pair, 2);
        row("injection", peakStages.injection, CGV_FDN_ORDER);
        row("raw_read", peakStages.rawRead, CGV_FDN_ORDER);
        row("filtered_read", peakStages.filteredRead, CGV_FDN_ORDER);
        row("matrix", peakStages.matrix, CGV_FDN_ORDER);
        row("before_saturation", peakStages.beforeSaturation, CGV_FDN_ORDER);
        row("after_saturation", peakStages.afterSaturation, CGV_FDN_ORDER);
        row("after_spectral_guard", peakStages.afterGuard, CGV_FDN_ORDER);
        row("loop_write", peakStages.loopWrite, CGV_FDN_ORDER);
        row("delays", peakStages.delays, CGV_FDN_ORDER);
        pair[0] = peakStages.tailL; pair[1] = peakStages.tailR; row("decoded", pair, 2);
        row("final", peakStages.final, 2);
        row("max_raw_read", maxima.rawRead, CGV_FDN_ORDER);
        row("max_matrix", maxima.matrix, CGV_FDN_ORDER);
        row("max_after_saturation", maxima.afterSaturation, CGV_FDN_ORDER);
        row("max_loop_write", maxima.loopWrite, CGV_FDN_ORDER);
        row("max_final", maxima.final, 2);
        // Original failing sample, even if the corrected global peak moves.
        f << "reference_frame,623509\n";
        row("reference_raw_read", referenceStages.rawRead, CGV_FDN_ORDER);
        row("reference_filtered_read", referenceStages.filteredRead, CGV_FDN_ORDER);
        row("reference_matrix", referenceStages.matrix, CGV_FDN_ORDER);
        row("reference_before_saturation", referenceStages.beforeSaturation, CGV_FDN_ORDER);
        row("reference_after_saturation", referenceStages.afterSaturation, CGV_FDN_ORDER);
        row("reference_after_guard", referenceStages.afterGuard, CGV_FDN_ORDER);
        row("reference_final", referenceStages.final, 2);
        f << "max_loop_energy," << maxima.loopEnergy << '\n';
        std::cout << "peak_frame=" << peakFrame << " time=" << double(peakFrame) / sr
                  << " peak=" << peak << " stages=" << path << '\n';
    }
};
