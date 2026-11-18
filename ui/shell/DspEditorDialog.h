#pragma once

#include <dawg/plugins/PluginPresetManager.h>
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
    delay,
    resonanceSuppressor,
    multibandImager,
    linearPhaseEq,
    buchlaComplexSynth
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
    void paintResonanceSuppressor(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintMultibandImager(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintLinearPhaseEq(juce::Graphics& g, juce::Rectangle<int> bounds);
    void paintBuchlaComplexSynth(juce::Graphics& g, juce::Rectangle<int> bounds);

    DspEditorKind m_kind;
    std::string m_title;
    std::string m_pluginId;

    juce::Label m_titleLabel;
    juce::ToggleButton m_bypassButton {"Bypass"};

    // Interactive Preset Browser Ribbon
    std::unique_ptr<plugins::PluginPresetManager> m_presetManager;
    juce::TextEditor m_searchPill;
    juce::ComboBox m_presetCombo;
    std::array<juce::TextButton, 5> m_starButtons;
    juce::TextButton m_abToggleBtn {"[ A ]"};
    juce::TextButton m_copyAbBtn {"A->B"};
    juce::TextButton m_savePresetBtn {"Save"};
    int m_currentRating {5};
    bool m_isSlotA {true};

    void populatePresets(const std::string& query = "");
    void selectPreset(int index);
    void updateStars(int rating);

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

    // Resonance Suppressor Visualizer State
    struct ResoCutNode { float freqHz; float depthDb; float q; bool active; };
    std::array<ResoCutNode, 5> m_resoNodes {{
        { 250.0f, -8.0f, 2.0f, true },
        { 850.0f, -14.0f, 3.5f, true },
        { 2800.0f, -18.0f, 4.0f, true },
        { 5500.0f, -12.0f, 3.0f, true },
        { 9200.0f, -9.0f, 2.2f, true }
    }};
    int m_activeResoNode {-1};
    std::array<float, 128> m_resoFftBins {};
    std::array<float, 128> m_resoErbMask {};
    float m_resoDepth {0.65f};
    float m_resoSharpness {1.8f};

    // Multiband Imager Visualizer State
    std::array<float, 3> m_imagerCrossovers {{120.0f, 1500.0f, 6000.0f}};
    std::array<float, 4> m_imagerWidths {{0.0f, 1.0f, 1.5f, 1.8f}};
    std::array<bool, 4> m_imagerSolos {{false, false, false, false}};
    std::array<bool, 4> m_imagerMutes {{false, false, false, false}};
    int m_activeCrossoverDivider {-1};
    int m_activeImagerBand {-1};
    float m_imagerCorrelation {0.85f};
    std::array<juce::Point<float>, 32> m_lissajousPoints {};

    // Linear-Phase Mastering EQ Visualizer State
    struct MasterEqBand { float freqHz; float gainDb; float q; float dynReductionDb; };
    std::array<MasterEqBand, 6> m_masterEqBands {{
        {45.0f, 1.0f, 0.7f, 0.0f},
        {120.0f, -0.5f, 1.0f, -1.2f},
        {450.0f, 0.0f, 1.4f, 0.0f},
        {2500.0f, -1.0f, 2.0f, -2.5f},
        {6000.0f, 1.5f, 1.2f, 0.0f},
        {14000.0f, 2.0f, 0.7f, 0.0f}
    }};
    int m_activeMasterEqBand {-1};

    // Buchla Complex Synth Visualizer State
    float m_wavefoldDrive {1.8f};
    float m_wavefoldSymmetry {0.1f};
    float m_vactrolDecayEnvelope {0.8f};
    std::array<juce::Point<float>, 64> m_oscilloscopeTrace {};
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
