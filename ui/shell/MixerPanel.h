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
};

class MasterStripComponent final : public juce::Component
{
public:
    MasterStripComponent();

    void updateMeter(const engine::MeterReading& meter);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> barBounds, float peak, float rms);

    juce::Label m_nameLabel {"Master", "MASTER"};
    engine::MeterReading m_meter {};
    float m_displayPeakL {0.0f};
    float m_displayPeakR {0.0f};
    float m_displayRmsL {0.0f};
    float m_displayRmsR {0.0f};
};

class MixerPanel final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;

    explicit MixerPanel(CommandHandler handler);

    void setViewModel(const EditorViewModel& model);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    CommandHandler m_handler;
    juce::Viewport m_viewport;
    juce::Component m_tracksContainer;
    std::vector<std::unique_ptr<ChannelStripComponent>> m_strips;
    MasterStripComponent m_masterStrip;
};

} // namespace dawg::ui
