#pragma once

#include <cstddef>

namespace dawg::engine { class AudioEngine; }

namespace dawg::platform::juce {

// Borrows engine and output storage. The host serializes lifecycle calls with
// processing and keeps the engine alive until callbacks have stopped.
class JuceAudioCallbackAdapter final
{
public:
    explicit JuceAudioCallbackAdapter(dawg::engine::AudioEngine& engine) noexcept;
    void process(float* const* outputChannels, std::size_t channelCount,
                 std::size_t frameCount) noexcept;

private:
    dawg::engine::AudioEngine& m_engine;
};

} // namespace dawg::platform::juce
