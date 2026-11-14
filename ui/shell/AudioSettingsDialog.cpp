#include "AudioSettingsDialog.h"

namespace dawg::ui {

AudioSettingsComponent::AudioSettingsComponent(juce::AudioDeviceManager& deviceManager)
    : m_selector(deviceManager,
                 1, 2,  // min/max input channels
                 1, 2,  // min/max output channels
                 true,  // show audio input options
                 true,  // show audio output options
                 true,  // show MIDI input options
                 false) // show MIDI output options
{
    addAndMakeVisible(m_selector);
    setSize(520, 420);
}

void AudioSettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 26, 32));
}

void AudioSettingsComponent::resized()
{
    m_selector.setBounds(getLocalBounds().reduced(12));
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
