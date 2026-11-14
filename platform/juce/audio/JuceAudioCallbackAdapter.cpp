#include "JuceAudioCallbackAdapter.h"

#include <dawg/engine/AudioEngine.h>

#include <algorithm>
#include <array>

namespace dawg::platform::juce {

JuceAudioCallbackAdapter::JuceAudioCallbackAdapter(dawg::engine::AudioEngine& engine) noexcept
    : m_engine(engine)
{
}

void JuceAudioCallbackAdapter::prepare(const double sampleRate, const std::size_t maxBlockSize) noexcept
{
    m_sampleRate = sampleRate;
    m_synthLeft.resize(maxBlockSize, 0.0f);
    m_synthRight.resize(maxBlockSize, 0.0f);
    (void) m_liveSynth.prepare(sampleRate, maxBlockSize, 2);
}

void JuceAudioCallbackAdapter::triggerLiveSynthNote(const std::uint8_t pitch,
                                                   const std::uint8_t velocity,
                                                   const bool isNoteOn) noexcept
{
    if (isNoteOn)
    {
        const float vel = std::clamp(static_cast<float>(velocity) / 127.0f, 0.01f, 1.0f);
        m_liveSynth.noteOn(0, pitch, vel);
    }
    else
    {
        m_liveSynth.noteOff(0, pitch);
    }
}

void JuceAudioCallbackAdapter::process(float* const* outputChannels,
                                      const std::size_t channelCount,
                                      const std::size_t frameCount) noexcept
{
    m_engine.process({outputChannels, channelCount, frameCount}, {}, {});

    if (m_liveSynth.activeVoiceCount() > 0 && outputChannels != nullptr && frameCount > 0)
    {
        const auto frames = std::min(frameCount, m_synthLeft.size());
        std::fill_n(m_synthLeft.begin(), frames, 0.0f);
        std::fill_n(m_synthRight.begin(), frames, 0.0f);

        std::array<float*, 2> synthChannels {m_synthLeft.data(), m_synthRight.data()};
        engine::ProcessContext synthCtx {
            .audio = {synthChannels.data(), 2, frames},
            .midi = {},
            .sampleRate = m_sampleRate
        };
        m_liveSynth.process(synthCtx);

        if (channelCount >= 1 && outputChannels[0] != nullptr)
        {
            for (std::size_t i = 0; i < frames; ++i)
                outputChannels[0][i] += m_synthLeft[i];
        }
        if (channelCount >= 2 && outputChannels[1] != nullptr)
        {
            for (std::size_t i = 0; i < frames; ++i)
                outputChannels[1][i] += m_synthRight[i];
        }
    }
}

void JuceAudioCallbackAdapter::process(const float* const* inputChannels,
                                      const std::size_t inputChannelCount,
                                      float* const* outputChannels,
                                      const std::size_t outputChannelCount,
                                      const std::size_t frameCount) noexcept
{
    m_engine.process(
        dawg::engine::AudioInputBufferView {inputChannels, inputChannelCount, frameCount},
        dawg::engine::AudioBufferView {outputChannels, outputChannelCount, frameCount},
        {},
        {});

    if (m_liveSynth.activeVoiceCount() > 0 && outputChannels != nullptr && frameCount > 0)
    {
        const auto frames = std::min(frameCount, m_synthLeft.size());
        std::fill_n(m_synthLeft.begin(), frames, 0.0f);
        std::fill_n(m_synthRight.begin(), frames, 0.0f);

        std::array<float*, 2> synthChannels {m_synthLeft.data(), m_synthRight.data()};
        engine::ProcessContext synthCtx {
            .audio = {synthChannels.data(), 2, frames},
            .midi = {},
            .sampleRate = m_sampleRate
        };
        m_liveSynth.process(synthCtx);

        if (outputChannelCount >= 1 && outputChannels[0] != nullptr)
        {
            for (std::size_t i = 0; i < frames; ++i)
                outputChannels[0][i] += m_synthLeft[i];
        }
        if (outputChannelCount >= 2 && outputChannels[1] != nullptr)
        {
            for (std::size_t i = 0; i < frames; ++i)
                outputChannels[1][i] += m_synthRight[i];
        }
    }
}

} // namespace dawg::platform::juce
