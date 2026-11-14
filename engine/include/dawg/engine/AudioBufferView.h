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

struct AudioInputBufferView
{
    const float* const* channels {};
    std::size_t channelCount {};
    std::size_t frameCount {};

    constexpr AudioInputBufferView() noexcept = default;
    constexpr AudioInputBufferView(const float* const* ch, const std::size_t count,
                                   const std::size_t frames) noexcept
        : channels(ch), channelCount(count), frameCount(frames)
    {
    }
    constexpr AudioInputBufferView(const AudioBufferView& view) noexcept
        : channels(view.channels), channelCount(view.channelCount), frameCount(view.frameCount)
    {
    }
};

} // namespace dawg::engine
