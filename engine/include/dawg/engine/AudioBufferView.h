#pragma once

#include <cstddef>

namespace dawg::engine {

// Non-owning planar output. The caller supplies writable frameCount samples
// for each non-null channel and keeps that storage alive throughout process().
struct AudioBufferView
{
    float* const* channels {};
    std::size_t channelCount {};
    std::size_t frameCount {};
};

} // namespace dawg::engine
