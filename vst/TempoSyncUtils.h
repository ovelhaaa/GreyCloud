#pragma once
#include <cmath>

namespace TempoSyncUtils {
    // divisions: "1/32", "1/16", "1/16T", "1/16D", "1/8", "1/8T", "1/8D", "1/4", "1/4T", "1/4D", "1/2", "1/1", "2/1"
    inline float getDivisionMultiplier(int index) {
        switch (index) {
            case 0: return 0.125f;             // 1/32
            case 1: return 0.25f;              // 1/16
            case 2: return 0.25f * 0.6666667f; // 1/16T
            case 3: return 0.25f * 1.5f;       // 1/16D
            case 4: return 0.5f;               // 1/8
            case 5: return 0.5f * 0.6666667f;  // 1/8T
            case 6: return 0.5f * 1.5f;        // 1/8D
            case 7: return 1.0f;               // 1/4
            case 8: return 1.0f * 0.6666667f;  // 1/4T
            case 9: return 1.0f * 1.5f;        // 1/4D
            case 10: return 2.0f;              // 1/2
            case 11: return 4.0f;              // 1/1
            case 12: return 8.0f;              // 2/1
            default: return 1.0f;              // fallback
        }
    }

    inline float getMsFromBpm(float bpm, int divisionIndex) {
        if (bpm <= 0.0f) bpm = 120.0f;
        float quarterNoteMs = 60000.0f / bpm;
        return quarterNoteMs * getDivisionMultiplier(divisionIndex);
    }
}
