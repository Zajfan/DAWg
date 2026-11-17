#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawg::ui {

class TransportBar final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using ActionHandler = std::function<void()>;

    struct Callbacks
    {
        CommandHandler commandHandler;
        ActionHandler importWav;
        ActionHandler play;
        ActionHandler stop;
        ActionHandler returnToStart;
        ActionHandler record;
        ActionHandler undo;
        ActionHandler redo;
    };

    explicit TransportBar(Callbacks callbacks);

    void setViewModel(const EditorViewModel& model);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void updateButtons();

    Callbacks m_callbacks;
    EditorViewModel m_model;
    bool m_updatingFromModel {false};

    juce::TextButton m_returnToStartButton {"|<"};
    juce::TextButton m_playButton {"Play"};
    juce::TextButton m_stopButton {"Stop"};
    juce::TextButton m_recordButton {"Rec"};

    juce::Label m_bpmLabel {"BPM", "BPM"};
    juce::Slider m_bpmSlider;
    juce::TextButton m_timeSigButton {"4/4"};

    juce::TextButton m_metronomeButton {"Click"};
    juce::TextButton m_loopButton {"Loop"};

    juce::TextButton m_undoButton {"Undo"};
    juce::TextButton m_redoButton {"Redo"};

    juce::TextButton m_preRollButton {"Pre-Roll: OFF"};
    juce::TextButton m_punchButton {"Punch: OFF"};
    juce::Label m_punchRangeBadge;
    juce::TextButton m_latencyCalibrateButton {"Calibrate Latency"};

    std::uint32_t m_preRollBars {0};
    bool m_punchActive {false};
    double m_calibratedLatencySamples {0.0};

    juce::TextButton m_addTrackButton {"+ Track"};
    juce::TextButton m_importWavButton {"Import WAV"};
    juce::Label m_positionLabel;
};

} // namespace dawg::ui
