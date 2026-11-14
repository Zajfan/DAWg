#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine {

struct TempoEvent
{
    std::uint64_t tick {};
    double bpm {120.0};
};

struct TimeSignatureEvent
{
    std::uint64_t tick {};
    std::uint16_t numerator {4};
    std::uint16_t denominator {4};
};

struct BarBeatTick
{
    std::uint32_t bar {1};
    std::uint32_t beat {1};
    std::uint32_t tick {0};
};

class TempoMap final
{
public:
    static constexpr std::uint32_t kTicksPerQuarterNote = 960;

    TempoMap();

    // Control-thread configuration. Validates sorted ticks, finite BPM > 0,
    // and power-of-two time signature denominators.
    [[nodiscard]] bool setEvents(std::span<const TempoEvent> tempos,
                                 std::span<const TimeSignatureEvent> signatures);

    [[nodiscard]] double sampleToTick(std::uint64_t sample, double sampleRate) const noexcept;
    [[nodiscard]] std::uint64_t tickToSample(double tick, double sampleRate) const noexcept;
    [[nodiscard]] BarBeatTick sampleToBarBeatTick(std::uint64_t sample,
                                                  double sampleRate) const noexcept;
    [[nodiscard]] double bpmAtTick(std::uint64_t tick) const noexcept;
    [[nodiscard]] TimeSignatureEvent timeSignatureAtTick(std::uint64_t tick) const noexcept;

private:
    std::vector<TempoEvent> m_tempos;
    std::vector<TimeSignatureEvent> m_signatures;
};

} // namespace dawg::engine
