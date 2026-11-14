#pragma once

#include "EditorViewModel.h"
#include "MixerPanel.h"
#include "TimelineView.h"
#include "TransportBar.h"

#include <dawg/media/WaveformCache.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawg::ui {

class MainEditorView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using ActionHandler = std::function<void()>;
    using SeekCallback = std::function<void(std::int64_t samplePosition)>;

    struct TransportCallbacks
    {
        ActionHandler importWav;
        ActionHandler play;
        ActionHandler stop;
        ActionHandler returnToStart;
        ActionHandler record;
        ActionHandler undo;
        ActionHandler redo;
        SeekCallback seek;
    };

    MainEditorView(EditorViewModel model,
                   CommandHandler commandHandler,
                   TransportCallbacks transportCallbacks = {},
                   media::WaveformCache* waveformCache = nullptr);

    void setViewModel(EditorViewModel model);
    void setSnapshot(project::ProjectSnapshot snapshot);
    void setWaveformCache(media::WaveformCache* cache);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    EditorViewModel m_model;
    CommandHandler m_commandHandler;
    TransportCallbacks m_transportCallbacks;

    TransportBar m_transportBar;
    TimelineView m_timelineView;
    MixerPanel m_mixerPanel;
};

} // namespace dawg::ui
