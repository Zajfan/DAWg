#pragma once

#include <dawg/engine/dsp/TripleOscillatorSynthNode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

/// Bespoke GUI for "3xOsc Studio Edition" (Triple Oscillator Classic Synth).
/// Features 3 distinct morphable oscillator strips with AM/FM cross-modulation matrix,
/// stereo phase offset, and analog-modeled multi-mode filter.
class TripleOscillatorEditorView final : public juce::Component, private juce::Timer
{
public:
    explicit TripleOscillatorEditorView(engine::dsp::TripleOscillatorSynthNode* node = nullptr);
    ~TripleOscillatorEditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void setSynthNode(engine::dsp::TripleOscillatorSynthNode* node) noexcept { m_node = node; }

private:
    void timerCallback() override;

    struct ModernKnob
    {
        std::string label;
        float value {0.5f};
        float minVal {0.0f};
        float maxVal {1.0f};
        std::string unit;
        juce::Rectangle<int> bounds {};
        juce::Colour trackColour {juce::Colour::fromRGB(0, 220, 240)};
    };

    struct WaveButton
    {
        engine::dsp::TripleOscWaveform wave;
        juce::Rectangle<int> bounds {};
    };

    void drawKnob(juce::Graphics& g, const ModernKnob& knob, bool isHovered, bool isActive);
    void drawOscStrip(juce::Graphics& g, int oscIdx, juce::Rectangle<int> bounds);

    engine::dsp::TripleOscillatorSynthNode* m_node {nullptr};
    std::vector<ModernKnob> m_knobs;

    std::array<std::array<WaveButton, 5>, 3> m_waveButtons {};
    std::array<juce::Rectangle<int>, 2> m_modButtonsOsc2 {};
    std::array<juce::Rectangle<int>, 2> m_modButtonsOsc3 {};
    std::array<juce::Rectangle<int>, 4> m_filterTypeButtons {};

    int m_activeKnobIdx {-1};
    int m_dragStartY {0};
    float m_dragStartVal {0.0f};

    juce::Rectangle<int> m_btnPresetPrev {};
    juce::Rectangle<int> m_btnPresetNext {};
    std::size_t m_currentPreset {0};
};

class TripleOscillatorWindow final : public juce::DocumentWindow
{
public:
    static void open(engine::dsp::TripleOscillatorSynthNode* node = nullptr);
    static void close();
    static bool isOpen();

    TripleOscillatorWindow(const juce::String& name, engine::dsp::TripleOscillatorSynthNode* node);
    ~TripleOscillatorWindow() override;

    void closeButtonPressed() override;

private:
    static std::unique_ptr<TripleOscillatorWindow> s_instance;
    std::unique_ptr<engine::dsp::TripleOscillatorSynthNode> m_ownedNode;
    TripleOscillatorEditorView m_view;
};

} // namespace dawg::ui
