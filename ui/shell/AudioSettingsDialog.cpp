#include "AudioSettingsDialog.h"

namespace dawg::ui {

AudioSettingsComponent::AudioSettingsComponent(juce::AudioDeviceManager& deviceManager)
    : m_selector(deviceManager,
                 1, 8,  // min/max input channels (support multi-track recording interfaces)
                 1, 8,  // min/max output channels
                 true,  // show audio input options
                 true,  // show audio output options
                 true,  // show MIDI input options
                 true)  // show MIDI output options (for hardware MIDI Clock out)
{
    addAndMakeVisible(m_selector);

    m_controllerLabel.setText("Hardware Control Surface Profile:", juce::dontSendNotification);
    m_controllerLabel.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    m_controllerLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 205, 215));
    addAndMakeVisible(m_controllerLabel);

    m_controllerProfileCombo.addItem("Generic MIDI Keyboard / MCU Surface", 1);
    m_controllerProfileCombo.addItem("Novation Launchpad (RGB Clip Launcher Grid)", 2);
    m_controllerProfileCombo.addItem("AKAI MPK Mini (8 Pads + 8 Knobs Auto-Mapped)", 3);
    m_controllerProfileCombo.addItem("Arturia KeyLab / Minilab", 4);
    m_controllerProfileCombo.addItem("Behringer X-Touch (Motorized Faders + V-Pots)", 5);
    m_controllerProfileCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_controllerProfileCombo);

    m_midiClockOutToggle.setToggleState(true, juce::dontSendNotification);
    m_midiClockOutToggle.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(100, 210, 240));
    addAndMakeVisible(m_midiClockOutToggle);

    setSize(560, 520);
}

void AudioSettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 26, 32));

    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawHorizontalLine(getHeight() - 88, 12.0f, static_cast<float>(getWidth() - 12));
}

void AudioSettingsComponent::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    auto bottomSection = bounds.removeFromBottom(72);

    m_selector.setBounds(bounds);

    m_midiClockOutToggle.setBounds(bottomSection.removeFromTop(24));
    auto ctrlRow = bottomSection.removeFromBottom(28);
    m_controllerLabel.setBounds(ctrlRow.removeFromLeft(220));
    m_controllerProfileCombo.setBounds(ctrlRow);
}

AudioSettingsWindow::AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
    : DocumentWindow("Audio Hardware & MIDI Preferences",
                     juce::Colour::fromRGB(24, 26, 32),
                     DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AudioSettingsComponent(deviceManager), true);
    setResizable(true, false);
    centreWithSize(540, 460);
    setVisible(true);
}

void AudioSettingsWindow::closeButtonPressed()
{
    delete this;
}

void AudioSettingsWindow::open(juce::AudioDeviceManager& deviceManager)
{
    new AudioSettingsWindow(deviceManager);
}

} // namespace dawg::ui
