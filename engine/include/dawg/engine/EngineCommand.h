#pragma once

#include <cstdint>

namespace dawg::engine {

enum class EngineCommandType { play, stop, seekSamples };

struct EngineCommand
{
    EngineCommandType type;
    std::int64_t samplePosition;
};

} // namespace dawg::engine
