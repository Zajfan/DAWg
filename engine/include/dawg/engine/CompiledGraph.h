#pragma once

#include <dawg/engine/LoopRegion.h>
#include <dawg/engine/PlaybackRingBuffer.h>

#include <cstdint>
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
};

struct CompiledGraph
{
    std::uint64_t generation {};
    LoopRegion loop {};
    std::vector<CompiledTrack> tracks;
    std::vector<CompiledClip> clips;

    // Intrusive link used exclusively by GraphHandoff's lock-free retired stack
    // so retiring an active graph on the audio thread never allocates or frees.
    mutable const CompiledGraph* retiredNext {};
};

} // namespace dawg::engine
