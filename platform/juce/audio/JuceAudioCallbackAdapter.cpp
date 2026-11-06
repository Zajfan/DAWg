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

} // namespace dawg::platform::juce
