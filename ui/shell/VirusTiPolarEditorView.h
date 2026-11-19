#pragma once

#include <dawg/engine/dsp/VirusTiPolarSynthNode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace dawg::ui {

/// Bespoke Hardware-Emulation GUI for the Access Virus TI Polar Virtual Analog Synth.
/// Pure vector-drawn arctic white aluminum chassis, white wooden end-cheeks,
/// glowing ruby-red dot-matrix LCD, and tactile knurled rotary encoders.
class VirusTiPolarEditorView final : public juce::Component, private juce::Timer
{
public:
    VirusTiPolarEditorView();
    ~VirusTiPolarEditorView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    [[nodiscard]] engine::VirusTiPolarSynthNode& synthNode() noexcept { return m_synth; }

private:
    void timerCallback() override;

    struct KnurledKnob
    {
        std::string label;
        float value {0.5f};
        float minVal {0.0f};
        float maxVal {1.0f};
        std::string unit {"%"};
        juce::Rectangle<int> bounds;
    };

    struct LedButton
    {
        std::string label;
        bool state {false};
        juce::Rectangle<int> bounds;
    };

    void drawKnob(juce::Graphics& g, const KnurledKnob& knob, bool isHovered, bool isActive);
    void drawLedButton(juce::Graphics& g, const LedButton& btn);
    void drawLcdScreen(juce::Graphics& g, juce::Rectangle<int> bounds);

    engine::VirusTiPolarSynthNode m_synth;
    std::vector<engine::VirusTiPatch> m_presets;
    int m_currentPresetIdx {0};

    // Rotary Knobs:
    // 0: Hypersaw Density, 1: Detune, 2: Spread, 3: Osc Mix
    // 4: Filter 1 Cutoff, 5: Filter 1 Reso, 6: Filter 2 Cutoff, 7: Balance
    // 8: Drive, 9: Amp Attack, 10: Amp Decay, 11: Amp Release
    // 12: Delay Time, 13: Delay Mix, 14: Reverb Decay, 15: Reverb Mix
    std::array<KnurledKnob, 16> m_knobs;
    int m_activeKnobIdx {-1};
    juce::Point<int> m_dragStartPos;
    float m_dragStartValue {0.0f};

    // LED Toggle Buttons:
    // 0: Hypersaw, 1: Sync, 2: Distort, 3: Delay, 4: Reverb, 5: Arp
    std::array<LedButton, 6> m_ledButtons;

    juce::TextButton m_prevPresetBtn {"<"};
    juce::TextButton m_nextPresetBtn {">"};
    juce::ComboBox m_presetSelector;

    float m_scopePhase {0.0f};
    std::array<float, 64> m_scopeBuffer {};
};

class VirusTiPolarWindow final : public juce::DocumentWindow
{
public:
    static void open();
    static void close();
    [[nodiscard]] static bool isOpen() noexcept;

    explicit VirusTiPolarWindow(std::function<void()> onClose = nullptr);
    ~VirusTiPolarWindow() override = default;

    void closeButtonPressed() override;

    [[nodiscard]] VirusTiPolarEditorView* editor() noexcept { return m_editor.get(); }

private:
    std::unique_ptr<VirusTiPolarEditorView> m_editor;
    std::function<void()> m_onClose;
    static std::unique_ptr<VirusTiPolarWindow> s_instance;
};

} // namespace dawg::ui
