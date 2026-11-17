#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/dsp/MasteringMeterNode.h>
#include <dawg/engine/dsp/StudioMonitorCalibrationNode.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace dawg::ui {

class MasteringSuiteView final : public juce::Component, private juce::Timer
{
public:
    MasteringSuiteView();
    ~MasteringSuiteView() override = default;

    void setViewModel(const EditorViewModel& model);
    [[nodiscard]] engine::dsp::MasteringMeterNode& meterNode() noexcept { return m_meterNode; }
    [[nodiscard]] engine::dsp::StudioMonitorCalibrationNode& calibrationNode() noexcept { return m_calibrationNode; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    void drawLoudnessRadar(juce::Graphics& g, juce::Rectangle<float> area);
    void drawLissajousGoniometer(juce::Graphics& g, juce::Rectangle<float> area);
    void drawTruePeakMeter(juce::Graphics& g, juce::Rectangle<float> area);
    void drawSpectrumWaterfall(juce::Graphics& g, juce::Rectangle<float> area);
    void drawReadoutCard(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const juce::String& value, const juce::String& subtext, juce::Colour accentCol);

    engine::dsp::MasteringMeterNode m_meterNode;
    engine::dsp::StudioMonitorCalibrationNode m_calibrationNode;
    engine::dsp::MasteringTelemetry m_latestTelemetry {};

    // UI Controls
    juce::ComboBox m_targetProfileCombo;
    juce::TextButton m_resetStatsButton {"Reset Stats"};
    juce::ComboBox m_monitorTargetCombo;
    juce::TextButton m_runCalibrationSweepButton {"Sweep Calibration"};
    juce::ToggleButton m_calibrationActiveToggle {"Room Correction"};

    // Cached telemetry buffers
    std::array<engine::dsp::MasteringMeterNode::VectorPoint, engine::dsp::MasteringMeterNode::goniometerBufferSize> m_goniometerPoints {};
    std::array<float, engine::dsp::MasteringMeterNode::spectrumBands> m_spectrumMagnitudes {};
    std::array<float, engine::dsp::MasteringMeterNode::radarHistorySize> m_radarHistory {};

    float m_radarAngleRad {0.0f};
};

} // namespace dawg::ui
