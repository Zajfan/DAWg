#include <dawg/engine/DspPerformanceProfiler.h>

#include <algorithm>

namespace dawg::engine {

DspPerformanceProfiler::DspPerformanceProfiler() noexcept
{
    reset();
}

void DspPerformanceProfiler::reset() noexcept
{
    m_currentCpuPercent.store(0.0f, std::memory_order_relaxed);
    m_peakCpuPercent.store(0.0f, std::memory_order_relaxed);
    m_blockBudgetUs.store(2666.7f, std::memory_order_relaxed);
    m_lastBlockElapsedUs.store(0.0f, std::memory_order_relaxed);
    m_totalBlocksProcessed.store(0, std::memory_order_relaxed);
    m_xrunCount.store(0, std::memory_order_relaxed);
    m_isOverBudget.store(false, std::memory_order_relaxed);

    for (std::size_t i = 0; i < maxTracks; ++i)
    {
        m_pubTrackIds[i].store(0, std::memory_order_relaxed);
        m_pubTrackUs[i].store(0.0f, std::memory_order_relaxed);
    }
    m_workingTrackCount = 0;
}

void DspPerformanceProfiler::startBlock(const std::size_t numSamples, const double sampleRate) noexcept
{
    m_blockStartTime = std::chrono::steady_clock::now();
    if (sampleRate > 1000.0 && numSamples > 0)
    {
        m_currentBudgetUs = static_cast<float>((static_cast<double>(numSamples) / sampleRate) * 1000000.0);
    }
    else
    {
        m_currentBudgetUs = 2666.7f;
    }
    m_blockBudgetUs.store(m_currentBudgetUs, std::memory_order_relaxed);
    m_workingTrackCount = 0;
}

void DspPerformanceProfiler::endBlock() noexcept
{
    const auto blockEndTime = std::chrono::steady_clock::now();
    const float elapsedUs = std::chrono::duration<float, std::micro>(blockEndTime - m_blockStartTime).count();

    m_lastBlockElapsedUs.store(elapsedUs, std::memory_order_relaxed);
    m_totalBlocksProcessed.fetch_add(1, std::memory_order_relaxed);

    const float budget = std::max(10.0f, m_currentBudgetUs);
    const float rawUsage = (elapsedUs / budget) * 100.0f;

    // Exponential moving average filter for stable meter reading
    const float prevUsage = m_currentCpuPercent.load(std::memory_order_relaxed);
    const float smoothedUsage = (prevUsage * 0.85f) + (rawUsage * 0.15f);
    m_currentCpuPercent.store(smoothedUsage, std::memory_order_relaxed);

    // Peak hold with slow decay
    float currentPeak = m_peakCpuPercent.load(std::memory_order_relaxed);
    if (rawUsage > currentPeak)
    {
        m_peakCpuPercent.store(rawUsage, std::memory_order_relaxed);
    }
    else
    {
        m_peakCpuPercent.store(std::max(0.0f, currentPeak * 0.995f), std::memory_order_relaxed);
    }

    if (elapsedUs > budget)
    {
        m_isOverBudget.store(true, std::memory_order_relaxed);
        m_xrunCount.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        m_isOverBudget.store(false, std::memory_order_relaxed);
    }

    // Publish working track loads
    const std::size_t pubCount = std::min(m_workingTrackCount, maxTracks);
    for (std::size_t i = 0; i < pubCount; ++i)
    {
        m_pubTrackIds[i].store(m_workingTracks[i].trackId, std::memory_order_relaxed);
        m_pubTrackUs[i].store(m_workingTracks[i].executionUs, std::memory_order_relaxed);
    }
    for (std::size_t i = pubCount; i < maxTracks; ++i)
    {
        m_pubTrackIds[i].store(0, std::memory_order_relaxed);
    }
}

void DspPerformanceProfiler::recordTrackLoad(const std::uint64_t trackId, const float executionUs) noexcept
{
    if (m_workingTrackCount < maxTracks)
    {
        m_workingTracks[m_workingTrackCount].trackId = trackId;
        m_workingTracks[m_workingTrackCount].executionUs = executionUs;
        m_workingTrackCount++;
    }
}

void DspPerformanceProfiler::recordSlotLoad(const std::uint64_t trackId, const std::size_t slotIdx, const float executionUs) noexcept
{
    for (std::size_t i = 0; i < m_workingTrackCount; ++i)
    {
        if (m_workingTracks[i].trackId == trackId && slotIdx < 8)
        {
            m_workingTracks[i].slotUs[slotIdx] = executionUs;
            return;
        }
    }
}

DspPerformanceSnapshot DspPerformanceProfiler::readSnapshot() const noexcept
{
    DspPerformanceSnapshot s;
    s.currentCpuPercent = m_currentCpuPercent.load(std::memory_order_relaxed);
    s.peakCpuPercent = m_peakCpuPercent.load(std::memory_order_relaxed);
    s.blockBudgetUs = m_blockBudgetUs.load(std::memory_order_relaxed);
    s.lastBlockElapsedUs = m_lastBlockElapsedUs.load(std::memory_order_relaxed);
    s.totalBlocksProcessed = m_totalBlocksProcessed.load(std::memory_order_relaxed);
    s.xrunCount = m_xrunCount.load(std::memory_order_relaxed);
    s.isOverBudget = m_isOverBudget.load(std::memory_order_relaxed);

    const float budget = std::max(10.0f, s.blockBudgetUs);
    std::size_t count = 0;
    for (std::size_t i = 0; i < maxTracks; ++i)
    {
        const auto trkId = m_pubTrackIds[i].load(std::memory_order_relaxed);
        if (trkId == 0) continue;

        s.tracks[count].trackId = trkId;
        const float us = m_pubTrackUs[i].load(std::memory_order_relaxed);
        s.tracks[count].executionUs = us;
        s.tracks[count].cpuPercent = (us / budget) * 100.0f;
        count++;
    }
    s.activeTrackCount = count;
    return s;
}

} // namespace dawg::engine
