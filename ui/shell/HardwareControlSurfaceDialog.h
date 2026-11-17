#pragma once

#include <dawg/engine/HardwareControlSurfaceManager.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace dawg::ui {

class HardwareControlSurfaceComponent final : public juce::Component, private juce::Timer
{
public:
    explicit HardwareControlSurfaceComponent(engine::HardwareControlSurfaceManager& manager);
    ~HardwareControlSurfaceComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshMidiPorts();
    void applySettings();

    engine::HardwareControlSurfaceManager& m_manager;

    juce::Label m_titleLabel;
    juce::ToggleButton m_enableToggle {"Enable Hardware Control Surface"};

    juce::Label m_profileLabel;
    juce::ComboBox m_profileCombo;

    juce::Label m_inputPortLabel;
    juce::ComboBox m_inputPortCombo;

    juce::Label m_outputPortLabel;
    juce::ComboBox m_outputPortCombo;

    juce::Label m_pickupModeLabel;
    juce::ComboBox m_pickupModeCombo;

    juce::TextButton m_autoDetectButton {"Auto-Detect Device"};
    juce::TextButton m_pingButton {"Ping Hardware LEDs / Faders"};
    juce::Label m_statusLabel;
};

class HardwareControlSurfaceWindow final : public juce::DocumentWindow
{
public:
    explicit HardwareControlSurfaceWindow(engine::HardwareControlSurfaceManager& manager);
    ~HardwareControlSurfaceWindow() override = default;

    void closeButtonPressed() override;
    static void open(engine::HardwareControlSurfaceManager& manager);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwareControlSurfaceWindow)
};

} // namespace dawg::ui
