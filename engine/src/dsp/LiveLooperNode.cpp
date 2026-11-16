#include <dawg/engine/dsp/LiveLooperNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

LiveLooperNode::LiveLooperNode() noexcept
{
    reset();
}

bool LiveLooperNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    const auto maxSamples = static_cast<std::size_t>(MaxLoopSeconds * m_sampleRate);

    m_loopBufferL.assign(maxSamples, 0.0f);
    m_loopBufferR.assign(maxSamples, 0.0f);

    for (auto& hist : m_history)
    {
        hist.bufferL.assign(maxSamples, 0.0f);
        hist.bufferR.assign(maxSamples, 0.0f);
        hist.length = 0;
    }
    m_redoBackup.bufferL.assign(maxSamples, 0.0f);
    m_redoBackup.bufferR.assign(maxSamples, 0.0f);
    m_redoBackup.length = 0;

    reset();
    return true;
}

void LiveLooperNode::reset() noexcept
{
    m_state = LooperState::stopped;
    m_loopLength = 0;
    m_playbackPos = 0.0;
    m_layerCount = 0;
    m_redoAvailable = false;
    m_wfPhase = 0.0;

    if (!m_loopBufferL.empty())
    {
        std::fill(m_loopBufferL.begin(), m_loopBufferL.end(), 0.0f);
        std::fill(m_loopBufferR.begin(), m_loopBufferR.end(), 0.0f);
    }
}

void LiveLooperNode::setPitchSemitones(int semitones) noexcept
{
    m_pitchSemitones = std::clamp(semitones, -24, 24);
}

void LiveLooperNode::triggerRecord() noexcept
{
    if (m_state == LooperState::stopped)
    {
        clear();
        m_state = LooperState::recording;
        m_playbackPos = 0.0;
        m_loopLength = 0;
    }
    else if (m_state == LooperState::recording)
    {
        // Close initial loop and enter playing mode
        if (m_playbackPos > 100.0)
        {
            m_loopLength = static_cast<std::size_t>(m_playbackPos);
            commitCurrentLayerToHistory();
            m_state = LooperState::playing;
        }
        else
        {
            m_state = LooperState::stopped;
        }
    }
    else if (m_state == LooperState::playing)
    {
        triggerOverdub();
    }
}

void LiveLooperNode::triggerPlay() noexcept
{
    if (m_state == LooperState::recording && m_playbackPos > 100.0)
    {
        m_loopLength = static_cast<std::size_t>(m_playbackPos);
        commitCurrentLayerToHistory();
        m_state = LooperState::playing;
    }
    else if (m_state == LooperState::stopped && m_loopLength > 0)
    {
        m_state = LooperState::playing;
    }
    else if (m_state == LooperState::overdubbing)
    {
        m_state = LooperState::playing;
    }
}

void LiveLooperNode::triggerOverdub() noexcept
{
    if (m_loopLength == 0)
    {
        triggerRecord();
        return;
    }
    commitCurrentLayerToHistory();
    m_state = LooperState::overdubbing;
}

void LiveLooperNode::triggerStop() noexcept
{
    if (m_state == LooperState::recording && m_playbackPos > 100.0)
    {
        m_loopLength = static_cast<std::size_t>(m_playbackPos);
        commitCurrentLayerToHistory();
    }
    m_state = LooperState::stopped;
    m_playbackPos = 0.0;
}

void LiveLooperNode::clear() noexcept
{
    m_state = LooperState::stopped;
    m_loopLength = 0;
    m_playbackPos = 0.0;
    m_layerCount = 0;
    m_redoAvailable = false;
    if (!m_loopBufferL.empty())
    {
        std::fill(m_loopBufferL.begin(), m_loopBufferL.end(), 0.0f);
        std::fill(m_loopBufferR.begin(), m_loopBufferR.end(), 0.0f);
    }
}

void LiveLooperNode::commitCurrentLayerToHistory() noexcept
{
    if (m_layerCount >= MaxLayers)
    {
        // Shift history left
        for (std::size_t i = 0; i < MaxLayers - 1; ++i)
        {
            m_history[i].bufferL = m_history[i + 1].bufferL;
            m_history[i].bufferR = m_history[i + 1].bufferR;
            m_history[i].length = m_history[i + 1].length;
        }
        m_layerCount = MaxLayers - 1;
    }

    auto& target = m_history[m_layerCount];
    std::copy(m_loopBufferL.begin(), m_loopBufferL.end(), target.bufferL.begin());
    std::copy(m_loopBufferR.begin(), m_loopBufferR.end(), target.bufferR.begin());
    target.length = m_loopLength;
    ++m_layerCount;
    m_redoAvailable = false;
}

void LiveLooperNode::undo() noexcept
{
    if (m_layerCount == 0)
    {
        return;
    }

    // Save active state into redo
    std::copy(m_loopBufferL.begin(), m_loopBufferL.end(), m_redoBackup.bufferL.begin());
    std::copy(m_loopBufferR.begin(), m_loopBufferR.end(), m_redoBackup.bufferR.begin());
    m_redoBackup.length = m_loopLength;
    m_redoAvailable = true;

    // Restore previous
    --m_layerCount;
    const auto& prev = m_history[m_layerCount];
    std::copy(prev.bufferL.begin(), prev.bufferL.end(), m_loopBufferL.begin());
    std::copy(prev.bufferR.begin(), prev.bufferR.end(), m_loopBufferR.begin());
    m_loopLength = prev.length;

    if (m_loopLength == 0)
    {
        m_state = LooperState::stopped;
    }
}

void LiveLooperNode::redo() noexcept
{
    if (!m_redoAvailable)
    {
        return;
    }

    commitCurrentLayerToHistory();
    std::copy(m_redoBackup.bufferL.begin(), m_redoBackup.bufferL.end(), m_loopBufferL.begin());
    std::copy(m_redoBackup.bufferR.begin(), m_redoBackup.bufferR.end(), m_loopBufferR.begin());
    m_loopLength = m_redoBackup.length;
    m_redoAvailable = false;
}

float LiveLooperNode::playbackPositionNormalized() const noexcept
{
    if (m_loopLength == 0)
    {
        return 0.0f;
    }
    return std::clamp(static_cast<float>(m_playbackPos / static_cast<double>(m_loopLength)), 0.0f, 1.0f);
}

float LiveLooperNode::readLoopSample(const std::vector<float>& buffer, double pos) const noexcept
{
    if (m_loopLength == 0 || buffer.empty())
    {
        return 0.0f;
    }

    double wrappedPos = std::fmod(pos, static_cast<double>(m_loopLength));
    if (wrappedPos < 0.0)
    {
        wrappedPos += static_cast<double>(m_loopLength);
    }

    const auto idx0 = static_cast<std::size_t>(wrappedPos) % m_loopLength;
    const auto idx1 = (idx0 + 1) % m_loopLength;
    const float frac = static_cast<float>(wrappedPos - static_cast<double>(idx0));

    return buffer[idx0] + frac * (buffer[idx1] - buffer[idx0]);
}

void LiveLooperNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    const float maxCap = static_cast<float>(m_loopBufferL.size());
    const double pitchRate = std::pow(2.0, static_cast<double>(m_pitchSemitones) / 12.0);
    const double baseRate = static_cast<double>(m_speed) * pitchRate;

    const double wfInc = 2.0 * std::numbers::pi_v<double> * 0.5 / m_sampleRate;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inL = outL[i];
        const float inR = (outR != nullptr) ? outR[i] : inL;

        // Wow & Flutter pitch modulation
        m_wfPhase += wfInc;
        if (m_wfPhase >= 2.0 * std::numbers::pi_v<double>)
        {
            m_wfPhase -= 2.0 * std::numbers::pi_v<double>;
        }
        const double rateMod = 1.0 + 0.015 * static_cast<double>(m_wowFlutter) * std::sin(m_wfPhase);
        const double effectiveRate = baseRate * rateMod;

        float loopL = 0.0f;
        float loopR = 0.0f;

        if (m_state == LooperState::recording)
        {
            const auto writeIdx = static_cast<std::size_t>(m_playbackPos);
            if (writeIdx < m_loopBufferL.size())
            {
                m_loopBufferL[writeIdx] = inL;
                m_loopBufferR[writeIdx] = inR;
                m_playbackPos += 1.0;
                if (m_playbackPos >= maxCap)
                {
                    m_loopLength = static_cast<std::size_t>(maxCap);
                    commitCurrentLayerToHistory();
                    m_state = LooperState::playing;
                }
            }
            loopL = inL;
            loopR = inR;
        }
        else if (m_state == LooperState::playing || m_state == LooperState::overdubbing)
        {
            if (m_loopLength > 0)
            {
                // Read from loop buffer with anti-click crossfade at boundary
                loopL = readLoopSample(m_loopBufferL, m_playbackPos);
                loopR = readLoopSample(m_loopBufferR, m_playbackPos);

                if (m_state == LooperState::overdubbing && !m_frozen)
                {
                    const auto writeIdx = static_cast<std::size_t>(m_playbackPos) % m_loopLength;
                    if (m_overdubMode == OverdubMode::add)
                    {
                        m_loopBufferL[writeIdx] = inL + m_loopBufferL[writeIdx] * m_tapeDecay;
                        m_loopBufferR[writeIdx] = inR + m_loopBufferR[writeIdx] * m_tapeDecay;
                    }
                    else if (m_overdubMode == OverdubMode::replace)
                    {
                        m_loopBufferL[writeIdx] = inL;
                        m_loopBufferR[writeIdx] = inR;
                    }
                }

                // Advance playhead
                m_playbackPos += effectiveRate;
                while (m_playbackPos < 0.0)
                {
                    m_playbackPos += static_cast<double>(m_loopLength);
                }
                while (m_playbackPos >= static_cast<double>(m_loopLength))
                {
                    m_playbackPos -= static_cast<double>(m_loopLength);
                }
            }
        }

        // Dry/Wet blend
        outL[i] = m_mix * loopL + (1.0f - m_mix) * inL;
        if (outR != nullptr)
        {
            outR[i] = m_mix * loopR + (1.0f - m_mix) * inR;
        }
    }
}

} // namespace dawg::engine::dsp
