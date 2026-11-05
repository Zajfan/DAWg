#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

struct MidiEvent
{
    std::size_t frameOffset {};
    std::array<std::byte, 3> data {};
    std::uint8_t size {};
};

struct MidiBufferView
{
    const MidiEvent* events {};
    std::size_t eventCount {};
};

} // namespace dawg::engine
