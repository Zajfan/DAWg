#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace dawg::ui {

class ChannelStripComponent final : public juce::Component, private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;

    explicit ChannelStripComponent(CommandHandler handler);
    ~ChannelStripComponent() override;

    void updateTrack(const project::Track& track);
    void setGainReduction(float grDb) noexcept { m_gainReductionDb = grDb; repaint(); }
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    [[nodiscard]] project::TrackId trackId() const noexcept { return m_trackId; }
    [[nodiscard]] int vcaGroup() const noexcept { return std::max(0, m_vcaCombo.getSelectedId() - 1); }
    [[nodiscard]] float gainLinear() const noexcept { return static_cast<float>(m_fader.getValue()); }
    [[nodiscard]] float pan() const noexcept { return static_cast<float>(m_panKnob.getValue()); }
    [[nodiscard]] bool isMuted() const noexcept { return m_muteButton.getToggleState(); }
    [[nodiscard]] bool isSolo() const noexcept { return m_soloButton.getToggleState(); }
    [[nodiscard]] bool isPhaseInvert() const noexcept { return m_phaseButton.getToggleState(); }
    [[nodiscard]] bool isSidechainListen() const noexcept { return m_sidechainListen; }

private:
    void sendGainPan();
    void timerCallback() override;
    void drawMacroOverlay(juce::Graphics& g, juce::Rectangle<int> bounds);
    void openInsertEditor(std::size_t slotIdx);

    CommandHandler m_handler;
    project::TrackId m_trackId {0};
    bool m_updatingFromModel {false};
    float m_gainReductionDb {0.0f};
    int m_preampModel {0};
    bool m_autoGainComp {true};

    std::array<float, 8> m_macros {{0.5f, 0.4f, 0.6f, 0.3f, 0.7f, 0.5f, 0.2f, 0.8f}};
    float m_macroAnimPhase {0.0f};
    int m_draggedMacroIdx {-1};
    juce::Point<int> m_dragStartPos;
    float m_dragStartVal {0.0f};
    juce::Rectangle<int> m_macroBounds;

    juce::Label m_nameLabel;
    juce::Slider m_panKnob;
    juce::Slider m_fader;
    juce::ComboBox m_panLawCombo;
    juce::ComboBox m_vcaCombo;
    juce::TextButton m_muteButton {"M"};
    juce::TextButton m_soloButton {"S"};
    juce::TextButton m_phaseButton {"POL"};
    juce::TextButton m_armButton {"REC"};

    juce::TextButton m_addInsertBtn {"+ FX"};
    std::array<juce::TextButton, 2> m_slotButtons;
    std::array<std::string, 2> m_slotNames {{"EQ", "COMP"}};
    juce::TextButton m_macroToggleBtn {"[M]"};
    bool m_showMacros {false};
    bool m_sidechainListen {false};
};

class MasterStripComponent final : public juce::Component
{
public:
    using LimiterCallback = std::function<void(bool)>;

    explicit MasterStripComponent(LimiterCallback limiterCb = {});

    void updateMeter(const engine::MeterReading& meter, bool limiterActive);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> barBounds, float peak, float rms);

    LimiterCallback m_limiterCallback;
    juce::Label m_nameLabel {"Master", "MASTER"};
    juce::TextButton m_limiterButton {"LIMITER"};

    juce::Label m_cueLabel {"Cue", "CUE MIX"};
    juce::Slider m_cueAFader;
    juce::Label m_cueALabel {"CueA", "Cue A"};
    juce::Slider m_cueBFader;
    juce::Label m_cueBLabel {"CueB", "Cue B"};

    engine::MeterReading m_meter {};
    float m_displayPeakL {0.0f};
    float m_displayPeakR {0.0f};
    float m_displayRmsL {0.0f};
    float m_displayRmsR {0.0f};
    float m_correlation {0.95f};
    float m_lufs {-14.0f};
    bool m_limiterActive {false};
};

class ModulatorRackComponent final : public juce::Component, private juce::Timer
{
public:
    ModulatorRackComponent();
    ~ModulatorRackComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct MacroKnob
    {
        std::string name;
        juce::Slider slider;
        juce::Label label;
        juce::Colour color;
    };

    std::array<MacroKnob, 8> m_macros;
    juce::ComboBox m_lfoShapeCombo;
    juce::ComboBox m_lfoRateCombo;
    float m_lfoPhase {0.0f};
};

class MixerPanel final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using LimiterCallback = std::function<void(bool)>;

    explicit MixerPanel(CommandHandler handler, LimiterCallback limiterCb = {});

    void setViewModel(const EditorViewModel& model);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void toggleModulatorRack();
    [[nodiscard]] bool isModulatorRackVisible() const noexcept { return m_showModulators; }

    void switchScene(bool toSceneB);
    void copyActiveSceneToOpposite();
    void setVcaSpill(int vcaGroup);

private:
    struct ChannelMixSnapshot
    {
        project::TrackId trackId {0};
        float gainLinear {1.0f};
        float pan {0.0f};
        bool muted {false};
        bool solo {false};
        bool phaseInvert {false};
    };

    struct MixerScene
    {
        std::vector<ChannelMixSnapshot> channels;
    };

    void captureCurrentScene(MixerScene& target);
    void applyScene(const MixerScene& scene);

    CommandHandler m_handler;
    juce::Viewport m_viewport;
    juce::Component m_tracksContainer;
    std::vector<std::unique_ptr<ChannelStripComponent>> m_strips;
    MasterStripComponent m_masterStrip;
    ModulatorRackComponent m_modulatorRack;
    juce::TextButton m_toggleModulatorsBtn {"Macros & LFO"};
    bool m_showModulators {true};

    // A/B Scene Snapshots
    MixerScene m_sceneA;
    MixerScene m_sceneB;
    bool m_activeSceneIsB {false};
    juce::TextButton m_sceneAButton {"Mix A"};
    juce::TextButton m_sceneBButton {"Mix B"};
    juce::TextButton m_copySceneButton {"A -> B"};

    // VCA Spill Controls
    int m_vcaSpillGroup {0}; // 0 = all tracks
    juce::TextButton m_vcaAllButton {"ALL"};
    std::array<juce::TextButton, 8> m_vcaSpillButtons;
    juce::Label m_spillStatusLabel;
};

} // namespace dawg::ui
