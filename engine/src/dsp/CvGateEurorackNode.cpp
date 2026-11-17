#include <dawg/engine/dsp/CvGateEurorackNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

CvGateEurorackNode::CvGateEurorackNode() noexcept
{
    // Default 4-channel Eurorack modular layout:
    // Ch 0: 1V/Oct Pitch CV
    // Ch 1: Gate
    // Ch 2: Clock / Sync
    // Ch 3: Modulation LFO
    m_channels[0].mode = CvOutputMode::pitch1VPerOct;
    m_channels[0].maxVolts = 5.0f;

    m_channels[1].mode = CvOutputMode::gateHighLow;
    m_channels[1].maxVolts = 5.0f;

    m_channels[2].mode = CvOutputMode::clock24PPQN;
    m_channels[2].maxVolts = 5.0f;

    m_channels[3].mode = CvOutputMode::lfoModulation;
    m_channels[3].maxVolts = 5.0f;
    m_channels[3].lfoRateHz = 2.0f;
    m_channels[3].lfoShape = CvLfoShape::triangle;

    for (auto& offset : m_calibrationOffsets)
    {
        offset.store(0.0f, std::memory_order_relaxed);
    }
}

bool CvGateEurorackNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void CvGateEurorackNode::reset() noexcept
{
    m_currentNote.store(60, std::memory_order_release);
    m_gateActive.store(false, std::memory_order_release);
    m_pitchBendSemitones.store(0.0f, std::memory_order_release);

    m_targetPitchVoltage = 0.0f;
    m_currentPitchVoltage = 0.0f;
    m_triggerCountdownSamples = 0;
    m_clockPulseSamples = 0;
    m_lfoPhase = 0.0;
    m_lastSahValue = 0.0f;
}

void CvGateEurorackNode::setChannelConfig(std::size_t channelIdx, const CvChannelConfig& config) noexcept
{
    if (channelIdx < maxCvChannels)
    {
        m_channels[channelIdx] = config;
    }
}

CvChannelConfig CvGateEurorackNode::getChannelConfig(std::size_t channelIdx) const noexcept
{
    if (channelIdx < maxCvChannels)
    {
        return m_channels[channelIdx];
    }
    return {};
}

void CvGateEurorackNode::noteOn(std::uint8_t note, std::uint8_t /*velocity*/) noexcept
{
    m_currentNote.store(note, std::memory_order_release);
    m_gateActive.store(true, std::memory_order_release);

    // 5ms trigger pulse (0.005 sec * sampleRate)
    m_triggerCountdownSamples = static_cast<std::uint32_t>(m_sampleRate * 0.005);
}

void CvGateEurorackNode::noteOff(std::uint8_t note) noexcept
{
    if (m_currentNote.load(std::memory_order_acquire) == note)
    {
        m_gateActive.store(false, std::memory_order_release);
    }
}

void CvGateEurorackNode::setPitchBend(float semitones) noexcept
{
    m_pitchBendSemitones.store(std::clamp(semitones, -24.0f, 24.0f), std::memory_order_release);
}

void CvGateEurorackNode::tickClock() noexcept
{
    // 5ms clock pulse width
    m_clockPulseSamples = static_cast<std::uint32_t>(m_sampleRate * 0.005);
}

void CvGateEurorackNode::setCalibrationOffset(std::uint8_t note, float voltageOffset) noexcept
{
    if (note < calibrationPoints)
    {
        m_calibrationOffsets[note].store(voltageOffset, std::memory_order_release);
    }
}

float CvGateEurorackNode::getCalibrationOffset(std::uint8_t note) const noexcept
{
    if (note < calibrationPoints)
    {
        return m_calibrationOffsets[note].load(std::memory_order_acquire);
    }
    return 0.0f;
}

void CvGateEurorackNode::process(ProcessContext& context) noexcept
{
    const std::size_t numSamples = context.audio.frameCount;
    const std::size_t numOutChannels = std::min(context.audio.channelCount, maxCvChannels);

    if (numSamples == 0 || numOutChannels == 0 || context.audio.channels == nullptr)
        return;

    const auto currentNote = m_currentNote.load(std::memory_order_acquire);
    const auto bend = m_pitchBendSemitones.load(std::memory_order_acquire);
    const auto gate = m_gateActive.load(std::memory_order_acquire);

    // Compute raw 1V/Oct target voltage: Middle C (60) = 0.0V
    const float calOffset = (currentNote < calibrationPoints) ? m_calibrationOffsets[currentNote].load(std::memory_order_acquire) : 0.0f;
    const float rawVoltage = (static_cast<float>(currentNote) + bend - 60.0f) / 12.0f + calOffset;
    m_targetPitchVoltage = rawVoltage;

    for (std::size_t ch = 0; ch < numOutChannels; ++ch)
    {
        float* out = context.audio.channels[ch];
        if (out == nullptr) continue;

        const auto& cfg = m_channels[ch];
        const float maxV = (cfg.maxVolts > 0.1f) ? cfg.maxVolts : 5.0f;

        switch (cfg.mode)
        {
        case CvOutputMode::pitch1VPerOct:
        {
            // Portamento slew rate coefficient
            float slew = 1.0f;
            if (cfg.portamentoMs > 0.1f)
            {
                slew = static_cast<float>(1.0 - std::exp(-1.0 / (m_sampleRate * (cfg.portamentoMs * 0.001))));
            }

            for (std::size_t i = 0; i < numSamples; ++i)
            {
                m_currentPitchVoltage += (m_targetPitchVoltage - m_currentPitchVoltage) * slew;
                // Normalize voltage to [-1.0, 1.0] audio output range
                out[i] = std::clamp(m_currentPitchVoltage / maxV, -1.0f, 1.0f);
            }
            break;
        }

        case CvOutputMode::gateHighLow:
        {
            const float gateLevel = gate ? 1.0f : 0.0f;
            for (std::size_t i = 0; i < numSamples; ++i)
            {
                out[i] = gateLevel;
            }
            break;
        }

        case CvOutputMode::triggerPulse:
        {
            for (std::size_t i = 0; i < numSamples; ++i)
            {
                if (m_triggerCountdownSamples > 0)
                {
                    out[i] = 1.0f;
                    --m_triggerCountdownSamples;
                }
                else
                {
                    out[i] = 0.0f;
                }
            }
            break;
        }

        case CvOutputMode::clock24PPQN:
        {
            for (std::size_t i = 0; i < numSamples; ++i)
            {
                if (m_clockPulseSamples > 0)
                {
                    out[i] = 1.0f;
                    --m_clockPulseSamples;
                }
                else
                {
                    out[i] = 0.0f;
                }
            }
            break;
        }

        case CvOutputMode::lfoModulation:
        {
            const double phaseInc = cfg.lfoRateHz / m_sampleRate;
            constexpr double twoPi = 6.2831853071795864769;

            for (std::size_t i = 0; i < numSamples; ++i)
            {
                float sampleVal = 0.0f;
                switch (cfg.lfoShape)
                {
                case CvLfoShape::sine:
                    sampleVal = static_cast<float>(std::sin(m_lfoPhase * twoPi));
                    break;
                case CvLfoShape::triangle:
                    sampleVal = static_cast<float>(m_lfoPhase < 0.5 ? (4.0 * m_lfoPhase - 1.0) : (3.0 - 4.0 * m_lfoPhase));
                    break;
                case CvLfoShape::rampUp:
                    sampleVal = static_cast<float>(2.0 * m_lfoPhase - 1.0);
                    break;
                case CvLfoShape::rampDown:
                    sampleVal = static_cast<float>(1.0 - 2.0 * m_lfoPhase);
                    break;
                case CvLfoShape::square:
                    sampleVal = (m_lfoPhase < 0.5) ? 1.0f : -1.0f;
                    break;
                case CvLfoShape::sampleAndHold:
                    sampleVal = m_lastSahValue;
                    break;
                }

                m_lfoPhase += phaseInc;
                if (m_lfoPhase >= 1.0)
                {
                    m_lfoPhase -= 1.0;
                    // S&H pseudo-random update
                    m_lastSahValue = static_cast<float>((static_cast<int>(m_lfoPhase * 1000.0) % 200 - 100)) / 100.0f;
                }

                out[i] = sampleVal * std::clamp(cfg.lfoDepth, 0.0f, 1.0f);
            }
            break;
        }
        }
    }
}

} // namespace dawg::engine::dsp
