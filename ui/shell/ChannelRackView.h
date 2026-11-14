#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct ChannelRackRow
{
    std::string name;
    std::uint8_t rootPitch {36};
    bool muted {false};
    float pan {0.0f};     // -1.0 to 1.0
    float volume {0.8f};  // 0.0 to 1.0
    int mixerTrack {1};   // 1 to 16
    std::array<bool, 16> steps {};
    std::array<std::uint8_t, 16> velocities {};
};

class ChannelRackView final : public juce::Component, private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;

    explicit ChannelRackView(CommandHandler handler = nullptr, AuditionCallback auditionCb = nullptr);
    ~ChannelRackView() override;

    void setViewModel(const EditorViewModel& model);
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCallback = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    void commitPatternToTimeline();
    void clearAllSteps();
    void addChannel(const std::string& name, std::uint8_t rootPitch, int mixerTrack);

    [[nodiscard]] int currentPlayStep() const noexcept { return m_lastPlayedStep; }

private:
    void timerCallback() override;

    void drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawChannelRow(juce::Graphics& g, int channelIndex, juce::Rectangle<int> bounds);
    void drawStepButton(juce::Graphics& g, int channelIndex, int stepIndex, juce::Rectangle<int> bounds, bool isActive, bool isPlayhead);

    [[nodiscard]] int getStepAt(juce::Point<int> pos, int& outChannelIndex) const noexcept;
    void triggerStepAudition(int channelIndex, int stepIndex);

    CommandHandler m_commandHandler;
    AuditionCallback m_auditionCallback;
    EditorViewModel m_model;

    std::vector<ChannelRackRow> m_channels;
    int m_selectedChannel {0};
    int m_lastPlayedStep {-1};
    bool m_dragPaintingState {true};

    // Header controls
    juce::ComboBox m_filterCombo;
    juce::Slider m_swingSlider;
    juce::Label m_swingLabel;
    juce::TextButton m_commitButton {"Commit Pattern (Timeline)"};
    juce::TextButton m_clearButton {"Clear"};
    juce::TextButton m_addChannelButton {"+ Add Sound"};

    int m_headerHeight {34};
    int m_rowHeight {28};
    int m_leftControlsWidth {240};
    int m_stepWidth {24};
    int m_stepSpacing {3};
};

} // namespace dawg::ui
