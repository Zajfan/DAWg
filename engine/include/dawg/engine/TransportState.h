#pragma once

#include <cstdint>

namespace dawg::engine {

struct TransportState
{
    bool playing {};
    std::int64_t samplePosition {};
};

} // namespace dawg::engine
