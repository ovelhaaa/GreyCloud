#include "cloud_grey_verb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct PresetSpec {
    const char* name;
    CloudGreyVerb::Preset preset;
};

constexpr std::array<PresetSpec, 9> kPresets = {{
    {"SmallCloudRoom", CloudGreyVerb::Preset::SmallCloudRoom},
    {"BassAmbientWash", CloudGreyVerb::Preset::BassAmbientWash},
    {"FrozenOrganPad", CloudGreyVerb::Preset::FrozenOrganPad},
    {"GreyholeDelayVerb", CloudGreyVerb::Preset::GreyholeDelayVerb},
    {"DarkLongCloud", CloudGreyVerb::Preset::DarkLongCloud},
    {"GlitchSmear", CloudGreyVerb::Preset::GlitchSmear},
    {"AlwaysOnSubtle", CloudGreyVerb::Preset::AlwaysOnSubtle},
    {"BrightCloud", CloudGreyVerb::Preset::BrightCloud},
    {"ShimmerCloud", CloudGreyVerb::Preset::ShimmerCloud},
}};

struct Options {
    fs::path outputDirectory = "fdn-ir-output";
    double seconds = 8.0;
    double memorySeconds = 3.0;
    float sampleRate = 48000.0f;
    std::string presetName = "all";
    bool writeWav = true;
};

struct DecayEstimate {
    double rt60Seconds = std::numeric_limits<double>::quiet_NaN();
    double rSquared = std::numeric_limits<double>::quiet_NaN();
};

struct Metrics {
    std::string presetName;
    int fdnOrder = 0;
    int sampleRate = 0;
    double renderSeconds = 0.0;
    size_t memoryFloats = 0;
    size_t delayCapacityFrames = 0;
    double peak = 0.0;
    double rms = 0.0;
    double onsetMs = 0.0;
    double rt60 = std::numeric_limits<double>::quiet_NaN();
    double rt60FitR2 = std::numeric_limits<double>::quiet_NaN();
    double rt60Low = std::numeric_limits<double>::quiet_NaN();
    double rt60Mid = std::numeric_limits<double>::quiet_NaN();
    double rt60High = std::numeric_limits<double>::quiet_NaN();
    double tailLevelDb = -120.0;
    bool rt60Truncated = false;
    double stereoCorrelation = 0.0;
    double sideEnergyPercent = 0.0;
    double c80Db = 0.0;
    double densityPercent = 0.0;
    double energyL = 0.0;
    double energyR = 0.0;
    double minSafetyGain = 1.0;
    double maxLoopEnergy = 0.0;
};

class OnePoleLowpass {
public:
    OnePoleLowpass(double cutoff, double sampleRate) {
        alpha_ = 1.0 - std::exp(-2.0 * kPi * cutoff / sampleRate);
    }

    double process(double input) {
        state_ += alpha_ * (input - state_);
        return state_;
    }

private:
    double alpha_ = 1.0;
    double state_ = 0.0;
};

void printUsage(const char* executable) {
    std::cout
        << "Usage: " << executable << " [options]\n"
        << "  --output-dir PATH       Output directory (default: fdn-ir-output)\n"
        << "  --seconds N             Render duration (default: 8)\n"
        << "  --memory-seconds N      External buffer in mono-equivalent seconds (default: 3)\n"
        << "  --sample-rate N         Sample rate (default: 48000)\n"
        << "  --preset NAME|all       Render one factory preset or all presets\n"
        << "  --no-wav                Calculate metrics without writing WAV files\n"
        << "  --help                  Show this help\n";
}

Options parseOptions(int argc, char** argv) {
    Options options;

    auto requireValue = [&](int& index, const char* option) -> std::string {
        if (index + 1 >= argc)
            throw std::runtime_error(std::string("Missing value for ") + option);
        return argv[++index];
    };

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output-dir")
            options.outputDirectory = requireValue(i, "--output-dir");
        else if (arg == "--seconds")
            options.seconds = std::stod(requireValue(i, "--seconds"));
        else if (arg == "--memory-seconds")
            options.memorySeconds = std::stod(requireValue(i, "--memory-seconds"));
        else if (arg == "--sample-rate")
            options.sampleRate = std::stof(requireValue(i, "--sample-rate"));
        else if (arg == "--preset")
            options.presetName = requireValue(i, "--preset");
        else if (arg == "--no-wav")
            options.writeWav = false;
        else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
        else
            throw std::runtime_error("Unknown option: " + arg);
    }

    if (options.seconds <= 0.25)
        throw std::runtime_error("--seconds must be greater than 0.25");
    if (options.memorySeconds <= 0.5)
        throw std::runtime_error("--memory-seconds must be greater than 0.5");
    if (options.sampleRate < 8000.0f || options.sampleRate > 384000.0f)
        throw std::runtime_error("--sample-rate must be between 8000 and 384000");

    return options;
}

void writeU16(std::ostream& stream, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
    };
    stream.write(bytes, 2);
}

void writeU32(std::ostream& stream, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xffu),
        static_cast<char>((value >> 8u) & 0xffu),
        static_cast<char>((value >> 16u) & 0xffu),
        static_cast<char>((value >> 24u) & 0xffu),
    };
    stream.write(bytes, 4);
}

void writeFloat32(std::ostream& stream, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "Unexpected float size");
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(stream, bits);
}

void writeFloatWav(const fs::path& path,
                   const std::vector<float>& left,
                   const std::vector<float>& right,
                   uint32_t sampleRate) {
    if (left.size() != right.size())
        throw std::runtime_error("Channel lengths differ while writing WAV");

    const uint64_t dataBytes64 = left.size() * 2ull * sizeof(float);
    if (dataBytes64 > std::numeric_limits<uint32_t>::max() - 36u)
        throw std::runtime_error("WAV is too large for a RIFF32 container");

    const uint32_t dataBytes = static_cast<uint32_t>(dataBytes64);
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Unable to create WAV: " + path.string());

    stream.write("RIFF", 4);
    writeU32(stream, 36u + dataBytes);
    stream.write("WAVE", 4);
    stream.write("fmt ", 4);
    writeU32(stream, 16u);
    writeU16(stream, 3u); // WAVE_FORMAT_IEEE_FLOAT
    writeU16(stream, 2u);
    writeU32(stream, sampleRate);
    writeU32(stream, sampleRate * 2u * sizeof(float));
    writeU16(stream, 2u * sizeof(float));
    writeU16(stream, 32u);
    stream.write("data", 4);
    writeU32(stream, dataBytes);

    for (size_t i = 0; i < left.size(); ++i) {
        writeFloat32(stream, left[i]);
        writeFloat32(stream, right[i]);
    }
}

DecayEstimate estimateRt60(const std::vector<double>& frameEnergy, double sampleRate) {
    DecayEstimate estimate;
    if (frameEnergy.empty())
        return estimate;

    std::vector<double> schroeder(frameEnergy.size(), 0.0);
    double accumulated = 0.0;
    for (size_t i = frameEnergy.size(); i-- > 0;) {
        accumulated += std::max(0.0, frameEnergy[i]);
        schroeder[i] = accumulated;
    }

    const double reference = schroeder.front();
    if (reference <= 1.0e-24)
        return estimate;

    auto fitRange = [&](double upperDb, double lowerDb) -> DecayEstimate {
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        double sumYY = 0.0;
        size_t count = 0;

        for (size_t i = 0; i < schroeder.size(); ++i) {
            const double ratio = schroeder[i] / reference;
            if (ratio <= 1.0e-30)
                continue;
            const double db = 10.0 * std::log10(ratio);
            if (db > upperDb || db < lowerDb)
                continue;

            const double time = static_cast<double>(i) / sampleRate;
            sumX += time;
            sumY += db;
            sumXX += time * time;
            sumXY += time * db;
            sumYY += db * db;
            ++count;
        }

        DecayEstimate result;
        if (count < 128)
            return result;

        const double n = static_cast<double>(count);
        const double denominator = n * sumXX - sumX * sumX;
        if (std::abs(denominator) < 1.0e-20)
            return result;

        const double slope = (n * sumXY - sumX * sumY) / denominator;
        if (slope >= -1.0e-6)
            return result;

        const double correlationDenominator =
            (n * sumXX - sumX * sumX) * (n * sumYY - sumY * sumY);
        const double correlationNumerator = n * sumXY - sumX * sumY;

        result.rt60Seconds = -60.0 / slope;
        if (correlationDenominator > 1.0e-24)
            result.rSquared = (correlationNumerator * correlationNumerator)
                            / correlationDenominator;
        return result;
    };

    estimate = fitRange(-5.0, -35.0); // T30
    if (!std::isfinite(estimate.rt60Seconds))
        estimate = fitRange(-5.0, -25.0); // T20
    if (!std::isfinite(estimate.rt60Seconds))
        estimate = fitRange(-5.0, -15.0); // T10 fallback
    return estimate;
}

Metrics analyze(const std::string& presetName,
                const std::vector<float>& left,
                const std::vector<float>& right,
                float sampleRate,
                size_t memoryFloats,
                size_t delayCapacityFrames,
                double minSafetyGain,
                double maxLoopEnergy) {
    Metrics metrics;
    metrics.presetName = presetName;
    metrics.fdnOrder = static_cast<int>(CloudGreyVerb::kFdnOrder);
    metrics.sampleRate = static_cast<int>(std::lround(sampleRate));
    metrics.renderSeconds = static_cast<double>(left.size()) / sampleRate;
    metrics.memoryFloats = memoryFloats;
    metrics.delayCapacityFrames = delayCapacityFrames;
    metrics.minSafetyGain = minSafetyGain;
    metrics.maxLoopEnergy = maxLoopEnergy;

    std::vector<double> fullEnergy(left.size(), 0.0);
    std::vector<double> lowEnergy(left.size(), 0.0);
    std::vector<double> midEnergy(left.size(), 0.0);
    std::vector<double> highEnergy(left.size(), 0.0);

    OnePoleLowpass lowL(250.0, sampleRate);
    OnePoleLowpass lowR(250.0, sampleRate);
    OnePoleLowpass upperL(2000.0, sampleRate);
    OnePoleLowpass upperR(2000.0, sampleRate);

    double totalSquared = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        const double l = left[i];
        const double r = right[i];
        metrics.peak = std::max(metrics.peak, std::max(std::abs(l), std::abs(r)));
        metrics.energyL += l * l;
        metrics.energyR += r * r;
        fullEnergy[i] = l * l + r * r;
        totalSquared += fullEnergy[i];

        const double lLow = lowL.process(l);
        const double rLow = lowR.process(r);
        const double lUpper = upperL.process(l);
        const double rUpper = upperR.process(r);
        const double lMid = lUpper - lLow;
        const double rMid = rUpper - rLow;
        const double lHigh = l - lUpper;
        const double rHigh = r - rUpper;

        lowEnergy[i] = lLow * lLow + rLow * rLow;
        midEnergy[i] = lMid * lMid + rMid * rMid;
        highEnergy[i] = lHigh * lHigh + rHigh * rHigh;
    }

    metrics.rms = left.empty()
        ? 0.0
        : std::sqrt(totalSquared / (2.0 * static_cast<double>(left.size())));

    const double onsetThreshold = std::max(1.0e-9, metrics.peak * 1.0e-5);
    size_t onset = 0;
    while (onset < left.size()
           && std::max(std::abs(static_cast<double>(left[onset])),
                       std::abs(static_cast<double>(right[onset]))) < onsetThreshold)
        ++onset;
    if (onset >= left.size())
        onset = 0;
    metrics.onsetMs = 1000.0 * static_cast<double>(onset) / sampleRate;

    const DecayEstimate broadband = estimateRt60(fullEnergy, sampleRate);
    metrics.rt60 = broadband.rt60Seconds;
    metrics.rt60FitR2 = broadband.rSquared;
    metrics.rt60Low = estimateRt60(lowEnergy, sampleRate).rt60Seconds;
    metrics.rt60Mid = estimateRt60(midEnergy, sampleRate).rt60Seconds;
    metrics.rt60High = estimateRt60(highEnergy, sampleRate).rt60Seconds;

    const size_t tailWindow = std::max<size_t>(1, static_cast<size_t>(sampleRate * 0.25f));
    const size_t tailStart = left.size() > tailWindow ? left.size() - tailWindow : 0;
    double tailSquared = 0.0;
    for (size_t i = tailStart; i < left.size(); ++i)
        tailSquared += fullEnergy[i];
    const double tailRms = std::sqrt(tailSquared
        / std::max(1.0, 2.0 * static_cast<double>(left.size() - tailStart)));
    metrics.tailLevelDb = metrics.peak > 1.0e-12
        ? 20.0 * std::log10(std::max(1.0e-12, tailRms / metrics.peak))
        : -120.0;
    metrics.rt60Truncated = metrics.tailLevelDb > -40.0;

    const size_t correlationStart = onset;
    const size_t count = left.size() - correlationStart;
    double meanL = 0.0;
    double meanR = 0.0;
    if (count > 0) {
        for (size_t i = correlationStart; i < left.size(); ++i) {
            meanL += left[i];
            meanR += right[i];
        }
        meanL /= static_cast<double>(count);
        meanR /= static_cast<double>(count);
    }

    double covariance = 0.0;
    double varianceL = 0.0;
    double varianceR = 0.0;
    double midEnergyTotal = 0.0;
    double sideEnergyTotal = 0.0;
    for (size_t i = correlationStart; i < left.size(); ++i) {
        const double centeredL = left[i] - meanL;
        const double centeredR = right[i] - meanR;
        covariance += centeredL * centeredR;
        varianceL += centeredL * centeredL;
        varianceR += centeredR * centeredR;

        const double mid = 0.5 * (left[i] + right[i]);
        const double side = 0.5 * (left[i] - right[i]);
        midEnergyTotal += mid * mid;
        sideEnergyTotal += side * side;
    }
    const double correlationDenominator = std::sqrt(varianceL * varianceR);
    metrics.stereoCorrelation = correlationDenominator > 1.0e-24
        ? covariance / correlationDenominator
        : 0.0;
    const double stereoEnergy = midEnergyTotal + sideEnergyTotal;
    metrics.sideEnergyPercent = stereoEnergy > 1.0e-24
        ? 100.0 * sideEnergyTotal / stereoEnergy
        : 0.0;

    const size_t earlyEnd = std::min(left.size(), onset
        + static_cast<size_t>(sampleRate * 0.080f));
    double earlyEnergy = 0.0;
    double lateEnergy = 0.0;
    for (size_t i = onset; i < left.size(); ++i) {
        if (i < earlyEnd)
            earlyEnergy += fullEnergy[i];
        else
            lateEnergy += fullEnergy[i];
    }
    metrics.c80Db = 10.0 * std::log10((earlyEnergy + 1.0e-24)
                                      / (lateEnergy + 1.0e-24));

    const size_t densityStart = std::min(left.size(), onset
        + static_cast<size_t>(sampleRate * 0.050f));
    const size_t densityEnd = std::min(left.size(), onset
        + static_cast<size_t>(sampleRate * 0.500f));
    const double densityThreshold = std::max(1.0e-9, metrics.peak * 1.0e-4);
    size_t denseFrames = 0;
    for (size_t i = densityStart; i < densityEnd; ++i) {
        if (std::max(std::abs(static_cast<double>(left[i])),
                     std::abs(static_cast<double>(right[i]))) >= densityThreshold)
            ++denseFrames;
    }
    metrics.densityPercent = densityEnd > densityStart
        ? 100.0 * static_cast<double>(denseFrames)
            / static_cast<double>(densityEnd - densityStart)
        : 0.0;

    return metrics;
}

void writeCsvHeader(std::ostream& stream) {
    stream
        << "preset,fdn_order,sample_rate,render_seconds,memory_floats,"
        << "delay_capacity_frames,peak,rms,onset_ms,rt60_seconds,rt60_fit_r2,"
        << "rt60_low_seconds,rt60_mid_seconds,rt60_high_seconds,tail_level_db,"
        << "rt60_truncated,stereo_correlation,side_energy_percent,c80_db,"
        << "density_percent,energy_l,energy_r,min_safety_gain,max_loop_energy\n";
}

void writeNumber(std::ostream& stream, double value) {
    if (std::isfinite(value))
        stream << value;
    else
        stream << "nan";
}

void writeCsvRow(std::ostream& stream, const Metrics& metrics) {
    stream << metrics.presetName << ','
           << metrics.fdnOrder << ','
           << metrics.sampleRate << ','
           << metrics.renderSeconds << ','
           << metrics.memoryFloats << ','
           << metrics.delayCapacityFrames << ',';
    writeNumber(stream, metrics.peak); stream << ',';
    writeNumber(stream, metrics.rms); stream << ',';
    writeNumber(stream, metrics.onsetMs); stream << ',';
    writeNumber(stream, metrics.rt60); stream << ',';
    writeNumber(stream, metrics.rt60FitR2); stream << ',';
    writeNumber(stream, metrics.rt60Low); stream << ',';
    writeNumber(stream, metrics.rt60Mid); stream << ',';
    writeNumber(stream, metrics.rt60High); stream << ',';
    writeNumber(stream, metrics.tailLevelDb); stream << ',';
    stream << (metrics.rt60Truncated ? 1 : 0) << ',';
    writeNumber(stream, metrics.stereoCorrelation); stream << ',';
    writeNumber(stream, metrics.sideEnergyPercent); stream << ',';
    writeNumber(stream, metrics.c80Db); stream << ',';
    writeNumber(stream, metrics.densityPercent); stream << ',';
    writeNumber(stream, metrics.energyL); stream << ',';
    writeNumber(stream, metrics.energyR); stream << ',';
    writeNumber(stream, metrics.minSafetyGain); stream << ',';
    writeNumber(stream, metrics.maxLoopEnergy); stream << '\n';
}

bool shouldRender(const Options& options, const PresetSpec& preset) {
    return options.presetName == "all" || options.presetName == preset.name;
}

Metrics renderPreset(const Options& options,
                     const PresetSpec& preset,
                     size_t memoryFloats) {
    const size_t frames = static_cast<size_t>(std::llround(options.seconds
        * static_cast<double>(options.sampleRate)));
    std::vector<float> memory(memoryFloats, 0.0f);
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);

    CloudGreyVerb reverb;
    reverb.init(options.sampleRate, memory.data(), memory.size());
    if (reverb.getMainDelayFrames() == 0)
        throw std::runtime_error("DSP initialization failed for " + std::string(preset.name));

    CloudGreyVerb::Params params = CloudGreyVerb::getPreset(preset.preset);
    params.mix = 1.0f;
    params.freeze = 0.0f;
    params.hardFreeze = false;
    reverb.setParams(params);
    reverb.reset();

    double minSafetyGain = 1.0;
    double maxLoopEnergy = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        // Dual-mono normalized impulse: total input energy remains 1.0 while
        // Side energy and correlation measure width generated by the reverb.
        constexpr float kImpulseChannelGain = 0.70710678f;
        const float input = i == 0 ? kImpulseChannelGain : 0.0f;
        float outputL = 0.0f;
        float outputR = 0.0f;
        reverb.processSample(input, input, outputL, outputR);
        if (!std::isfinite(outputL) || !std::isfinite(outputR))
            throw std::runtime_error("Non-finite output in " + std::string(preset.name));
        left[i] = outputL;
        right[i] = outputR;
        minSafetyGain = std::min(minSafetyGain,
            static_cast<double>(reverb.getSafetyGain()));
        maxLoopEnergy = std::max(maxLoopEnergy,
            static_cast<double>(reverb.getLoopEnergy()));
    }

    if (options.writeWav) {
        const fs::path wavPath = options.outputDirectory
            / (std::string(preset.name) + ".wav");
        writeFloatWav(wavPath, left, right,
                      static_cast<uint32_t>(std::lround(options.sampleRate)));
    }

    return analyze(preset.name, left, right, options.sampleRate, memoryFloats,
                   reverb.getMainDelayFrames(), minSafetyGain, maxLoopEnergy);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);
        fs::create_directories(options.outputDirectory);

        const size_t memoryFloats = std::max<size_t>(24000,
            static_cast<size_t>(std::llround(options.memorySeconds
                * static_cast<double>(options.sampleRate))));

        bool foundPreset = false;
        std::ofstream csv(options.outputDirectory / "metrics.csv");
        if (!csv)
            throw std::runtime_error("Unable to create metrics.csv");
        csv << std::setprecision(10);
        writeCsvHeader(csv);

        std::cout << "FDN " << CloudGreyVerb::kFdnOrder << 'x'
                  << CloudGreyVerb::kFdnOrder << " IR analysis\n"
                  << "Sample rate: " << options.sampleRate << " Hz\n"
                  << "Render: " << options.seconds << " s\n"
                  << "External memory: " << memoryFloats << " floats\n";

        for (const PresetSpec& preset : kPresets) {
            if (!shouldRender(options, preset))
                continue;
            foundPreset = true;
            std::cout << "  Rendering " << preset.name << "..." << std::flush;
            const Metrics metrics = renderPreset(options, preset, memoryFloats);
            writeCsvRow(csv, metrics);
            std::cout << " RT60=";
            if (std::isfinite(metrics.rt60))
                std::cout << std::fixed << std::setprecision(3) << metrics.rt60 << 's';
            else
                std::cout << "n/a";
            std::cout << ", corr=" << std::fixed << std::setprecision(3)
                      << metrics.stereoCorrelation << '\n';
        }

        if (!foundPreset)
            throw std::runtime_error("Unknown preset: " + options.presetName);

        std::cout << "Metrics: " << (options.outputDirectory / "metrics.csv") << '\n';
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "fdn_ir_analyzer: " << error.what() << '\n';
        return 1;
    }
}
