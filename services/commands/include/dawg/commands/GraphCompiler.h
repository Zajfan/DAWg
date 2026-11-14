#pragma once

#include <dawg/engine/CompiledGraph.h>
#include <dawg/project/ProjectSnapshot.h>

#include <cstdint>
#include <memory>
#include <span>

namespace dawg::commands {

struct ClipBufferBinding
{
    project::ClipId clipId {};
    engine::PlaybackRingBuffer* buffer {};
};

class GraphCompiler final
{
public:
    [[nodiscard]] static std::unique_ptr<const engine::CompiledGraph> compile(
        const project::ProjectSnapshot& snapshot,
        std::uint64_t generation,
        std::span<const ClipBufferBinding> bindings);
};

} // namespace dawg::commands
