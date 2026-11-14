#pragma once

#include <cstdint>

namespace dawg::engine {

class PlaybackRingBuffer;

struct PlaybackDescription
{
    PlaybackRingBuffer* buffer {};
    std::uint64_t clipStartSample {};
    std::uint64_t clipLengthSamples {};
    std::uint64_t sourceOffset {};
    std::uint16_t channelCount {};
};

} // namespace dawg::engine
