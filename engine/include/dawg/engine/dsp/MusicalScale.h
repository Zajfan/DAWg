#pragma once

#include <cstdint>

namespace dawg::engine::dsp {

enum class MusicalScale : std::uint8_t
{
    chromatic,
    major,
    minor,
    harmonicMinor,
    dorian,
    mixolydian,
    pentatonicMinor,
    pentatonicMajor
};

} // namespace dawg::engine::dsp
