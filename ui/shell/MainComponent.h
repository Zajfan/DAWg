#pragma once

#include "MainEditorView.h"

#include <functional>
#include <string>

namespace dawg::media { class WaveformCache; }

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

    MainComponent(EditorViewModel,
                  MainEditorView::CommandHandler,
                  MainEditorView::TransportCallbacks,
                  AudioStatus,
                  RestartAudio,
                  media::WaveformCache* waveformCache = nullptr);

    MainComponent(EditorViewModel,
                  MainEditorView::CommandHandler,
                  MainEditorView::TransportCallbacks,
                  MainEditorView::MenuCallbacks,
                  AudioStatus,
                  RestartAudio,
                  media::WaveformCache* waveformCache = nullptr);

    void setViewModel(EditorViewModel);
    void setProjectSnapshot(project::ProjectSnapshot);
    void setAudioStatus(const AudioStatus&);
    void setWaveformCache(media::WaveformCache* cache);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Label m_statusLabel;
    juce::TextButton m_restartButton {"Restart audio device"};
    MainEditorView m_editor;
};

} // namespace dawg::ui
