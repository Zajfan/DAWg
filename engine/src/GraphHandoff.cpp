#include <dawg/engine/GraphHandoff.h>

namespace dawg::engine {

GraphHandoff::~GraphHandoff() noexcept
{
    static_cast<void>(reclaimRetired());
    delete m_pending.exchange(nullptr, std::memory_order_acq_rel);
    delete m_active;
    m_active = nullptr;
}

void GraphHandoff::publish(std::unique_ptr<const CompiledGraph> graph) noexcept
{
    static_cast<void>(reclaimRetired());
    const CompiledGraph* incoming = graph.release();
    const CompiledGraph* overwritten = m_pending.exchange(incoming, std::memory_order_acq_rel);
    delete overwritten;
}

std::size_t GraphHandoff::reclaimRetired() noexcept
{
    const CompiledGraph* head = m_retiredHead.exchange(nullptr, std::memory_order_acquire);
    std::size_t count = 0;
    while (head != nullptr)
    {
        const CompiledGraph* next = head->retiredNext;
        delete head;
        head = next;
        ++count;
    }
    return count;
}

const CompiledGraph* GraphHandoff::adopt() noexcept
{
    const CompiledGraph* incoming = m_pending.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming != nullptr)
    {
        if (m_active != nullptr)
            pushRetired(m_active);
        m_active = incoming;
    }
    return m_active;
}

const CompiledGraph* GraphHandoff::active() const noexcept
{
    return m_active;
}

void GraphHandoff::pushRetired(const CompiledGraph* graph) noexcept
{
    const CompiledGraph* expected = m_retiredHead.load(std::memory_order_relaxed);
    do
    {
        graph->retiredNext = expected;
    } while (! m_retiredHead.compare_exchange_weak(
        expected, graph, std::memory_order_release, std::memory_order_relaxed));
}

} // namespace dawg::engine
