#pragma once

#include "EditorViewModel.h"

#include <dawg/engine/DjCrossfaderEngine.h>
#include <dawg/engine/SessionMatrixEngine.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

struct SessionClipSlot
{
    bool hasClip {false};
    std::string name;
    juce::Colour color {juce::Colour::fromRGB(45, 120, 180)};
    bool isPlaying {false};
    bool isQueued {false};
    int lengthBars {2};
    std::vector<project::MidiNote> notes {};

    // Live Jam & Launch Parameters
    engine::ClipLaunchMode launchMode {engine::ClipLaunchMode::trigger};
    bool isLegato {false};
    float playheadProgress {0.0f}; // 0.0 to 1.0 (for circular progress ring)
    engine::FollowAction followActionA {engine::FollowAction::none};
    engine::FollowAction followActionB {engine::FollowAction::none};
    float followChanceA {1.0f};
    int jumpTargetScene {0};
    int loopCount {1};
    int currentLoop {0};
};

struct SessionSceneInfo
{
    double bpm {120.0};
    std::uint8_t timeSigNumerator {4};
    std::uint8_t timeSigDenominator {4};
    std::string name;
};

class SessionMatrixView final : public juce::Component, private juce::Timer
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;

    explicit SessionMatrixView(CommandHandler handler = nullptr, AuditionCallback auditionCb = nullptr);
    ~SessionMatrixView() override;

    void setViewModel(const EditorViewModel& model);
    void setAuditionCallback(AuditionCallback cb) noexcept { m_auditionCallback = std::move(cb); }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;

    void launchScene(int sceneIndex);
    void launchClip(int trackIndex, int sceneIndex);
    void stopClip(int trackIndex, int sceneIndex);
    void stopAllClips();

    engine::DjCrossfaderEngine& crossfader() noexcept { return m_crossfaderEngine; }
    [[nodiscard]] const engine::DjCrossfaderEngine& crossfader() const noexcept { return m_crossfaderEngine; }

private:
    void timerCallback() override;

    void drawTrackHeaders(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawSceneTriggers(juce::Graphics& g, juce::Rectangle<int> bounds);

    [[nodiscard]] juce::Rectangle<int> getSlotBounds(int trackIdx, int sceneIdx) const noexcept;

    CommandHandler m_commandHandler;
    AuditionCallback m_auditionCallback;
    EditorViewModel m_model;

    static constexpr int kNumScenes = 6;
    // 2D matrix: [trackIndex][sceneIndex]
    std::vector<std::array<SessionClipSlot, kNumScenes>> m_trackSlots;
    std::array<SessionSceneInfo, kNumScenes> m_scenes {{
        {120.0, 4, 4, "Intro"},
        {124.0, 4, 4, "Build"},
        {126.0, 4, 4, "Drop A"},
        {126.0, 4, 4, "Break"},
        {128.0, 4, 4, "Drop VIP"},
        {118.0, 4, 4, "Outro"}
    }};

    juce::ComboBox m_quantizeCombo;
    juce::TextButton m_stopAllButton {"Stop All Clips [Stop]"};

    engine::DjCrossfaderEngine m_crossfaderEngine;
    juce::Slider m_crossfaderSlider;
    juce::ComboBox m_curveSelector;
    juce::TextButton m_btnNudgeMinus {"-1% Nudge"};
    juce::TextButton m_btnNudgePlus {"+1% Nudge"};
    juce::TextButton m_btnSceneFollow {"Follow Actions: ON"};
    bool m_sceneFollowEnabled {true};

    bool m_blinkState {false};
    int m_slotWidth {130};
    int m_slotHeight {44};
    int m_sceneColWidth {124};
    int m_headerHeight {32};
};

} // namespace dawg::ui
