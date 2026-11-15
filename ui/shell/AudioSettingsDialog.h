#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace dawg::ui {

class AudioSettingsComponent final : public juce::Component
{
public:
    explicit AudioSettingsComponent(juce::AudioDeviceManager& deviceManager);
    ~AudioSettingsComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioDeviceSelectorComponent m_selector;
    juce::Label m_controllerLabel;
    juce::ComboBox m_controllerProfileCombo;
    juce::ToggleButton m_midiClockOutToggle {"Transmit Master MIDI Clock (24 PPQN / DIN Sync)"};
};

class AudioSettingsWindow final : public juce::DocumentWindow
{
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager);
    ~AudioSettingsWindow() override = default;

    void closeButtonPressed() override;

    static void open(juce::AudioDeviceManager& deviceManager);
};

} // namespace dawg::ui
