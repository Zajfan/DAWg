#pragma once

#include <dawg/engine/MidiControllerManager.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

enum class ClipSlotState : std::uint8_t
{
    empty,
    stopped,
    queuedToPlay,
    playing,
    queuedToStop
};

enum class ClipLaunchMode : std::uint8_t
{
    trigger = 0, // Normal fire: starts playing at next quantize boundary
    gate = 1,    // Plays while button/pad is held down, stops on release
    toggle = 2,  // Click once to start, click again to stop
    repeat = 3   // Continually re-triggers at quantization rate while held
};

enum class LaunchQuantize : std::uint8_t
{
    none,       // Immediate launch
    beat_1_16,
    beat_1_8,
    beat_1_4,
    beat_1_2,
    bar_1,      // Next bar (default)
    bars_2,
    bars_4,
    triplet_1_4,
    triplet_1_8
};

enum class FollowAction : std::uint8_t
{
    none,
    stop,
    nextClip,
    prevClip,
    firstClip,
    lastClip,
    randomClip,
    jumpToScene
};

struct SessionClipSlot
{
    ClipSlotState state {ClipSlotState::empty};
    ClipLaunchMode launchMode {ClipLaunchMode::trigger};
    std::string name;
    LaunchpadColor color {LaunchpadColor::green};
    double lengthBeats {4.0};
    double playbackBeat {0.0};
    double barsPlayed {0.0};
    bool isLegato {false}; // Legato switching: inherits playhead position from previous playing clip

    LaunchQuantize launchQuantize {LaunchQuantize::bar_1};

    // Dual Follow Actions with Probabilistic Chance (Ableton/Bitwig style)
    FollowAction followActionA {FollowAction::none};
    FollowAction followActionB {FollowAction::none};
    float chanceA {1.0f}; // 0.0 to 1.0 (chance of Action A; 1 - chanceA is chance of Action B)
    std::size_t jumpSceneA {0}; // Target scene if Action A is jumpToScene
    std::size_t jumpSceneB {0}; // Target scene if Action B is jumpToScene
    double followActionBars {1.0};
};

// Session-to-Arrangement Jam Recorder Event
struct RecordedJamEvent
{
    double songBeat {0.0};
    std::size_t trackIdx {0};
    std::size_t sceneIdx {0};
    enum class Type : std::uint8_t {
        clipFired,
        clipStopped,
        sceneFired,
        stopAll,
        macroChanged
    } type {Type::clipFired};
    std::string clipName;
    double durationBeats {0.0};
    float macroValue {0.0f};
    std::size_t macroIndex {0};
};

struct ConsolidatedJamClip
{
    std::size_t trackIdx {0};
    std::string clipName;
    double startBeat {0.0};
    double lengthBeats {0.0};
    std::size_t sourceSceneIdx {0};
};

// Non-Linear Session View & Matrix Clip Launcher Engine (Ableton Live / Bitwig style)
// Coordinates non-linear 8x8 clip launching, bar-quantized scheduling,
// launch modes (Trigger, Gate, Toggle, Repeat, Legato), dual probabilistic follow actions,
// session-to-arrangement jam recording, and Launchpad/APC RGB matrix feedback.
class SessionMatrixEngine final
{
public:
    static constexpr std::size_t numTracks = 8;
    static constexpr std::size_t numScenes = 8;

    SessionMatrixEngine() noexcept;
    ~SessionMatrixEngine() = default;

    // Slot Configuration
    void setClip(std::size_t trackIdx, std::size_t sceneIdx, const SessionClipSlot& slot);
    void clearClip(std::size_t trackIdx, std::size_t sceneIdx);
    [[nodiscard]] const SessionClipSlot& getClip(std::size_t trackIdx, std::size_t sceneIdx) const noexcept;
    [[nodiscard]] SessionClipSlot& getClipRef(std::size_t trackIdx, std::size_t sceneIdx) noexcept;

    // Triggering Actions
    void fireClip(std::size_t trackIdx, std::size_t sceneIdx, bool immediate = false) noexcept;
    void releaseClip(std::size_t trackIdx, std::size_t sceneIdx) noexcept; // For Gate / Repeat modes
    void stopClip(std::size_t trackIdx, bool immediate = false) noexcept;
    void fireScene(std::size_t sceneIdx, bool immediate = false) noexcept;
    void stopAllClips(bool immediate = false) noexcept;

    // Clock advancement (called each audio block or transport step)
    void advanceClock(double deltaBeats, double songBeat, double bpm) noexcept;

    // Session-to-Arrangement Jam Recorder
    void startJamRecording(double currentSongBeat) noexcept;
    void stopJamRecording() noexcept;
    [[nodiscard]] bool isJamRecording() const noexcept { return m_isJamRecording; }
    void recordMacroChange(std::size_t macroIdx, float value) noexcept;
    [[nodiscard]] const std::vector<RecordedJamEvent>& recordedJamEvents() const noexcept { return m_jamEvents; }
    [[nodiscard]] std::vector<ConsolidatedJamClip> consolidateRecordedClips() const;
    void clearJamRecording() noexcept;

    // Hardware Launchpad / APC RGB Matrix Feedback
    [[nodiscard]] std::vector<MidiEvent> generateLaunchpadLedEvents() const;
    void handleLaunchpadPress(std::size_t trackIdx, std::size_t sceneIdx) noexcept;
    void handleLaunchpadRelease(std::size_t trackIdx, std::size_t sceneIdx) noexcept;

    // Active playing count query
    [[nodiscard]] std::size_t activePlayingCount() const noexcept;

private:
    [[nodiscard]] bool isQuantizeBoundaryCrossed(
        LaunchQuantize quantize,
        double prevBeat,
        double nextBeat) const noexcept;

    void triggerFollowAction(std::size_t trackIdx, std::size_t currentSceneIdx) noexcept;

    std::array<std::array<SessionClipSlot, numScenes>, numTracks> m_grid {};
    double m_currentSongBeat {0.0};
    std::uint32_t m_randState {987654321};

    // Jam Recording state
    bool m_isJamRecording {false};
    double m_jamRecordingStartBeat {0.0};
    std::vector<RecordedJamEvent> m_jamEvents;
};

} // namespace dawg::engine
