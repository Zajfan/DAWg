#pragma once

#include <dawg/engine/dsp/AnalogLadderRingModSynthNode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

/// Authentic Minimoog Model D Bespoke Vintage Hardware GUI.
/// Crafted with tipped-up matte-black control panel, rich walnut end cheeks,
/// fluted aluminum pointer knobs, and authentic transistor ladder filter & feedback loop controls.
class ModelDEditorView final : public juce::Component, private juce::Timer
{
public:
    explicit ModelDEditorView(engine::dsp::AnalogLadderRingModSynthNode* node = nullptr);
    ~ModelDEditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    void setSynthNode(engine::dsp::AnalogLadderRingModSynthNode* node) noexcept { m_node = node; }

private:
    void timerCallback() override;

    struct VintageKnob
    {
        std::string label;
        std::string section;
        float value {0.5f};
        float minVal {0.0f};
        float maxVal {1.0f};
        std::string unit;
        juce::Rectangle<int> bounds {};
        juce::Colour capColour {juce::Colour::fromRGB(210, 215, 225)};
    };

    struct RockerSwitch
    {
        std::string label;
        bool state {false};
        juce::Rectangle<int> bounds {};
        juce::Colour colour {juce::Colour::fromRGB(45, 120, 220)};
    };

    void drawKnob(juce::Graphics& g, const VintageKnob& knob, bool isHovered, bool isActive);
    void drawRocker(juce::Graphics& g, const RockerSwitch& rocker);
    void drawSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& title);

    engine::dsp::AnalogLadderRingModSynthNode* m_node {nullptr};
    std::vector<VintageKnob> m_knobs;
    std::vector<RockerSwitch> m_rockers;

    int m_activeKnobIdx {-1};
    int m_dragStartY {0};
    float m_dragStartVal {0.0f};

    juce::Rectangle<int> m_btnPresetPrev {};
    juce::Rectangle<int> m_btnPresetNext {};
    std::size_t m_currentPreset {0};
};

class ModelDWindow final : public juce::DocumentWindow
{
public:
    static void open(engine::dsp::AnalogLadderRingModSynthNode* node = nullptr);
    static void close();
    static bool isOpen();

    ModelDWindow(const juce::String& name, engine::dsp::AnalogLadderRingModSynthNode* node);
    ~ModelDWindow() override;

    void closeButtonPressed() override;

private:
    static std::unique_ptr<ModelDWindow> s_instance;
    std::unique_ptr<engine::dsp::AnalogLadderRingModSynthNode> m_ownedNode;
    ModelDEditorView m_view;
};

} // namespace dawg::ui
