#pragma once

#include "MainEditorView.h"

#include <functional>
#include <string>

namespace dawg::ui {

struct AudioStatus
{
    bool ready {};
    double sampleRate {};
    int blockSize {};
    std::string error;
};

class MainComponent final : public juce::Component
{
public:
    using RestartAudio = std::function<void()>;

    MainComponent(project::ProjectSnapshot, MainEditorView::CommandHandler,
                  AudioStatus, RestartAudio);

    void setProjectSnapshot(project::ProjectSnapshot);
    void setAudioStatus(const AudioStatus&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Label m_titleLabel;
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton {"Restart audio device"};
    MainEditorView m_editor;
};

} // namespace dawg::ui
