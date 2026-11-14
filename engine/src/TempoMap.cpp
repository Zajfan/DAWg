#include <dawg/engine/TempoMap.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {
namespace {
bool isValidPowerOfTwoDenominator(const std::uint16_t denom) noexcept
{
    return denom != 0 && (denom & (denom - 1u)) == 0 && denom <= 64u;
}

double samplesPerTick(const double bpm, const double sampleRate) noexcept
{
    const double beatsPerSecond = bpm / 60.0;
    const double ticksPerSecond = beatsPerSecond * static_cast<double>(TempoMap::kTicksPerQuarterNote);
    return sampleRate / ticksPerSecond;
}
} // namespace

TempoMap::TempoMap()
{
    m_tempos.push_back(TempoEvent {.tick = 0, .bpm = 120.0});
    m_signatures.push_back(TimeSignatureEvent {.tick = 0, .numerator = 4, .denominator = 4});
}

bool TempoMap::setEvents(const std::span<const TempoEvent> tempos,
                         const std::span<const TimeSignatureEvent> signatures)
{
    if (tempos.empty() || signatures.empty())
        return false;
    if (tempos.front().tick != 0 || signatures.front().tick != 0)
        return false;

    for (std::size_t i = 0; i < tempos.size(); ++i)
    {
        if (! std::isfinite(tempos[i].bpm) || tempos[i].bpm <= 0.0 || tempos[i].bpm > 1000.0)
            return false;
        if (i > 0 && tempos[i].tick <= tempos[i - 1].tick)
            return false;
    }

    for (std::size_t i = 0; i < signatures.size(); ++i)
    {
        if (signatures[i].numerator == 0
            || ! isValidPowerOfTwoDenominator(signatures[i].denominator))
        {
            return false;
        }
        if (i > 0 && signatures[i].tick <= signatures[i - 1].tick)
            return false;
    }

    m_tempos.assign(tempos.begin(), tempos.end());
    m_signatures.assign(signatures.begin(), signatures.end());
    return true;
}

double TempoMap::sampleToTick(const std::uint64_t sample, const double sampleRate) const noexcept
{
    if (! std::isfinite(sampleRate) || sampleRate <= 0.0 || m_tempos.empty())
        return 0.0;

    double remainingSamples = static_cast<double>(sample);
    for (std::size_t i = 0; i < m_tempos.size(); ++i)
    {
        const double spt = samplesPerTick(m_tempos[i].bpm, sampleRate);
        const auto currentTick = static_cast<double>(m_tempos[i].tick);

        if (i + 1 < m_tempos.size())
        {
            const auto nextTick = static_cast<double>(m_tempos[i + 1].tick);
            const double segmentTicks = nextTick - currentTick;
            const double segmentSamples = segmentTicks * spt;
            if (remainingSamples < segmentSamples)
                return currentTick + (remainingSamples / spt);
            remainingSamples -= segmentSamples;
        }
        else
        {
            return currentTick + (remainingSamples / spt);
        }
    }
    return 0.0;
}

std::uint64_t TempoMap::tickToSample(const double tick, const double sampleRate) const noexcept
{
    if (! std::isfinite(sampleRate) || sampleRate <= 0.0 || tick <= 0.0 || m_tempos.empty())
        return 0;

    double accumulatedSamples = 0.0;
    for (std::size_t i = 0; i < m_tempos.size(); ++i)
    {
        const double spt = samplesPerTick(m_tempos[i].bpm, sampleRate);
        const auto currentTick = static_cast<double>(m_tempos[i].tick);

        if (i + 1 < m_tempos.size() && tick >= static_cast<double>(m_tempos[i + 1].tick))
        {
            const auto nextTick = static_cast<double>(m_tempos[i + 1].tick);
            accumulatedSamples += (nextTick - currentTick) * spt;
        }
        else
        {
            accumulatedSamples += (tick - currentTick) * spt;
            break;
        }
    }

    return static_cast<std::uint64_t>(std::llround(accumulatedSamples));
}

BarBeatTick TempoMap::sampleToBarBeatTick(const std::uint64_t sample,
                                          const double sampleRate) const noexcept
{
    const auto absoluteTick = static_cast<std::uint64_t>(
        std::llround(sampleToTick(sample, sampleRate)));

    std::uint32_t bar = 1;
    for (std::size_t i = 0; i < m_signatures.size(); ++i)
    {
        const auto& sig = m_signatures[i];
        const std::uint64_t ticksPerBeat =
            (static_cast<std::uint64_t>(kTicksPerQuarterNote) * 4u) / sig.denominator;
        const std::uint64_t ticksPerBar = ticksPerBeat * sig.numerator;

        if (i + 1 < m_signatures.size() && absoluteTick >= m_signatures[i + 1].tick)
        {
            const std::uint64_t spanTicks = m_signatures[i + 1].tick - sig.tick;
            bar += static_cast<std::uint32_t>(spanTicks / std::max<std::uint64_t>(1, ticksPerBar));
        }
        else
        {
            const std::uint64_t offsetTicks = absoluteTick - sig.tick;
            const std::uint64_t barsInSegment =
                offsetTicks / std::max<std::uint64_t>(1, ticksPerBar);
            const std::uint64_t remAfterBars =
                offsetTicks % std::max<std::uint64_t>(1, ticksPerBar);
            const std::uint64_t beatZeroBased =
                remAfterBars / std::max<std::uint64_t>(1, ticksPerBeat);
            const std::uint64_t tickInBeat =
                remAfterBars % std::max<std::uint64_t>(1, ticksPerBeat);

            return BarBeatTick {
                .bar = bar + static_cast<std::uint32_t>(barsInSegment),
                .beat = static_cast<std::uint32_t>(beatZeroBased + 1u),
                .tick = static_cast<std::uint32_t>(tickInBeat)
            };
        }
    }

    return {};
}

double TempoMap::bpmAtTick(const std::uint64_t tick) const noexcept
{
    double activeBpm = 120.0;
    for (const auto& event : m_tempos)
    {
        if (event.tick <= tick)
            activeBpm = event.bpm;
        else
            break;
    }
    return activeBpm;
}

TimeSignatureEvent TempoMap::timeSignatureAtTick(const std::uint64_t tick) const noexcept
{
    TimeSignatureEvent active {.tick = 0, .numerator = 4, .denominator = 4};
    for (const auto& sig : m_signatures)
    {
        if (sig.tick <= tick)
            active = sig;
        else
            break;
    }
    return active;
}

} // namespace dawg::engine
