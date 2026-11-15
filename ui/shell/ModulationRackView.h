#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/dsp/ModulatorRack.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace dawg::ui {

class ModulationRackView final : public juce::Component, private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;

    explicit ModulationRackView(CommandHandler handler = nullptr);
    ~ModulationRackView() override;

    void setViewModel(const EditorViewModel& model);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    [[nodiscard]] engine::ModulatorRack& modulatorRack() noexcept { return m_rack; }

private:
    void timerCallback() override;

    void drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawMacroSection(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawLfoSection(juce::Graphics& g, juce::Rectangle<int> bounds, std::size_t lfoIdx);
    void drawEnvelopeFollowerSection(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawStepModulatorSection(juce::Graphics& g, juce::Rectangle<int> bounds);

    CommandHandler m_commandHandler;
    EditorViewModel m_model;
    engine::ModulatorRack m_rack;

    // Macro Knobs (8)
    struct MacroGui
    {
        juce::Slider slider;
        juce::Label label;
    };
    std::array<MacroGui, 8> m_macroKnobs;

    // LFO Controls
    juce::ComboBox m_lfo1WaveCombo;
    juce::ComboBox m_lfo1RateCombo;
    juce::Slider m_lfo1DepthSlider;

    juce::ComboBox m_lfo2WaveCombo;
    juce::ComboBox m_lfo2RateCombo;
    juce::Slider m_lfo2DepthSlider;

    // Envelope Follower Controls
    juce::Slider m_envAttackSlider;
    juce::Slider m_envReleaseSlider;
    juce::Slider m_envGainSlider;

    // Step Modulator Controls
    juce::ComboBox m_stepRateCombo;
    int m_draggingStepIdx {-1};
    juce::Rectangle<int> m_stepBounds;

    int m_headerHeight {34};
};

} // namespace dawg::ui
