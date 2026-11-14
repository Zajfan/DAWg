#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace dawg::engine
{
class WorkStealingScheduler
{
public:
    using TaskFn = void (*)(void* context, std::size_t taskIndex) noexcept;

    WorkStealingScheduler() = default;
    ~WorkStealingScheduler() noexcept;

    WorkStealingScheduler(const WorkStealingScheduler&) = delete;
    WorkStealingScheduler& operator=(const WorkStealingScheduler&) = delete;

    [[nodiscard]] bool prepare(std::size_t workerCount);
    void shutdown() noexcept;

    void executeWavefront(std::size_t taskCount, TaskFn fn, void* context) noexcept;

    [[nodiscard]] std::size_t workerCount() const noexcept
    {
        return m_workers.size();
    }

private:
    void workerLoop() noexcept;

    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stopping {false};
    std::atomic<std::uint64_t> m_epoch {0};
    std::atomic<TaskFn> m_taskFn {nullptr};
    std::atomic<void*> m_taskContext {nullptr};
    std::atomic<std::size_t> m_taskCount {0};
    std::atomic<std::size_t> m_nextTaskIndex {0};
    std::atomic<std::size_t> m_completedTaskCount {0};
    std::atomic<std::size_t> m_readyWorkers {0};
    std::atomic<std::size_t> m_finishedWorkers {0};
};
} // namespace dawg::engine
