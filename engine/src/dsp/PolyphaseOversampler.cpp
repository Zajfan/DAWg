#include <dawg/engine/dsp/PolyphaseOversampler.h>

namespace dawg::engine::dsp {

PolyphaseOversampler::PolyphaseOversampler(const OversampleFactor factor) noexcept
    : m_factor(factor)
{
}

bool PolyphaseOversampler::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_channelCount = channelCount;

    m_filtersX2.assign(channelCount, HalfBandFilter {});
    m_filtersX4.assign(channelCount, HalfBandFilter {});
    return true;
}

void PolyphaseOversampler::reset() noexcept
{
    for (auto& f : m_filtersX2)
    {
        f.reset();
    }
    for (auto& f : m_filtersX4)
    {
        f.reset();
    }
}

} // namespace dawg::engine::dsp
