#include "MainComponent.h"

namespace dawg::app {

MainComponent::MainComponent()
{
    setSize(900, 560);

    m_titleLabel.setText("DAWg", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(m_titleLabel);

    m_statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_statusLabel);

    m_restartButton.onClick = [this]
    {
        const auto result = m_audioAdapter.initialise();

        if (result.failed())
            m_statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                                  juce::dontSendNotification);
        else
            refreshAudioStatus();
    };
    addAndMakeVisible(m_restartButton);

    const auto result = m_audioAdapter.initialise();

    if (result.failed())
        m_statusLabel.setText("Audio device error: " + result.getErrorMessage(),
                              juce::dontSendNotification);
    else
        refreshAudioStatus();

    startTimerHz(2);
}

MainComponent::~MainComponent()
{
    stopTimer();
    m_audioAdapter.shutdown();
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(25, 28, 34));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(28);

    m_titleLabel.setBounds(area.removeFromTop(52));
    area.removeFromTop(20);
    m_statusLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(12);
    m_restartButton.setBounds(area.removeFromTop(36).removeFromLeft(200));
}

void MainComponent::timerCallback()
{
    refreshAudioStatus();
}

void MainComponent::refreshAudioStatus()
{
    if (!m_audioAdapter.isDeviceReady())
    {
        m_statusLabel.setText("No audio device is ready. Check the device settings and retry.",
                              juce::dontSendNotification);
        return;
    }

    const auto rate  = juce::String(m_audioAdapter.getSampleRate(), 0);
    const auto block = juce::String(m_audioAdapter.getBlockSize());
    m_statusLabel.setText("Audio device ready  |  " + rate + " Hz  |  block " + block + " samples",
                          juce::dontSendNotification);
}

} // namespace dawg::app
