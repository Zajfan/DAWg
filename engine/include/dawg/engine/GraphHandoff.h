#pragma once

#include <dawg/engine/CompiledGraph.h>

#include <atomic>
#include <cstddef>
#include <memory>

namespace dawg::engine {

// Wait-free SPSC pointer handoff with garbage-carrying retirement.
// - Control thread calls publish() and reclaimRetired() (allocates/deletes).
// - Audio thread calls adopt() and active() (zero allocations, zero deletes).
class GraphHandoff final
{
public:
    GraphHandoff() noexcept = default;
    ~GraphHandoff() noexcept;

    GraphHandoff(const GraphHandoff&) = delete;
    GraphHandoff& operator=(const GraphHandoff&) = delete;

    // Control-thread producer: reclaims any retired graphs, then publishes
    // the new graph, immediately deleting any unconsumed pending graph.
    void publish(std::unique_ptr<const CompiledGraph> graph) noexcept;

    // Control-thread reclamation of graphs retired by the audio thread.
    // Returns the number of retired graphs destroyed.
    std::size_t reclaimRetired() noexcept;

    // Audio-thread consumer: adopts the latest published graph (if any),
    // retiring the previous active graph to the lock-free retired stack.
    [[nodiscard]] const CompiledGraph* adopt() noexcept;

    [[nodiscard]] const CompiledGraph* active() const noexcept;

private:
    void pushRetired(const CompiledGraph* graph) noexcept;

    std::atomic<const CompiledGraph*> m_pending {nullptr};
    std::atomic<const CompiledGraph*> m_retiredHead {nullptr};
    const CompiledGraph* m_active {nullptr};
};

} // namespace dawg::engine
