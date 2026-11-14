#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawg::ui {

class MainEditorView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using ActionHandler = std::function<void()>;

    struct TransportCallbacks
    {
        ActionHandler importWav;
        ActionHandler play;
        ActionHandler stop;
        ActionHandler returnToStart;
    };

    MainEditorView(EditorViewModel model,
                   CommandHandler commandHandler,
                   TransportCallbacks transportCallbacks = {});
    void setViewModel(EditorViewModel model);
    void setSnapshot(project::ProjectSnapshot snapshot);
    void resized() override;

private:
    void refreshLabels();

    EditorViewModel m_model;
    CommandHandler m_commandHandler;
    TransportCallbacks m_transportCallbacks;
    juce::Label m_projectLabel;
    juce::Label m_transportLabel;
    juce::TextButton m_addTrackButton {"Add audio track"};
    juce::TextButton m_importWavButton {"Import WAV"};
    juce::TextButton m_playButton {"Play"};
    juce::TextButton m_stopButton {"Stop"};
    juce::TextButton m_returnToStartButton {"Return to start"};
};

} // namespace dawg::ui
