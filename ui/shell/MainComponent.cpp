#include "MainComponent.h"

#include <utility>

namespace dawg::ui {

MainComponent::MainComponent(project::ProjectSnapshot snapshot,
                             MainEditorView::CommandHandler commandHandler,
                             AudioStatus audioStatus, RestartAudio restartAudio)
    : m_editor(std::move(snapshot), std::move(commandHandler))
{
    setSize(900, 560);
    m_titleLabel.setText("DAWg", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    addAndMakeVisible(m_titleLabel);
    m_statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(m_statusLabel);
    m_restartButton.onClick = std::move(restartAudio);
    addAndMakeVisible(m_restartButton);
    addAndMakeVisible(m_editor);
    setAudioStatus(audioStatus);
}

void MainComponent::setProjectSnapshot(project::ProjectSnapshot snapshot)
{
    m_editor.setSnapshot(std::move(snapshot));
}

void MainComponent::setAudioStatus(const AudioStatus& status)
{
    if (! status.error.empty())
        m_statusLabel.setText("Audio device error: " + juce::String(status.error),
                              juce::dontSendNotification);
    else if (! status.ready)
        m_statusLabel.setText("No audio device is ready. Check the device settings and retry.",
                              juce::dontSendNotification);
    else
        m_statusLabel.setText("Audio device ready  |  " + juce::String(status.sampleRate, 0)
                                  + " Hz  |  block " + juce::String(status.blockSize) + " samples",
                              juce::dontSendNotification);
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
    area.removeFromTop(24);
    m_editor.setBounds(area);
}

} // namespace dawg::ui
