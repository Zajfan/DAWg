#pragma once

#include <dawg/engine/PlaybackDescription.h>

#include <cstdint>

namespace dawg::engine {

// Fixed-size value built on the message thread; no editable project references.
struct RenderSnapshot
{
    std::uint64_t generation {};
    std::uint32_t trackCount {};
    PlaybackDescription playback {};
};

} // namespace dawg::engine
