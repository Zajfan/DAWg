#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

struct TrackLoadEntry
{
    std::uint64_t trackId {0};
    float cpuPercent {0.0f};
    float executionUs {0.0f};
    std::array<float, 8> slotExecutionUs {};
};

struct DspPerformanceSnapshot
{
    float currentCpuPercent {0.0f};
    float peakCpuPercent {0.0f};
    float blockBudgetUs {0.0f};
    float lastBlockElapsedUs {0.0f};
    std::uint64_t totalBlocksProcessed {0};
    std::uint64_t xrunCount {0};
    bool isOverBudget {false};
    std::size_t activeTrackCount {0};
    std::array<TrackLoadEntry, 64> tracks {};
};

class DspPerformanceProfiler final
{
public:
    static constexpr std::size_t maxTracks = 64;

    DspPerformanceProfiler() noexcept;
    ~DspPerformanceProfiler() = default;

    // Called on audio thread at start of audio block
    void startBlock(std::size_t numSamples, double sampleRate) noexcept;

    // Called on audio thread at end of audio block
    void endBlock() noexcept;

    // Timer utility for scoped track/plugin measurement on audio thread
    class ScopedTimer final
    {
    public:
        using Clock = std::chrono::steady_clock;
        explicit ScopedTimer(float& outTargetUs) noexcept
            : m_outTargetUs(outTargetUs)
            , m_start(Clock::now())
        {
        }

        ~ScopedTimer() noexcept
        {
            const auto end = Clock::now();
            const auto dur = std::chrono::duration<float, std::micro>(end - m_start).count();
            m_outTargetUs = dur;
        }

    private:
        float& m_outTargetUs;
        Clock::time_point m_start;
    };

    // Audio-thread record methods
    void recordTrackLoad(std::uint64_t trackId, float executionUs) noexcept;
    void recordSlotLoad(std::uint64_t trackId, std::size_t slotIdx, float executionUs) noexcept;

    // Lock-free read on message thread / UI
    [[nodiscard]] DspPerformanceSnapshot readSnapshot() const noexcept;

    void reset() noexcept;

private:
    std::chrono::steady_clock::time_point m_blockStartTime;
    float m_currentBudgetUs {2666.7f};
    std::atomic<float> m_currentCpuPercent {0.0f};
    std::atomic<float> m_peakCpuPercent {0.0f};
    std::atomic<float> m_blockBudgetUs {2666.7f};
    std::atomic<float> m_lastBlockElapsedUs {0.0f};
    std::atomic<std::uint64_t> m_totalBlocksProcessed {0};
    std::atomic<std::uint64_t> m_xrunCount {0};
    std::atomic<bool> m_isOverBudget {false};

    // Staging and published track buffers
    struct TrackStorage
    {
        std::uint64_t trackId {0};
        float executionUs {0.0f};
        std::array<float, 8> slotUs {};
    };
    std::array<TrackStorage, maxTracks> m_workingTracks {};
    std::size_t m_workingTrackCount {0};

    std::array<std::atomic<std::uint64_t>, maxTracks> m_pubTrackIds {};
    std::array<std::atomic<float>, maxTracks> m_pubTrackUs {};
};

} // namespace dawg::engine
