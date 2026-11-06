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

    MainEditorView(project::ProjectSnapshot snapshot, CommandHandler commandHandler);
    void setSnapshot(project::ProjectSnapshot snapshot);
    void resized() override;

private:
    EditorViewModel m_model;
    CommandHandler m_commandHandler;
    juce::Label m_projectLabel;
    juce::TextButton m_addTrackButton {"Add audio track"};
};

} // namespace dawg::ui
