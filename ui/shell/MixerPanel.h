#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace dawg::ui {

class ChannelStripComponent final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;

    explicit ChannelStripComponent(CommandHandler handler);

    void updateTrack(const project::Track& track);
    void paint(juce::Graphics& g) override;
    void resized() override;

    [[nodiscard]] project::TrackId trackId() const noexcept { return m_trackId; }

private:
    void sendGainPan();

    CommandHandler m_handler;
    project::TrackId m_trackId {0};
    bool m_updatingFromModel {false};

    juce::Label m_nameLabel;
    juce::Slider m_panKnob;
    juce::Slider m_fader;
    juce::TextButton m_muteButton {"M"};
    juce::TextButton m_soloButton {"S"};
    juce::TextButton m_phaseButton {"Ø"};
    juce::TextButton m_armButton {"REC"};
    juce::TextButton m_eqButton {"EQ"};
    juce::TextButton m_compButton {"COMP"};
    juce::TextButton m_synthButton {"SYNTH"};
    juce::TextButton m_revButton {"REV"};
    juce::TextButton m_delayButton {"DLY"};
    juce::TextButton m_limiterButton {"LIMIT"};
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
    engine::MeterReading m_meter {};
    float m_displayPeakL {0.0f};
    float m_displayPeakR {0.0f};
    float m_displayRmsL {0.0f};
    float m_displayRmsR {0.0f};
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

private:
    CommandHandler m_handler;
    juce::Viewport m_viewport;
    juce::Component m_tracksContainer;
    std::vector<std::unique_ptr<ChannelStripComponent>> m_strips;
    MasterStripComponent m_masterStrip;
    ModulatorRackComponent m_modulatorRack;
    juce::TextButton m_toggleModulatorsBtn {"Macros & LFO"};
    bool m_showModulators {true};
};

} // namespace dawg::ui
