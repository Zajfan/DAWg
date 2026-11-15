#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::plugins {

enum class PluginHostState : std::uint8_t
{
    unloaded,
    running,
    crashed,
    restarting
};

struct ShmAudioBuffer
{
    static constexpr std::size_t maxChannels = 8;
    static constexpr std::size_t maxBlockSize = 2048;

    std::atomic<std::uint32_t> hostSequence {0};
    std::atomic<std::uint32_t> workerSequence {0};
    std::atomic<bool> terminate {false};
    std::atomic<bool> workerAlive {false};

    std::uint32_t frameCount {0};
    std::uint16_t channelCount {2};
    std::uint32_t latencySamples {0};
    float channels[maxChannels][maxBlockSize] {};
};

class PluginCrashSupervisor final : public ProcessorNode
{
public:
    explicit PluginCrashSupervisor(
        std::string pluginName = "AirGappedPlugin",
        std::uint32_t slotId = 0) noexcept;
    ~PluginCrashSupervisor() override;

    PluginCrashSupervisor(const PluginCrashSupervisor&) = delete;
    PluginCrashSupervisor& operator=(const PluginCrashSupervisor&) = delete;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return m_latencySamples.load(std::memory_order_relaxed);
    }

    void process(ProcessContext& context) noexcept override;

    // Process supervision & crash recovery
    [[nodiscard]] PluginHostState state() const noexcept
    {
        return m_state.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool isCrashed() const noexcept
    {
        return m_state.load(std::memory_order_acquire) == PluginHostState::crashed;
    }

    [[nodiscard]] const std::string& pluginName() const noexcept { return m_pluginName; }
    [[nodiscard]] std::uint32_t crashCount() const noexcept { return m_crashCount.load(std::memory_order_relaxed); }

    // 1-Click Air-Gapped Restart
    bool restart();

    // Trigger intentional test crash in worker process to verify air-gap isolation
    void injectTestWorkerCrash() noexcept;

    // Direct simulated in-memory mode for environments without fork/shm
    void setSimulatedCrashMode(bool simulated) noexcept { m_simulatedMode = simulated; }

private:
    void spawnWorkerProcess();
    void terminateWorkerProcess() noexcept;
    void checkWorkerHealth() noexcept;

    std::string m_pluginName;
    std::uint32_t m_slotId {0};
    std::string m_shmName;
    int m_shmFd {-1};
    ShmAudioBuffer* m_shmBuffer {nullptr};
    std::int32_t m_childPid {-1};

    double m_sampleRate {48000.0};
    std::size_t m_maxBlockSize {512};
    std::uint16_t m_channelCount {2};

    std::atomic<PluginHostState> m_state {PluginHostState::unloaded};
    std::atomic<std::uint32_t> m_latencySamples {0};
    std::atomic<std::uint32_t> m_crashCount {0};
    std::atomic<bool> m_triggerTestCrash {false};
    bool m_simulatedMode {false};
};

} // namespace dawg::engine::plugins
