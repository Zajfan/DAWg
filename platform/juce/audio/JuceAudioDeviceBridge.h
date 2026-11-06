#pragma once

#include "JuceAudioCallbackAdapter.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>

namespace dawg::platform::juce {

// Owns the device manager, borrows the engine. Construct, initialise, restart,
// and shut down on the message thread. The engine must outlive this bridge;
// its lifecycle and processing are driven exclusively by this device manager.
class JuceAudioDeviceBridge final : private ::juce::AudioIODeviceCallback
{
public:
    explicit JuceAudioDeviceBridge(dawg::engine::AudioEngine& engine);
    ~JuceAudioDeviceBridge() override;

    JuceAudioDeviceBridge(const JuceAudioDeviceBridge&) = delete;
    JuceAudioDeviceBridge& operator=(const JuceAudioDeviceBridge&) = delete;

    [[nodiscard]] ::juce::Result initialise();
    void shutdown() noexcept;

    [[nodiscard]] bool isDeviceReady() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] int blockSize() const noexcept;

private:
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels, int numSamples,
        const ::juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(::juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    dawg::engine::AudioEngine& m_engine;
    JuceAudioCallbackAdapter m_callbackAdapter;
    ::juce::AudioDeviceManager m_deviceManager;

    std::atomic<bool> m_deviceReady {false};
    std::atomic<double> m_sampleRate {0.0};
    std::atomic<int> m_blockSize {0};
};

} // namespace dawg::platform::juce
