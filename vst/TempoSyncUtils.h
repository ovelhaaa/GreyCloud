#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace TempoSyncUtils {
    // Single tempo-sync contract: APVTS order, conversion and tests share it.
    // 60..240 BPM through 2/1 is supported. At 60 BPM, 2/1 is eight
    // quarter notes, so the longest request is 8 seconds.
    inline constexpr float kMinimumSupportedBpm = 60.0f;
    inline constexpr float kMaximumSupportedBpm = 240.0f;
    inline constexpr float kFallbackBpm = 120.0f;
    inline constexpr std::array<const char*, 13> kDivisionNames {{ "1/32", "1/16", "1/16T", "1/16D", "1/8", "1/8T", "1/8D", "1/4", "1/4T", "1/4D", "1/2", "1/1", "2/1" }};
    inline constexpr std::array<float, 13> kDivisionMultipliers {{ 0.125f, 0.25f, 1.0f / 6.0f, 0.375f, 0.5f, 1.0f / 3.0f, 0.75f, 1.0f, 2.0f / 3.0f, 1.5f, 2.0f, 4.0f, 8.0f }};
    inline int clampDivisionIndex(int index) { return index < 0 ? 0 : (index >= static_cast<int>(kDivisionMultipliers.size()) ? static_cast<int>(kDivisionMultipliers.size()) - 1 : index); }
    inline float sanitizeBpm(float bpm) {
        // No valid host tempo is materially different from no playhead: use the
        // documented musical fallback rather than interpreting 0 as 60 BPM.
        if (!std::isfinite(bpm) || bpm <= 0.0f) return kFallbackBpm;
        return std::fmax(kMinimumSupportedBpm, std::fmin(kMaximumSupportedBpm, bpm));
    }
    inline float getDivisionMultiplier(int index) {
        return kDivisionMultipliers[static_cast<size_t>(clampDivisionIndex(index))];
    }

    inline float getMsFromBpm(float bpm, int divisionIndex) {
        float quarterNoteMs = 60000.0f / sanitizeBpm(bpm);
        return quarterNoteMs * getDivisionMultiplier(divisionIndex);
    }
}
