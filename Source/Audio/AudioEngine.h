#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

namespace daw::audio
{
class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine() = default;
    ~AudioEngine() override;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] juce::Result initialise();
    void shutdown() noexcept;

    [[nodiscard]] bool isDeviceReady() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] int getBlockSize() const noexcept;

private:
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    std::atomic<bool> deviceReady { false };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> blockSize { 0 };
};
} // namespace daw::audio
