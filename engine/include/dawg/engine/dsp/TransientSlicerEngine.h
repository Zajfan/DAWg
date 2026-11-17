#pragma once

#include <dawg/engine/dsp/AudioTranscriber.h>
#include <dawg/engine/dsp/DrumPadSamplerNode.h>
#include <dawg/engine/dsp/MultiSampleInstrumentNode.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

struct SlicedBeatSegment
{
    std::size_t startSample {0};
    std::size_t endSample {0};
    float peakAmplitude {0.0f};
    bool reverse {false};
    bool granularScrub {false};
    std::string label {"Slice"};
};

struct ReusableGrooveTemplate
{
    std::string name {"Default 16th"};
    float swingPercentage {50.0f};      // 50% = straight, 54%-71% = MPC, >71% = heavy swing
    float velocityVariance {0.0f};      // Dynamic humanization variance (0.0 to 1.0)
    float microTimingDeviationMs {0.0f};
    std::vector<float> stepOffsets;     // 16-step grid micro-offsets (-0.5 to +0.5 fraction of step)
};

// High-Performance Transient Beat Slicer & Dynamic Sampler Engine
// Detects transients with zero-crossing alignment and maps slices directly to
// Drum Pad Sampler pads or Multi-Sample SFZ chromatic key-zones, with reverse,
// granular scrub, and groove template extraction.
class TransientSlicerEngine
{
public:
    TransientSlicerEngine() = default;
    ~TransientSlicerEngine() = default;

    // Detect transient slices with zero-crossing adjustment
    [[nodiscard]] static std::vector<SlicedBeatSegment> detectSlices(
        const float* audioData,
        std::size_t sampleCount,
        double sampleRate,
        std::size_t maxSlices = 16,
        float sensitivity = 1.3f);

    // Slice stereo audio directly into a DrumPadSamplerNode (Pad 0..15)
    static std::size_t sliceToDrumPadSampler(
        const float* audioL,
        const float* audioR,
        std::size_t sampleCount,
        double sampleRate,
        DrumPadSamplerNode& drumPadSampler,
        bool reverse = false,
        bool granularScrub = false);

    // Slice stereo audio directly into a MultiSampleInstrumentNode (mapped across chromatic keys)
    static std::size_t sliceToMultiSampleInstrument(
        const float* audioL,
        const float* audioR,
        std::size_t sampleCount,
        double sampleRate,
        MultiSampleInstrumentNode& instrument,
        std::uint8_t startMidiNote = 36);

    // Extract rhythmic groove template (swing %, velocity spread, and 16-step micro-offsets)
    [[nodiscard]] static ReusableGrooveTemplate extractGrooveTemplate(
        const float* audioData,
        std::size_t sampleCount,
        double sampleRate,
        double bpm = 120.0);

    // Factory Groove Templates
    [[nodiscard]] static ReusableGrooveTemplate getFactoryMpc60Swing(int percent); // 54 to 71
    [[nodiscard]] static ReusableGrooveTemplate getFactorySp1200Swing(int swingIndex); // 1 to 6

private:
    [[nodiscard]] static std::size_t findNearestZeroCrossing(
        const float* audio,
        std::size_t sampleCount,
        std::size_t targetIndex,
        std::size_t searchRadius = 128) noexcept;
};

} // namespace dawg::engine::dsp
