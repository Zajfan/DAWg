#include <dawg/engine/Metronome.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

void Metronome::setEnabled(const bool enabled) noexcept
{
    m_enabled.store(enabled, std::memory_order_relaxed);
}

bool Metronome::isEnabled() const noexcept
{
    return m_enabled.load(std::memory_order_relaxed);
}

void Metronome::setGain(const float gainLinear) noexcept
{
    m_gainLinear.store(std::clamp(gainLinear, 0.0f, 2.0f), std::memory_order_relaxed);
}

float Metronome::gain() const noexcept
{
    return m_gainLinear.load(std::memory_order_relaxed);
}

void Metronome::reset() noexcept
{
    m_phase = 0.0;
    m_phaseIncrement = 0.0;
    m_envelope = 0.0f;
}

void Metronome::renderSlice(const AudioBufferView output,
                            const std::size_t blockOffset,
                            const std::size_t sliceFrames,
                            const std::uint64_t timelineStartSample,
                            const double sampleRate,
                            const TempoMap& tempoMap) noexcept
{
    if (! isEnabled() || output.channels == nullptr || sliceFrames == 0 || sampleRate <= 0.0)
        return;

    const float masterGain = gain();
    if (masterGain <= 0.0f)
        return;

    // 25 ms exponential decay constant
    m_envelopeDecay = static_cast<float>(std::exp(-1.0 / (0.025 * sampleRate)));

    for (std::size_t f = 0; f < sliceFrames; ++f)
    {
        const std::uint64_t currentSample = timelineStartSample + f;
        const auto bbt = tempoMap.sampleToBarBeatTick(currentSample, sampleRate);
        const bool isExactSampleZero = (currentSample == 0);

        bool triggerClick = isExactSampleZero;
        if (! triggerClick && bbt.tick == 0)
        {
            const auto prevBbt = tempoMap.sampleToBarBeatTick(currentSample - 1, sampleRate);
            triggerClick = (prevBbt.beat != bbt.beat || prevBbt.bar != bbt.bar);
        }

        if (triggerClick)
        {
            const double freq = (bbt.beat == 1) ? 1760.0 : 880.0;
            m_phase = 0.0;
            m_phaseIncrement = (2.0 * std::numbers::pi_v<double> * freq) / sampleRate;
            m_envelope = (bbt.beat == 1) ? 1.0f : 0.7f;
        }

        if (m_envelope > 1e-4f)
        {
            const float sampleVal =
                static_cast<float>(std::sin(m_phase)) * m_envelope * masterGain;
            m_phase += m_phaseIncrement;
            if (m_phase >= 2.0 * std::numbers::pi_v<double>)
                m_phase -= 2.0 * std::numbers::pi_v<double>;
            m_envelope *= m_envelopeDecay;

            for (std::size_t ch = 0; ch < output.channelCount; ++ch)
            {
                if (output.channels[ch] != nullptr)
                    output.channels[ch][blockOffset + f] += sampleVal;
            }
        }
    }
}

} // namespace dawg::engine
