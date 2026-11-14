#pragma once

#include <cstdint>

namespace dawg::engine {

struct LoopRegion
{
    bool enabled {};
    std::uint64_t startSample {};
    std::uint64_t endSample {};

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return enabled && startSample < endSample;
    }

    [[nodiscard]] constexpr std::uint64_t lengthSamples() const noexcept
    {
        return startSample < endSample ? (endSample - startSample) : 0;
    }
};

} // namespace dawg::engine
