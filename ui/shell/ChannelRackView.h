#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct ChannelPatternData
{
    std::array<bool, 64> steps {};
    std::array<std::uint8_t, 64> velocities {};
    std::array<int8_t, 64> pitchOffsets {}; // -24 to +24 semitones
    std::array<int8_t, 64> panOffsets {};   // -50 to +50
};

struct ChannelRackPattern
{
    std::string name {"Pattern 1"};
    std::vector<ChannelPatternData> channelData {};
};

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
    std::array<int8_t, 64> pitchOffsets {};
    std::array<int8_t, 64> panOffsets {};
};

class ChannelRackView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;
    using OpenPianoRollCallback = std::function<void()>;

    explicit ChannelRackView(CommandHandler handler = nullptr, AuditionCallback auditionCb = nullptr);
    ~ChannelRackView() override;

    void setViewModel(const EditorViewModel& model);
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCallback = std::move(cb); }
    void setOpenPianoRollCallback(OpenPianoRollCallback cb) noexcept { m_openPianoRollCb = std::move(cb); }

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
    void setSelectedTrackId(project::TrackId trkId) { m_selectedTrackId = trkId; }

    // Multi-pattern management
    void switchPattern(int patternIndex);
    void addNewPattern();
    [[nodiscard]] int currentPatternIndex() const noexcept { return m_currentPatternIndex; }
    [[nodiscard]] const std::string& currentPatternName() const noexcept;

    [[nodiscard]] int currentPlayStep() const noexcept { return m_lastPlayedStep; }
    [[nodiscard]] int numSteps() const noexcept { return m_numSteps; }

    enum class GraphMode : std::uint8_t
    {
        velocity,
        pitch,
        pan
    };

private:
    void timerCallback() override;

    void drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawChannelRow(juce::Graphics& g, int channelIndex, juce::Rectangle<int> bounds);
    void drawStepButton(juce::Graphics& g, int channelIndex, int stepIndex, juce::Rectangle<int> bounds, bool isActive, bool isPlayhead);
    void drawGraphEditor(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawBottomBar(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] int getStepAt(juce::Point<int> pos, int& outChannelIndex) const noexcept;
    void triggerStepAudition(int channelIndex, int stepIndex);
    void showChannelContextMenu(int channelIndex);
    void syncCurrentPatternToBank();
    void loadPatternFromBank(int patternIndex);

    CommandHandler m_commandHandler;
    AuditionCallback m_auditionCallback;
    OpenPianoRollCallback m_openPianoRollCb;
    EditorViewModel m_model;

    std::vector<ChannelRackRow> m_channels;
    std::vector<ChannelRackPattern> m_patterns;
    int m_currentPatternIndex {0};

    int m_selectedChannel {0};
    int m_lastPlayedStep {-1};
    int m_numSteps {32}; // Default 32 steps (matching FL Studio Channel Rack)
    int m_scrollOffsetSteps {0};
    bool m_dragPaintingState {true};

    bool m_graphEditorOpen {false};
    GraphMode m_graphMode {GraphMode::velocity};

    enum class DragControl { none, pan, volume, mixerTrack, graphEditor };
    DragControl m_activeDragControl {DragControl::none};
    int m_dragChannelIndex {-1};
    juce::Point<int> m_dragStartPos;
    float m_dragStartValue {0.0f};

    // Header controls
    juce::ComboBox m_patternCombo;
    juce::TextButton m_addPatternButton {"+ Pat"};
    juce::ComboBox m_filterCombo;
    juce::Slider m_swingSlider;
    juce::Label m_swingLabel;
    juce::ComboBox m_stepCountCombo;
    juce::TextButton m_graphToggleBtn {"Graph"};
    juce::ComboBox m_graphModeCombo;
    juce::TextButton m_commitButton {"Commit (Timeline)"};
    juce::TextButton m_clearButton {"Clear"};

    // Bottom bar controls
    juce::TextButton m_addChannelButton {"+"};

    std::optional<project::TrackId> m_selectedTrackId;
    int m_headerHeight {34};
    int m_bottomBarHeight {26};
    int m_rowHeight {28};
    int m_leftControlsWidth {246};
    int m_stepWidth {20};
    int m_stepSpacing {3};
    int m_graphEditorHeight {70};
};

} // namespace dawg::ui
