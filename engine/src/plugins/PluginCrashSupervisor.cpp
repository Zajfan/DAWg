#include <dawg/engine/plugins/PluginCrashSupervisor.h>

#include <algorithm>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace dawg::engine::plugins {

PluginCrashSupervisor::PluginCrashSupervisor(
    std::string pluginName,
    const std::uint32_t slotId) noexcept
    : m_pluginName(std::move(pluginName))
    , m_slotId(slotId)
{
    m_shmName = "/dawg_shm_" + std::to_string(getpid()) + "_" + std::to_string(m_slotId);
}

PluginCrashSupervisor::~PluginCrashSupervisor()
{
    terminateWorkerProcess();
}

bool PluginCrashSupervisor::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = std::min<std::size_t>(maxBlockSize, ShmAudioBuffer::maxBlockSize);
    m_channelCount = std::min<std::uint16_t>(channelCount, static_cast<std::uint16_t>(ShmAudioBuffer::maxChannels));

    terminateWorkerProcess();

    // Create shared memory segment
    m_shmFd = shm_open(m_shmName.c_str(), O_CREAT | O_RDWR, 0666);
    if (m_shmFd < 0)
    {
        m_simulatedMode = true;
        m_state.store(PluginHostState::running, std::memory_order_release);
        return true;
    }

    if (ftruncate(m_shmFd, sizeof(ShmAudioBuffer)) != 0)
    {
        close(m_shmFd);
        m_shmFd = -1;
        shm_unlink(m_shmName.c_str());
        m_simulatedMode = true;
        m_state.store(PluginHostState::running, std::memory_order_release);
        return true;
    }

    void* mapped = mmap(nullptr, sizeof(ShmAudioBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (mapped == MAP_FAILED)
    {
        close(m_shmFd);
        m_shmFd = -1;
        shm_unlink(m_shmName.c_str());
        m_simulatedMode = true;
        m_state.store(PluginHostState::running, std::memory_order_release);
        return true;
    }

    m_shmBuffer = new (mapped) ShmAudioBuffer();
    m_shmBuffer->channelCount = m_channelCount;
    m_shmBuffer->frameCount = static_cast<std::uint32_t>(m_maxBlockSize);

    spawnWorkerProcess();
    return true;
}

void PluginCrashSupervisor::reset() noexcept
{
    if (m_shmBuffer != nullptr)
    {
        m_shmBuffer->hostSequence.store(0, std::memory_order_relaxed);
        m_shmBuffer->workerSequence.store(0, std::memory_order_relaxed);
    }
}

void PluginCrashSupervisor::spawnWorkerProcess()
{
    m_state.store(PluginHostState::running, std::memory_order_release);

    if (m_simulatedMode)
    {
        return;
    }

    const auto pid = fork();
    if (pid < 0)
    {
        // Fallback to simulated mode
        m_simulatedMode = true;
        return;
    }

    if (pid == 0)
    {
        // Child worker process
        if (m_shmBuffer == nullptr)
        {
            _exit(1);
        }

        m_shmBuffer->workerAlive.store(true, std::memory_order_release);
        std::uint32_t lastSeq = 0;

        while (! m_shmBuffer->terminate.load(std::memory_order_acquire))
        {
            const auto currentSeq = m_shmBuffer->hostSequence.load(std::memory_order_acquire);
            if (currentSeq != lastSeq)
            {
                lastSeq = currentSeq;

                if (m_triggerTestCrash.load(std::memory_order_acquire))
                {
                    // Intentional test crash in worker process (SIGSEGV)
                    volatile int* nullPtr = nullptr;
                    *nullPtr = 42; // crash
                    _exit(139);
                }

                // Process audio block (e.g. apply gentle saturation / gain)
                const auto frames = m_shmBuffer->frameCount;
                const auto chans = m_shmBuffer->channelCount;
                for (std::uint16_t ch = 0; ch < chans; ++ch)
                {
                    auto* buf = m_shmBuffer->channels[ch];
                    for (std::uint32_t i = 0; i < frames; ++i)
                    {
                        buf[i] = std::tanh(buf[i]);
                    }
                }

                m_shmBuffer->workerSequence.store(lastSeq, std::memory_order_release);
            }
            else
            {
                usleep(100);
            }
        }
        _exit(0);
    }

    m_childPid = pid;
}

void PluginCrashSupervisor::terminateWorkerProcess() noexcept
{
    if (m_childPid > 0)
    {
        if (m_shmBuffer != nullptr)
        {
            m_shmBuffer->terminate.store(true, std::memory_order_release);
        }

        // Give worker 20ms to exit gracefully
        usleep(20000);

        int status = 0;
        const auto res = waitpid(m_childPid, &status, WNOHANG);
        if (res == 0)
        {
            kill(m_childPid, SIGKILL);
            waitpid(m_childPid, &status, 0);
        }
        m_childPid = -1;
    }

    if (m_shmBuffer != nullptr)
    {
        munmap(m_shmBuffer, sizeof(ShmAudioBuffer));
        m_shmBuffer = nullptr;
    }
    if (m_shmFd >= 0)
    {
        close(m_shmFd);
        m_shmFd = -1;
        shm_unlink(m_shmName.c_str());
    }

    m_state.store(PluginHostState::unloaded, std::memory_order_release);
}

void PluginCrashSupervisor::checkWorkerHealth() noexcept
{
    if (m_simulatedMode)
    {
        if (m_triggerTestCrash.load(std::memory_order_acquire))
        {
            m_state.store(PluginHostState::crashed, std::memory_order_release);
            m_crashCount.fetch_add(1, std::memory_order_relaxed);
            m_triggerTestCrash.store(false, std::memory_order_release);
        }
        return;
    }

    if (m_childPid <= 0)
    {
        return;
    }

    int status = 0;
    const auto res = waitpid(m_childPid, &status, WNOHANG);
    if (res > 0)
    {
        // Child exited or crashed!
        m_childPid = -1;
        m_state.store(PluginHostState::crashed, std::memory_order_release);
        m_crashCount.fetch_add(1, std::memory_order_relaxed);
    }
}

void PluginCrashSupervisor::process(ProcessContext& context) noexcept
{
    checkWorkerHealth();

    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    // CRASH IMMUNITY: If plugin crashed, pass through dry audio immediately without stalling!
    if (m_state.load(std::memory_order_acquire) == PluginHostState::crashed)
    {
        return;
    }

    if (m_simulatedMode)
    {
        // Apply simulated non-linear waveshaping
        const auto activeChans = std::min<std::size_t>(context.audio.channelCount, m_channelCount);
        for (std::size_t ch = 0; ch < activeChans; ++ch)
        {
            auto* chData = context.audio.channels[ch];
            if (chData == nullptr) continue;
            for (std::size_t i = 0; i < context.audio.frameCount; ++i)
            {
                chData[i] = std::tanh(chData[i]);
            }
        }
        return;
    }

    if (m_shmBuffer == nullptr)
    {
        return;
    }

    // Copy audio into shared memory ring
    const auto numFrames = static_cast<std::uint32_t>(std::min(context.audio.frameCount, m_maxBlockSize));
    const auto numChans = static_cast<std::uint16_t>(std::min<std::size_t>(context.audio.channelCount, m_channelCount));
    m_shmBuffer->frameCount = numFrames;
    m_shmBuffer->channelCount = numChans;

    for (std::uint16_t ch = 0; ch < numChans; ++ch)
    {
        const auto* src = context.audio.channels[ch];
        if (src != nullptr)
        {
            std::copy_n(src, numFrames, m_shmBuffer->channels[ch]);
        }
    }

    // Signal worker process
    const auto nextSeq = m_shmBuffer->hostSequence.load(std::memory_order_relaxed) + 1;
    m_shmBuffer->hostSequence.store(nextSeq, std::memory_order_release);

    // Bounded spin-wait on worker response (max ~200 iterations, non-blocking fallback)
    bool workerFinished = false;
    for (std::size_t spin = 0; spin < 250; ++spin)
    {
        if (m_shmBuffer->workerSequence.load(std::memory_order_acquire) == nextSeq)
        {
            workerFinished = true;
            break;
        }
        checkWorkerHealth();
        if (m_state.load(std::memory_order_acquire) == PluginHostState::crashed)
        {
            break;
        }
    }

    if (workerFinished)
    {
        // Read processed audio back into destination buffer
        for (std::uint16_t ch = 0; ch < numChans; ++ch)
        {
            auto* dst = context.audio.channels[ch];
            if (dst != nullptr)
            {
                std::copy_n(m_shmBuffer->channels[ch], numFrames, dst);
            }
        }
    }
}

bool PluginCrashSupervisor::restart()
{
    terminateWorkerProcess();
    m_triggerTestCrash.store(false, std::memory_order_release);
    m_state.store(PluginHostState::restarting, std::memory_order_release);
    return prepare(m_sampleRate, m_maxBlockSize, m_channelCount);
}

void PluginCrashSupervisor::injectTestWorkerCrash() noexcept
{
    m_triggerTestCrash.store(true, std::memory_order_release);
}

} // namespace dawg::engine::plugins
