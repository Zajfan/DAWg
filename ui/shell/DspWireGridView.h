#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/dsp/DspWireGrid.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace dawg::ui {

class DspWireGridView final : public juce::Component, private juce::Timer
{
public:
    DspWireGridView();
    ~DspWireGridView() override = default;

    void setViewModel(const EditorViewModel& model);
    [[nodiscard]] engine::dsp::DspWireGrid& wireGrid() noexcept { return m_grid; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    // Hit-testing helpers
    struct PortHitResult
    {
        bool hit {false};
        std::uint32_t moduleId {0};
        bool isOutput {false};
        std::uint8_t portIdx {0};
        juce::Point<float> centerPos;
    };

    [[nodiscard]] PortHitResult findPortAt(juce::Point<float> pos) const noexcept;
    [[nodiscard]] int findModuleAt(juce::Point<float> pos) const noexcept;
    [[nodiscard]] juce::Point<float> getPortPosition(std::uint32_t moduleId, bool isOutput, std::uint8_t portIdx) const noexcept;

    engine::dsp::DspWireGrid m_grid;

    // Dragging & Interaction State
    int m_draggedModuleIdx {-1};
    juce::Point<float> m_moduleDragOffset;

    bool m_draggingCable {false};
    std::uint32_t m_cableSrcModuleId {0};
    std::uint8_t m_cableSrcPort {0};
    juce::Point<float> m_cableDragCurrentPos;

    float m_animationPhase {0.0f};

    // UI Controls
    juce::ComboBox m_presetCombo;
    juce::TextButton m_addOscButton {"+ Osc"};
    juce::TextButton m_addFilterButton {"+ Filter"};
    juce::TextButton m_addSatButton {"+ Drive"};
    juce::TextButton m_addLfoButton {"+ LFO"};
    juce::TextButton m_addDelayButton {"+ Delay"};
    juce::TextButton m_clearButton {"Clear"};

    // Macro Knobs
    std::array<juce::Slider, 4> m_macroSliders;
    std::array<juce::Label, 4> m_macroLabels;

    // Oscilloscope Probe Cache
    std::array<float, engine::dsp::DspWireGrid::probeBufferSize> m_probeSamples {};
};

} // namespace dawg::ui
