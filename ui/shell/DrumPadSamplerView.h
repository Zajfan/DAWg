#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/dsp/DrumPadSamplerNode.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace dawg::ui {

class DrumPadSamplerView final : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;

    explicit DrumPadSamplerView(CommandHandler handler = nullptr, AuditionCallback auditionCb = nullptr);
    ~DrumPadSamplerView() override;

    void setViewModel(const EditorViewModel& model);
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCallback = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // File Drag and Drop: dropping sample onto pad loads sample into that pad
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void triggerPad(std::size_t padIdx, float velocity);
    void sliceAudioFileAcrossPads(const juce::File& file);
    void loadSampleFileIntoPad(const juce::File& file, std::size_t padIdx);

    [[nodiscard]] engine::DrumPadSamplerNode& samplerNode() noexcept { return m_sampler; }

private:
    void timerCallback() override;

    void drawPadGrid(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawWaveformEditor(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawPad(juce::Graphics& g, std::size_t padIdx, juce::Rectangle<int> bounds);

    [[nodiscard]] int getPadAt(juce::Point<int> pos) const noexcept;
    void updateControlsForSelectedPad();

    CommandHandler m_commandHandler;
    AuditionCallback m_auditionCallback;
    EditorViewModel m_model;
    engine::DrumPadSamplerNode m_sampler;
    std::unique_ptr<juce::FileChooser> m_fileChooser;

    std::size_t m_selectedPad {0};
    std::array<float, 16> m_padLitGlow {};

    // Header & Actions
    juce::Label m_titleLabel;
    juce::TextButton m_sliceButton {"Auto-Slice to 16 Pads"};
    juce::TextButton m_loadSampleButton {"Load Sample..."};

    // Parameter Controls for Selected Pad
    juce::Label m_padTitleLabel;
    juce::Slider m_pitchSlider;
    juce::Label m_pitchLabel;
    juce::ToggleButton m_reverseToggle {"Reverse"};
    juce::ComboBox m_chokeCombo;
    juce::Label m_chokeLabel;

    // ADSR
    juce::Slider m_attackSlider;
    juce::Label m_attackLabel;
    juce::Slider m_decaySlider;
    juce::Label m_decayLabel;
    juce::Slider m_sustainSlider;
    juce::Label m_sustainLabel;
    juce::Slider m_releaseSlider;
    juce::Label m_releaseLabel;

    // Filter
    juce::Slider m_filterCutoffSlider;
    juce::Label m_filterCutoffLabel;
    juce::Slider m_filterResoSlider;
    juce::Label m_filterResoLabel;

    // Waveform trimming drag state
    bool m_draggingStartHandle {false};
    bool m_draggingEndHandle {false};
    juce::Rectangle<int> m_waveformBounds;
};

} // namespace dawg::ui
