#pragma once

#include <dawg/engine/dsp/OmniFmSynthNode.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

class OmniFmEditorView final : public juce::Component, private juce::Timer
{
public:
    explicit OmniFmEditorView(engine::OmniFmSynthNode* node = nullptr);
    ~OmniFmEditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void setSynthNode(engine::OmniFmSynthNode* node) noexcept { m_node = node; }

private:
    void timerCallback() override;

    struct OpGui
    {
        juce::Rectangle<int> box {};
        juce::Rectangle<int> btnPower {};
        juce::Rectangle<int> btnWave {};
        juce::Rectangle<int> knobRatio {};
        juce::Rectangle<int> knobLevel {};
        juce::Rectangle<int> knobDetune {};
        juce::Rectangle<int> knobAttack {};
        juce::Rectangle<int> knobDecay {};
    };

    void drawKnob(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& label, float value, bool isHot);
    void drawAlgorithmDisplay(juce::Graphics& g, const juce::Rectangle<int>& bounds);

    engine::OmniFmSynthNode* m_node {nullptr};
    std::array<OpGui, 6> m_opGuis {};

    juce::Rectangle<int> m_btnAlgoPrev {};
    juce::Rectangle<int> m_btnAlgoNext {};
    juce::Rectangle<int> m_knobFeedback {};

    juce::Rectangle<int> m_btnPresetPrev {};
    juce::Rectangle<int> m_btnPresetNext {};

    int m_dragOpIdx {-1};
    int m_dragParamIdx {-1};
    int m_dragStartY {0};
    float m_dragStartVal {0.0f};

    std::size_t m_currentPreset {0};
};

class OmniFmWindow final : public juce::DocumentWindow
{
public:
    static void open(engine::OmniFmSynthNode* node = nullptr);
    static void close();
    static bool isOpen();

    OmniFmWindow(const juce::String& name, engine::OmniFmSynthNode* node);
    ~OmniFmWindow() override;

    void closeButtonPressed() override;

private:
    static std::unique_ptr<OmniFmWindow> s_instance;
    std::unique_ptr<engine::OmniFmSynthNode> m_ownedNode;
    OmniFmEditorView m_view;
};

} // namespace dawg::ui
