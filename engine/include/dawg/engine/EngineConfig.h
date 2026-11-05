#pragma once

#include <cstddef>

namespace dawg::engine {

struct EngineConfig
{
    double sampleRate {};
    std::size_t maximumBlockSize {};
    std::size_t outputChannels {};
};

} // namespace dawg::engine
