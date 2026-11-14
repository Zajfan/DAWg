#include "MainEditorView.h"

#include <string>
#include <utility>

namespace dawg::ui {

MainEditorView::MainEditorView(EditorViewModel model,
                               CommandHandler commandHandler,
                               TransportCallbacks transportCallbacks)
    : m_commandHandler(std::move(commandHandler))
    , m_transportCallbacks(std::move(transportCallbacks))
{
    addAndMakeVisible(m_projectLabel);
    addAndMakeVisible(m_transportLabel);
    addAndMakeVisible(m_addTrackButton);
    addAndMakeVisible(m_importWavButton);
    addAndMakeVisible(m_playButton);
    addAndMakeVisible(m_stopButton);
    addAndMakeVisible(m_returnToStartButton);

    m_addTrackButton.onClick = [this] {
        if (m_commandHandler)
            m_commandHandler(project::AddAudioTrack {"Audio " + std::to_string(m_model.trackCount() + 1)});
    };
    m_importWavButton.onClick = [this] {
        if (m_transportCallbacks.importWav)
            m_transportCallbacks.importWav();
    };
    m_playButton.onClick = [this] {
        if (m_transportCallbacks.play)
            m_transportCallbacks.play();
    };
    m_stopButton.onClick = [this] {
        if (m_transportCallbacks.stop)
            m_transportCallbacks.stop();
    };
    m_returnToStartButton.onClick = [this] {
        if (m_transportCallbacks.returnToStart)
            m_transportCallbacks.returnToStart();
    };

    setViewModel(std::move(model));
}

void MainEditorView::setViewModel(EditorViewModel model)
{
    m_model = std::move(model);
    refreshLabels();
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model.project = std::move(snapshot);
    refreshLabels();
}

void MainEditorView::refreshLabels()
{
    m_projectLabel.setText(
        juce::String(m_model.project.name) + "  |  "
            + juce::String(static_cast<juce::int64>(m_model.trackCount())) + " audio tracks  |  "
            + (m_model.hasAudioClip
                   ? ("clip " + juce::String(static_cast<juce::int64>(m_model.clipLengthSamples))
                      + " samples")
                   : juce::String("no clip")),
        juce::dontSendNotification);

    juce::String transportText = m_model.playing ? "Playing" : "Stopped";
    transportText << "  |  pos " << juce::String(static_cast<juce::int64>(m_model.playbackPosition))
                  << " samples  |  underruns "
                  << juce::String(static_cast<juce::int64>(m_model.underrunCount));

    if (!m_model.mediaAvailable) {
        transportText << "  |  media unavailable";
        if (!m_model.mediaError.empty())
            transportText << ": " << juce::String(m_model.mediaError);
    }

    m_transportLabel.setText(transportText, juce::dontSendNotification);
}

void MainEditorView::resized()
{
    auto area = getLocalBounds();
    m_projectLabel.setBounds(area.removeFromTop(32));
    area.removeFromTop(6);
    m_transportLabel.setBounds(area.removeFromTop(32));
    area.removeFromTop(12);

    auto buttonRow = area.removeFromTop(36);
    m_addTrackButton.setBounds(buttonRow.removeFromLeft(160));
    buttonRow.removeFromLeft(10);
    m_importWavButton.setBounds(buttonRow.removeFromLeft(130));
    buttonRow.removeFromLeft(10);
    m_playButton.setBounds(buttonRow.removeFromLeft(90));
    buttonRow.removeFromLeft(10);
    m_stopButton.setBounds(buttonRow.removeFromLeft(90));
    buttonRow.removeFromLeft(10);
    m_returnToStartButton.setBounds(buttonRow.removeFromLeft(140));
}

} // namespace dawg::ui
