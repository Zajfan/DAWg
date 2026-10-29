#include "MainComponent.h"

namespace daw::app
{
MainComponent::MainComponent()
{
    setSize(900, 560);

    titleLabel.setText("DAWg", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    restartButton.onClick = [this]
    {
        const auto result = audioEngine.initialise();

        if (result.failed())
            statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                                juce::dontSendNotification);
        else
            refreshAudioStatus();
    };
    addAndMakeVisible(restartButton);

    const auto result = audioEngine.initialise();

    if (result.failed())
        statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                            juce::dontSendNotification);
    else
        refreshAudioStatus();

    startTimerHz(2);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine.shutdown();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(25, 28, 34));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(28);

    titleLabel.setBounds(area.removeFromTop(52));
    area.removeFromTop(20);
    statusLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(12);
    restartButton.setBounds(area.removeFromTop(36).removeFromLeft(200));
}

void MainComponent::timerCallback()
{
    refreshAudioStatus();
}

void MainComponent::refreshAudioStatus()
{
    if (!audioEngine.isDeviceReady())
    {
        statusLabel.setText("No audio device is ready. Check the device settings and retry.",
                            juce::dontSendNotification);
        return;
    }

    const auto rate = juce::String(audioEngine.getSampleRate(), 0);
    const auto block = juce::String(audioEngine.getBlockSize());
    statusLabel.setText("Audio device ready  |  " + rate + " Hz  |  block " + block + " samples",
                        juce::dontSendNotification);
}
} // namespace daw::app
