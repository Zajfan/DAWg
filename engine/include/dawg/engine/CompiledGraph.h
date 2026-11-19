#pragma once

#include <dawg/engine/LoopRegion.h>
#include <dawg/engine/PlaybackRingBuffer.h>

#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

struct CompiledClip
{
    std::uint64_t clipId {};
    std::uint64_t assetId {};
    PlaybackRingBuffer* buffer {};
    std::uint64_t startSample {};
    std::uint64_t lengthSamples {};
    std::uint64_t sourceOffset {};
    std::uint16_t channelCount {};
    float gainLinear {1.0f};
    std::uint32_t fadeInSamples {};
    std::uint32_t fadeOutSamples {};
};

struct CompiledInsert
{
    std::uint32_t slotIndex {};
    std::uint8_t dspType {};
    bool bypassed {false};
    std::string name {};
    std::string pluginId {};
    std::vector<float> parameters {};
};

struct CompiledTrack
{
    std::uint64_t trackId {};
    bool muted {};
    bool solo {};
    bool phaseInvert {};
    float gainLinear {1.0f};
    float pan {0.0f};
    std::uint32_t clipOffset {};
    std::uint32_t clipCount {};
    std::uint8_t trackKind {0};
    std::vector<CompiledInsert> inserts {};
};

struct CompiledMidiNote
{
    std::uint64_t noteId {};
    std::uint64_t startSample {};
    std::uint64_t lengthSamples {};
    std::uint8_t pitch {};
    std::uint8_t velocity {};
};

struct CompiledMidiClip
{
    std::uint64_t clipId {};
    std::uint64_t trackId {};
    std::uint64_t startSample {};
    std::uint64_t lengthSamples {};
    std::vector<CompiledMidiNote> notes {};
};

struct CompiledGraph
{
    std::uint64_t generation {};
    LoopRegion loop {};
    double bpm {120.0};
    std::uint8_t timeSigNumerator {4};
    std::uint8_t timeSigDenominator {4};
    bool metronomeEnabled {false};
    std::vector<CompiledTrack> tracks;
    std::vector<CompiledClip> clips;
    std::vector<CompiledMidiClip> midiClips {};

    // Intrusive link used exclusively by GraphHandoff's lock-free retired stack
    // so retiring an active graph on the audio thread never allocates or frees.
    mutable const CompiledGraph* retiredNext {};
};

} // namespace dawg::engine
