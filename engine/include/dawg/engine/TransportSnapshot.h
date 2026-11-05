#pragma once

#include <cstdint>

namespace dawg::engine {

struct TransportSnapshot
{
    bool playing {};
    std::int64_t samplePosition {};
    double tempoBpm { 120.0 };
};

} // namespace dawg::engine
