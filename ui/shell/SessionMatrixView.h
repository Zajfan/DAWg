#pragma once

#include "EditorViewModel.h"

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

    juce::ComboBox m_quantizeCombo;
    juce::TextButton m_stopAllButton {"Stop All Clips [■]"};

    bool m_blinkState {false};
    int m_slotWidth {130};
    int m_slotHeight {44};
    int m_sceneColWidth {90};
    int m_headerHeight {32};
};

} // namespace dawg::ui
