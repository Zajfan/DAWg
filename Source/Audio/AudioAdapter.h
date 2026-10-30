#pragma once

#include <dawg/engine/AudioEngine.h>
#include <dawg/engine/AudioBuffer.h>
#include <dawg/engine/Transport.h>
#include <dawg/engine/VoiceScheduler.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace dawg::app::audio {

class AudioAdapter final : private juce::AudioIODeviceCallback
{
public:
    AudioAdapter() = default;
    ~AudioAdapter() override;

    AudioAdapter(const AudioAdapter&)            = delete;
    AudioAdapter& operator=(const AudioAdapter&) = delete;

    [[nodiscard]] juce::Result initialise();
    void shutdown() noexcept;

    [[nodiscard]] bool   isDeviceReady() const noexcept;
    [[nodiscard]] double getSampleRate()  const noexcept;
    [[nodiscard]] int    getBlockSize()   const noexcept;

    // Engine sub-system access for transport commands (play/stop/seek/tempo)
    [[nodiscard]] dawg::engine::Transport&       getTransport()       noexcept;
    [[nodiscard]] dawg::engine::VoiceScheduler&  getVoiceScheduler()  noexcept;

private:
    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const*                        inputChannelData,
        int                                        numInputChannels,
        float* const*                              outputChannelData,
        int                                        numOutputChannels,
        int                                        numSamples,
        const juce::AudioIODeviceCallbackContext&  context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager m_deviceManager;
    dawg::engine::AudioEngine m_engine;

    // Pre-allocated at device start; never touched inside the callback
    std::unique_ptr<dawg::engine::AudioBuffer> m_processingBuffer;

    std::atomic<bool>   m_deviceReady { false };
    std::atomic<double> m_sampleRate  { 0.0 };
    std::atomic<int>    m_blockSize   { 0 };
};

} // namespace dawg::app::audio
