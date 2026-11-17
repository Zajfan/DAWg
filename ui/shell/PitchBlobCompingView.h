#pragma once

#include "EditorViewModel.h"

#include <dawg/project/TakeLaneComping.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace dawg::ui {

// Professional Melodyne-Style Pitch-Blob & Take Comping Lane Component
// Renders take lanes with zero-crossing swipe selection handles,
// continuous vocal pitch trajectories, and interactive draggable syllable blobs.
class PitchBlobCompingView final : public juce::Component, private juce::Timer
{
public:
    using NoteModifiedCallback = std::function<void(std::size_t blobIdx, int semiShift, float centsOffset)>;

    PitchBlobCompingView();
    ~PitchBlobCompingView() override;

    void setViewModel(const EditorViewModel& model);
    void setNoteModifiedCallback(NoteModifiedCallback cb) noexcept { m_noteCallback = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    [[nodiscard]] project::PitchBlobEditor& pitchEditor() noexcept { return m_pitchEditor; }
    [[nodiscard]] const project::PitchBlobEditor& pitchEditor() const noexcept { return m_pitchEditor; }

private:
    void timerCallback() override;

    void drawPianoRuler(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTakeLanes(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawPitchBlobCanvas(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int findBlobAt(juce::Point<int> pos, juce::Rectangle<int> canvasBounds) const noexcept;
    [[nodiscard]] float midiNoteToY(float midiPitch, juce::Rectangle<int> canvasBounds) const noexcept;
    [[nodiscard]] float yToMidiNote(float y, juce::Rectangle<int> canvasBounds) const noexcept;

    EditorViewModel m_model;
    NoteModifiedCallback m_noteCallback;

    project::PitchBlobEditor m_pitchEditor;
    project::TakeTrackCompManager m_compManager;

    // View Options
    bool m_showTakeLanes {true};
    bool m_snapToScale {true};
    project::MusicalScaleType m_scaleType {project::MusicalScaleType::major};
    std::uint8_t m_rootNote {60}; // Middle C

    // UI Buttons & Combos
    juce::ComboBox m_scaleCombo;
    juce::TextButton m_snapButton {"Snap Scale [✓]"};
    juce::TextButton m_toggleLanesButton {"Take Lanes [▼]"};
    juce::TextButton m_flattenCompButton {"Flatten Comp"};
    juce::Slider m_formantSlider;
    juce::Label m_formantLabel {"Formant", "Formant Shift: 0 st"};

    // Mouse Interaction State
    int m_selectedBlobIdx {-1};
    int m_draggedBlobIdx {-1};
    juce::Point<int> m_dragStartPos;
    float m_dragStartPitch {60.0f};

    // Layout constants
    int m_pianoRulerWidth {44};
    int m_takeLanesHeight {140};
    int m_topBarHeight {36};

    // Simulated waveform & pitch trajectory cache
    std::vector<float> m_mockWaveform;
    std::vector<project::PitchTrajectoryPoint> m_mockTrajectory;
};

} // namespace dawg::ui
