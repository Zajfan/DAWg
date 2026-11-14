#include "JuceAudioCallbackAdapter.h"

#include <dawg/engine/AudioEngine.h>

namespace dawg::platform::juce {

JuceAudioCallbackAdapter::JuceAudioCallbackAdapter(dawg::engine::AudioEngine& engine) noexcept
    : m_engine(engine)
{
}

void JuceAudioCallbackAdapter::process(float* const* outputChannels,
                                      const std::size_t channelCount,
                                      const std::size_t frameCount) noexcept
{
    // The engine clears every supplied non-null channel, including unused
    // outputs, and borrows the pointer array only for this block.
    m_engine.process({outputChannels, channelCount, frameCount}, {}, {});
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
}

} // namespace dawg::platform::juce
