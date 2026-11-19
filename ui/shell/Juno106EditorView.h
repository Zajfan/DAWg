#pragma once

#include <dawg/engine/dsp/Juno106SynthNode.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

class Juno106EditorView final : public juce::Component, private juce::Timer
{
public:
    explicit Juno106EditorView(engine::Juno106SynthNode* node = nullptr);
    ~Juno106EditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void setSynthNode(engine::Juno106SynthNode* node) noexcept { m_node = node; }

private:
    void timerCallback() override;

    struct FaderDef
    {
        std::string id;
        std::string label;
        std::string section;
        float value {0.5f};
        juce::Rectangle<int> bounds {};
        juce::Colour capColour {};
    };

    void drawFader(juce::Graphics& g, const FaderDef& fader, bool isHot);
    void drawButton(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& text, bool active);

    engine::Juno106SynthNode* m_node {nullptr};
    std::vector<FaderDef> m_faders;
    int m_activeFaderIdx {-1};
    int m_dragStartY {0};
    float m_dragStartVal {0.0f};

    // Pushbuttons
    juce::Rectangle<int> m_btnSaw {};
    juce::Rectangle<int> m_btnPulse {};
    juce::Rectangle<int> m_btnChorusOff {};
    juce::Rectangle<int> m_btnChorusI {};
    juce::Rectangle<int> m_btnChorusII {};
    juce::Rectangle<int> m_btnChorusBoth {};

    juce::Rectangle<int> m_btnRange16 {};
    juce::Rectangle<int> m_btnRange8 {};
    juce::Rectangle<int> m_btnRange4 {};

    juce::Rectangle<int> m_btnHpf0 {};
    juce::Rectangle<int> m_btnHpf1 {};
    juce::Rectangle<int> m_btnHpf2 {};
    juce::Rectangle<int> m_btnHpf3 {};

    juce::Rectangle<int> m_btnPresetPrev {};
    juce::Rectangle<int> m_btnPresetNext {};

    std::size_t m_currentPreset {0};
};

class Juno106Window final : public juce::DocumentWindow
{
public:
    static void open(engine::Juno106SynthNode* node = nullptr);
    static void close();
    static bool isOpen();

    Juno106Window(const juce::String& name, engine::Juno106SynthNode* node);
    ~Juno106Window() override;

    void closeButtonPressed() override;

private:
    static std::unique_ptr<Juno106Window> s_instance;
    std::unique_ptr<engine::Juno106SynthNode> m_ownedNode;
    Juno106EditorView m_view;
};

} // namespace dawg::ui
