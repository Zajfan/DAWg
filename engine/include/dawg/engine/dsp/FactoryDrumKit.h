#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine::dsp {

enum class DrumHitType : std::uint8_t
{
    kick808,
    kick909,
    snare808,
    clap808,
    hihatClosed,
    hihatOpen,
    rimshot,
    cowbell808
};

class FactoryDrumKit
{
public:
    static std::vector<float> generateHit(DrumHitType type, double sampleRate = 48000.0);
    static const char* hitName(DrumHitType type) noexcept;
};

} // namespace dawg::engine::dsp
