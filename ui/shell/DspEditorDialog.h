#pragma once

#include <dawg/project/ProjectTypes.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dawg::ui {

enum class DspEditorKind : std::uint8_t
{
    parametricEq,
    compressor,
    synth,
    reverb,
    delay
};

class DspEditorComponent final : public juce::Component, private juce::Timer
{
public:
    explicit DspEditorComponent(DspEditorKind kind, std::string title = "");
    ~DspEditorComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    [[nodiscard]] DspEditorKind kind() const noexcept { return m_kind; }

private:
    void timerCallback() override;

    // Drawing helpers
    void paintEq(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintCompressor(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintSynth(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintReverb(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintDelay(juce::Graphics& g, juce::Rectangle<int> bounds);

    DspEditorKind m_kind;
    std::string m_title;

    juce::Label m_titleLabel;
    juce::ToggleButton m_bypassButton {"Bypass"};

    // Parametric EQ state
    struct EqBand
    {
        float freqHz {1000.0f};
        float gainDb {0.0f};
        float q {1.0f};
    };
    std::array<EqBand, 4> m_eqBands {{
        {100.0f, 0.0f, 0.707f},   // Low Shelf
        {500.0f, 0.0f, 1.414f},   // Bell 1
        {2500.0f, 0.0f, 1.414f},  // Bell 2
        {8000.0f, 0.0f, 0.707f}   // High Shelf
    }};
    int m_activeEqBand {-1};

    // Compressor state
    float m_compThresholdDb {-18.0f};
    float m_compRatio {4.0f};
    float m_compKneeDb {3.0f};
    float m_compAttackMs {10.0f};
    float m_compReleaseMs {100.0f};
    float m_compMakeupDb {0.0f};
    bool m_compSidechainEnabled {false};
    float m_compGainReductionDb {0.0f};
    float m_simPhase {0.0f};

    // Synth state
    int m_synthOsc1Wave {0}; // 0: Saw, 1: Pulse, 2: Tri, 3: Sine
    int m_synthOsc2Wave {1};
    float m_synthAttack {0.01f};
    float m_synthDecay {0.2f};
    float m_synthSustain {0.7f};
    float m_synthRelease {0.3f};
    int m_synthActiveEnvelopePoint {-1};
    int m_synthSelectedPreset {0};

    // Reverb state
    float m_revRoomSize {0.75f};
    float m_revDecaySeconds {2.5f};
    float m_revDamping {0.35f};
    float m_revStereoWidth {1.0f};
    float m_revWetDry {0.4f};

    // Delay state
    int m_delayMode {1}; // 0: Dual Mono, 1: Ping Pong
    int m_delaySyncDiv {2}; // 0: Free ms, 1: 1/4, 2: 1/8, 3: 1/8T, 4: 1/8D, 5: 1/16
    float m_delayTimeMsL {375.0f};
    float m_delayTimeMsR {500.0f};
    float m_delayFeedback {0.45f};
    float m_delayLowCutHz {120.0f};
    float m_delayHighCutHz {7500.0f};
    float m_delayWetDry {0.35f};

    // Spectrum simulation buffer for visualizer
    std::array<float, 64> m_spectrumMagnitudes {};
};

class DspEditorWindow final : public juce::DocumentWindow
{
public:
    DspEditorWindow(const juce::String& name, DspEditorKind kind, std::function<void()> onClose = nullptr);
    ~DspEditorWindow() override = default;

    void closeButtonPressed() override;

    static void open(DspEditorKind kind, const std::string& trackName, std::function<void()> onClose = nullptr);

private:
    std::function<void()> m_onClose;
};

} // namespace dawg::ui
