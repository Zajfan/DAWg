#include <dawg/engine/WorkStealingScheduler.h>

namespace dawg::engine
{
WorkStealingScheduler::~WorkStealingScheduler() noexcept
{
    shutdown();
}

bool WorkStealingScheduler::prepare(std::size_t workerCount)
{
    shutdown();

    if (workerCount == 0)
    {
        return true;
    }

    m_stopping.store(false, std::memory_order_relaxed);
    m_epoch.store(0, std::memory_order_relaxed);
    m_taskFn.store(nullptr, std::memory_order_relaxed);
    m_taskContext.store(nullptr, std::memory_order_relaxed);
    m_taskCount.store(0, std::memory_order_relaxed);
    m_nextTaskIndex.store(0, std::memory_order_relaxed);
    m_completedTaskCount.store(0, std::memory_order_relaxed);
    m_readyWorkers.store(0, std::memory_order_relaxed);
    m_finishedWorkers.store(0, std::memory_order_relaxed);

    m_workers.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back([this]() noexcept { workerLoop(); });
    }

    while (m_readyWorkers.load(std::memory_order_acquire) < workerCount)
    {
        std::this_thread::yield();
    }

    return true;
}

void WorkStealingScheduler::shutdown() noexcept
{
    if (m_workers.empty())
    {
        return;
    }

    m_stopping.store(true, std::memory_order_release);
    m_epoch.fetch_add(1, std::memory_order_release);
    m_epoch.notify_all();

    for (auto& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    m_workers.clear();
}

void WorkStealingScheduler::executeWavefront(std::size_t taskCount, TaskFn fn, void* context) noexcept
{
    if (taskCount == 0 || fn == nullptr)
    {
        return;
    }

    if (taskCount == 1 || m_workers.empty())
    {
        for (std::size_t i = 0; i < taskCount; ++i)
        {
            fn(context, i);
        }
        return;
    }

    m_taskFn.store(fn, std::memory_order_relaxed);
    m_taskContext.store(context, std::memory_order_relaxed);
    m_taskCount.store(taskCount, std::memory_order_relaxed);
    m_nextTaskIndex.store(0, std::memory_order_relaxed);
    m_completedTaskCount.store(0, std::memory_order_relaxed);
    m_finishedWorkers.store(0, std::memory_order_relaxed);

    m_epoch.fetch_add(1, std::memory_order_release);
    m_epoch.notify_all();

    while (true)
    {
        const auto taskIndex = m_nextTaskIndex.fetch_add(1, std::memory_order_acq_rel);
        if (taskIndex >= taskCount)
        {
            break;
        }

        fn(context, taskIndex);
        m_completedTaskCount.fetch_add(1, std::memory_order_acq_rel);
    }

    const auto totalWorkers = m_workers.size();
    while (m_completedTaskCount.load(std::memory_order_acquire) < taskCount
           || m_finishedWorkers.load(std::memory_order_acquire) < totalWorkers)
    {
        std::this_thread::yield();
    }
}

void WorkStealingScheduler::workerLoop() noexcept
{
    std::uint64_t seenEpoch = 0;
    m_readyWorkers.fetch_add(1, std::memory_order_acq_rel);

    while (! m_stopping.load(std::memory_order_acquire))
    {
        m_epoch.wait(seenEpoch, std::memory_order_acquire);
        seenEpoch = m_epoch.load(std::memory_order_acquire);

        if (m_stopping.load(std::memory_order_acquire))
        {
            break;
        }

        const auto fn = m_taskFn.load(std::memory_order_acquire);
        auto* const ctx = m_taskContext.load(std::memory_order_acquire);
        const auto count = m_taskCount.load(std::memory_order_acquire);

        if (fn != nullptr && count > 0)
        {
            while (true)
            {
                const auto taskIndex = m_nextTaskIndex.fetch_add(1, std::memory_order_acq_rel);
                if (taskIndex >= count)
                {
                    break;
                }

                fn(ctx, taskIndex);
                m_completedTaskCount.fetch_add(1, std::memory_order_acq_rel);
            }
        }

        m_finishedWorkers.fetch_add(1, std::memory_order_acq_rel);
    }
}
} // namespace dawg::engine
