#pragma once

#include <cstdint>

namespace dawg::engine {

struct TransportState
{
    bool playing {};
    std::int64_t samplePosition {};
    std::uint64_t underrunCount {};
};

} // namespace dawg::engine
