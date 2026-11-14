#include "MainComponent.h"

#include <utility>

namespace dawg::ui {

MainComponent::MainComponent(EditorViewModel model,
                             MainEditorView::CommandHandler commandHandler,
                             MainEditorView::TransportCallbacks transportCallbacks,
                             AudioStatus audioStatus,
                             RestartAudio restartAudio,
                             media::WaveformCache* waveformCache)
    : m_editor(std::move(model), std::move(commandHandler), std::move(transportCallbacks), waveformCache)
{
    setSize(1024, 680);

    m_statusLabel.setJustificationType(juce::Justification::centredLeft);
    m_statusLabel.setFont(juce::FontOptions(11.0f));
    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 155, 170));
    addAndMakeVisible(m_statusLabel);

    m_restartButton.onClick = std::move(restartAudio);
    addAndMakeVisible(m_restartButton);

    addAndMakeVisible(m_editor);
    setAudioStatus(audioStatus);
}

void MainComponent::setViewModel(EditorViewModel model)
{
    m_editor.setViewModel(std::move(model));
}

void MainComponent::setProjectSnapshot(project::ProjectSnapshot snapshot)
{
    m_editor.setSnapshot(std::move(snapshot));
}

void MainComponent::setWaveformCache(media::WaveformCache* cache)
{
    m_editor.setWaveformCache(cache);
}

void MainComponent::setAudioStatus(const AudioStatus& status)
{
    if (! status.error.empty())
    {
        m_statusLabel.setText("Audio error: " + juce::String(status.error),
                              juce::dontSendNotification);
        m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 80, 80));
    }
    else if (! status.ready)
    {
        m_statusLabel.setText("No audio device ready. Check device settings and retry.",
                              juce::dontSendNotification);
        m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 180, 50));
    }
    else
    {
        m_statusLabel.setText("Device ready  |  " + juce::String(status.sampleRate, 0)
                                  + " Hz  |  buffer " + juce::String(status.blockSize) + " smp",
                              juce::dontSendNotification);
        m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 155, 170));
    }
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour::fromRGB(20, 21, 25));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // Compact bottom status bar (height 24 px)
    auto statusBar = area.removeFromBottom(24).reduced(6, 2);
    m_restartButton.setBounds(statusBar.removeFromRight(140));
    m_statusLabel.setBounds(statusBar);

    // Workbench editor takes all the remaining window area
    m_editor.setBounds(area);
}

} // namespace dawg::ui
