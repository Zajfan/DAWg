#pragma once

#include <dawg/engine/dsp/TnhcSynthNode.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine { class AudioEngine; }

namespace dawg::platform::juce {

// Borrows engine and output storage. The host serializes lifecycle calls with
// processing and keeps the engine alive until callbacks have stopped.
class JuceAudioCallbackAdapter final
{
public:
    explicit JuceAudioCallbackAdapter(dawg::engine::AudioEngine& engine) noexcept;

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void triggerLiveSynthNote(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn) noexcept;

    void process(float* const* outputChannels, std::size_t channelCount,
                 std::size_t frameCount) noexcept;
    void process(const float* const* inputChannels, std::size_t inputChannelCount,
                 float* const* outputChannels, std::size_t outputChannelCount,
                 std::size_t frameCount) noexcept;

private:
    dawg::engine::AudioEngine& m_engine;
    dawg::engine::TnhcSynthNode m_liveSynth;
    std::vector<float> m_synthLeft;
    std::vector<float> m_synthRight;
    double m_sampleRate {48000.0};
};

} // namespace dawg::platform::juce
