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
    int mixerTrack {1};   // 1 to 64
    std::string samplePath;
    std::array<bool, 64> steps {};
    std::array<std::uint8_t, 64> velocities {};
};

class ChannelRackView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
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
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // File drag and drop target (drop audio samples directly onto channel rows)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void commitPatternToTimeline();
    void clearAllSteps();
    void addChannel(const std::string& name, std::uint8_t rootPitch, int mixerTrack);
    void setNumSteps(int steps);

    [[nodiscard]] int currentPlayStep() const noexcept { return m_lastPlayedStep; }
    [[nodiscard]] int numSteps() const noexcept { return m_numSteps; }

private:
    void timerCallback() override;

    void drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawChannelRow(juce::Graphics& g, int channelIndex, juce::Rectangle<int> bounds);
    void drawStepButton(juce::Graphics& g, int channelIndex, int stepIndex, juce::Rectangle<int> bounds, bool isActive, bool isPlayhead);
    void drawBottomBar(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int getStepAt(juce::Point<int> pos, int& outChannelIndex) const noexcept;
    void triggerStepAudition(int channelIndex, int stepIndex);
    void showChannelContextMenu(int channelIndex);

    CommandHandler m_commandHandler;
    AuditionCallback m_auditionCallback;
    EditorViewModel m_model;

    std::vector<ChannelRackRow> m_channels;
    int m_selectedChannel {0};
    int m_lastPlayedStep {-1};
    int m_numSteps {32}; // Default 32 steps (matching FL Studio Channel Rack)
    int m_scrollOffsetSteps {0};
    bool m_dragPaintingState {true};

    enum class DragControl { none, pan, volume, mixerTrack };
    DragControl m_activeDragControl {DragControl::none};
    int m_dragChannelIndex {-1};
    juce::Point<int> m_dragStartPos;
    float m_dragStartValue {0.0f};

    // Header controls
    juce::ComboBox m_filterCombo;
    juce::Slider m_swingSlider;
    juce::Label m_swingLabel;
    juce::ComboBox m_stepCountCombo;
    juce::TextButton m_commitButton {"Commit (Timeline)"};
    juce::TextButton m_clearButton {"Clear"};

    // Bottom bar controls
    juce::TextButton m_addChannelButton {"+"};

    int m_headerHeight {34};
    int m_bottomBarHeight {26};
    int m_rowHeight {28};
    int m_leftControlsWidth {246};
    int m_stepWidth {20};
    int m_stepSpacing {3};
};

} // namespace dawg::ui
