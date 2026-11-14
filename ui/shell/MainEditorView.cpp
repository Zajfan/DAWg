#include "MainEditorView.h"

#include <utility>

namespace dawg::ui {

MainEditorView::MainEditorView(EditorViewModel model,
                               CommandHandler commandHandler,
                               TransportCallbacks transportCallbacks,
                               media::WaveformCache* waveformCache)
    : m_model(std::move(model))
    , m_commandHandler(commandHandler)
    , m_transportCallbacks(transportCallbacks)
    , m_transportBar({
        .commandHandler = m_commandHandler,
        .importWav = m_transportCallbacks.importWav,
        .play = m_transportCallbacks.play,
        .stop = m_transportCallbacks.stop,
        .returnToStart = m_transportCallbacks.returnToStart,
        .record = m_transportCallbacks.record,
        .undo = m_transportCallbacks.undo,
        .redo = m_transportCallbacks.redo
    })
    , m_timelineView(m_commandHandler, waveformCache, m_transportCallbacks.seek)
    , m_mixerPanel(m_commandHandler)
{
    addAndMakeVisible(m_transportBar);
    addAndMakeVisible(m_timelineView);
    addAndMakeVisible(m_mixerPanel);

    setViewModel(m_model);
}

void MainEditorView::setViewModel(EditorViewModel model)
{
    m_model = std::move(model);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model.project = std::move(snapshot);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
}

void MainEditorView::setWaveformCache(media::WaveformCache* cache)
{
    m_timelineView.setWaveformCache(cache);
}

void MainEditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 25, 29));
}

void MainEditorView::resized()
{
    auto area = getLocalBounds();

    // Top: Transport Bar (height 44 px)
    m_transportBar.setBounds(area.removeFromTop(44));

    // Bottom: Mixer Console (height 190 px)
    constexpr int mixerHeight = 190;
    m_mixerPanel.setBounds(area.removeFromBottom(mixerHeight));

    // Center: Multi-track Timeline
    m_timelineView.setBounds(area);
}

} // namespace dawg::ui
