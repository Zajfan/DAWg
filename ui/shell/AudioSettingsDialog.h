#pragma once

#include <audio/LowLatencyAudioDriver.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace dawg::ui {

class AudioSettingsComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AudioSettingsComponent(juce::AudioDeviceManager& deviceManager,
                                  platform::juce::LowLatencyAudioDriver* driver = nullptr);
    ~AudioSettingsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    juce::AudioDeviceSelectorComponent m_selector;

    // Driver Backend & Low-Latency Controls
    juce::Label m_backendLabel;
    juce::ComboBox m_backendCombo;

    juce::Label m_bufferPresetLabel;
    juce::ComboBox m_bufferPresetCombo;

    // RTL & XRUN Diagnostic HUD
    platform::juce::LowLatencyAudioDriver* m_driver {nullptr};
    platform::juce::LowLatencyAudioDriver m_fallbackDriver;

    juce::Label m_rtlHudTitle;
    juce::Label m_rtlHudValue;
    juce::Label m_rtlDetailsLabel;
    juce::Label m_xrunLabel;
    juce::TextButton m_resetXrunButton {"Reset XRUNs"};

    // Hardware MIDI
    juce::Label m_controllerLabel;
    juce::ComboBox m_controllerProfileCombo;
    juce::ToggleButton m_midiClockOutToggle {"Transmit Master MIDI Clock (24 PPQN / DIN Sync)"};
};

class AudioSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager,
                                platform::juce::LowLatencyAudioDriver* driver = nullptr);
    ~AudioSettingsWindow() override = default;

    void closeButtonPressed() override;

    static void open(juce::AudioDeviceManager& deviceManager,
                     platform::juce::LowLatencyAudioDriver* driver = nullptr);
};

} // namespace dawg::ui
