#pragma once

#include <dawg/engine/dsp/DrumPadSamplerNode.h>
#include <dawg/engine/dsp/TransientSlicerEngine.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

struct SlicedMidiNote
{
    std::uint8_t pitch {36};
    double startBeat {0.0};
    double durationBeats {0.25};
    std::uint8_t velocity {100};
};

struct SlicedWarpMarker
{
    std::size_t sourceSample {0};
    double beatPosition {0.0};
    bool pinned {true};
};

struct DrumSliceResult
{
    std::size_t sliceCount {0};
    std::vector<std::size_t> sliceStartSamples;
    std::vector<double> sliceBeatPositions;
    std::vector<SlicedMidiNote> generatedNotes;
    double totalDurationBeats {4.0};
};

class DrumSliceEngine final
{
public:
    DrumSliceEngine() = delete;

    /// Slices an audio loop into individual drum hit slices, loads them into the
    /// DrumPadSamplerNode (pads 0..15), and generates matching MIDI note events triggering
    /// notes C1 (36), C#1 (37), D1 (38)... at the precise timestamps of each slice.
    [[nodiscard]] static DrumSliceResult sliceLoopToDrumKit(
        const float* audioL,
        const float* audioR,
        std::size_t sampleCount,
        double sampleRate,
        double bpm,
        DrumPadSamplerNode* drumSampler = nullptr,
        std::size_t maxSlices = 16);

    /// Adds a warp marker at the specified sample position
    static std::size_t addWarpMarker(
        std::vector<SlicedWarpMarker>& markers,
        std::size_t samplePosition,
        double bpm,
        double sampleRate);

    /// Removes a warp marker by index
    static bool removeWarpMarker(std::vector<SlicedWarpMarker>& markers, std::size_t index);

    /// Auto-quantizes warp markers to the nearest musical grid division
    /// (e.g. 0.25 = 16th note, 0.5 = 8th note, 1.0 = Quarter note) with configurable strength (0.0 .. 1.0).
    static void quantizeWarpMarkers(
        std::vector<SlicedWarpMarker>& markers,
        double gridDivision = 0.25,
        float strength = 1.0f);
};

} // namespace dawg::engine::dsp
