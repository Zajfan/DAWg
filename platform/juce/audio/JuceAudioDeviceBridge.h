#pragma once

#include "JuceAudioCallbackAdapter.h"

#include <dawg/engine/MidiControllerManager.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>

namespace dawg::platform::juce {

// Owns the device manager, borrows the engine. Construct, initialise, restart,
// and shut down on the message thread. The engine must outlive this bridge;
// its lifecycle and processing are driven exclusively by this device manager.
class JuceAudioDeviceBridge final : private ::juce::AudioIODeviceCallback,
                                    private ::juce::MidiInputCallback
{
public:
    using MidiControlCallback = std::function<void(const dawg::engine::ProcessedMidiControl&)>;

    explicit JuceAudioDeviceBridge(dawg::engine::AudioEngine& engine);
    ~JuceAudioDeviceBridge() override;

    JuceAudioDeviceBridge(const JuceAudioDeviceBridge&) = delete;
    JuceAudioDeviceBridge& operator=(const JuceAudioDeviceBridge&) = delete;

    [[nodiscard]] ::juce::Result initialise();
    void shutdown() noexcept;

    void setMidiControlCallback(MidiControlCallback cb) { m_midiControlCallback = std::move(cb); }
    [[nodiscard]] dawg::engine::MidiControllerManager& controllerManager() noexcept { return m_controllerManager; }

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

    void handleIncomingMidiMessage(::juce::MidiInput* source,
                                   const ::juce::MidiMessage& message) override;

    dawg::engine::AudioEngine& m_engine;
    JuceAudioCallbackAdapter m_callbackAdapter;
    ::juce::AudioDeviceManager m_deviceManager;
    dawg::engine::MidiControllerManager m_controllerManager;
    MidiControlCallback m_midiControlCallback;

    std::atomic<bool> m_deviceReady {false};
    std::atomic<double> m_sampleRate {0.0};
    std::atomic<int> m_blockSize {0};
};

} // namespace dawg::platform::juce
