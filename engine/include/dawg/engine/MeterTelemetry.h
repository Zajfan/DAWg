#pragma once

#include <atomic>

namespace dawg::engine {

struct MeterReading
{
    float peakLeft {};
    float peakRight {};
    float rmsLeft {};
    float rmsRight {};
};

class MeterPublisher final
{
public:
    MeterPublisher() noexcept = default;

    void publish(const MeterReading& reading) noexcept
    {
        m_peakLeft.store(reading.peakLeft, std::memory_order_relaxed);
        m_peakRight.store(reading.peakRight, std::memory_order_relaxed);
        m_rmsLeft.store(reading.rmsLeft, std::memory_order_relaxed);
        m_rmsRight.store(reading.rmsRight, std::memory_order_release);
    }

    void reset() noexcept
    {
        publish({});
    }

    [[nodiscard]] MeterReading read() const noexcept
    {
        const float rmsR = m_rmsRight.load(std::memory_order_acquire);
        return {
            .peakLeft = m_peakLeft.load(std::memory_order_relaxed),
            .peakRight = m_peakRight.load(std::memory_order_relaxed),
            .rmsLeft = m_rmsLeft.load(std::memory_order_relaxed),
            .rmsRight = rmsR
        };
    }

private:
    std::atomic<float> m_peakLeft {0.0f};
    std::atomic<float> m_peakRight {0.0f};
    std::atomic<float> m_rmsLeft {0.0f};
    std::atomic<float> m_rmsRight {0.0f};
};

} // namespace dawg::engine
