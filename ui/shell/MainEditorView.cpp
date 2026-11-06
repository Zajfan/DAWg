#include "MainEditorView.h"

#include <string>
#include <utility>

namespace dawg::ui {

MainEditorView::MainEditorView(project::ProjectSnapshot snapshot, CommandHandler commandHandler)
    : m_commandHandler(std::move(commandHandler))
{
    addAndMakeVisible(m_projectLabel);
    addAndMakeVisible(m_addTrackButton);
    m_addTrackButton.onClick = [this] {
        if (m_commandHandler)
            m_commandHandler(project::AddAudioTrack {"Audio " + std::to_string(m_model.trackCount() + 1)});
    };
    setSnapshot(std::move(snapshot));
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model = {std::move(snapshot)};
    m_projectLabel.setText(juce::String(m_model.project.name) + "  |  "
                              + juce::String(static_cast<juce::int64>(m_model.trackCount()))
                              + " audio tracks", juce::dontSendNotification);
}

void MainEditorView::resized()
{
    auto area = getLocalBounds();
    m_projectLabel.setBounds(area.removeFromTop(36));
    area.removeFromTop(12);
    m_addTrackButton.setBounds(area.removeFromTop(36).removeFromLeft(200));
}

} // namespace dawg::ui
