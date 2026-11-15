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

enum class LaunchQuantize : std::uint8_t
{
    none,       // Immediate launch
    beat_1_16,
    beat_1_4,
    bar_1,      // Next bar (default)
    bars_2,
    bars_4
};

enum class FollowAction : std::uint8_t
{
    none,
    stop,
    nextClip,
    prevClip,
    firstClip,
    lastClip,
    randomClip
};

struct SessionClipSlot
{
    ClipSlotState state {ClipSlotState::empty};
    std::string name;
    LaunchpadColor color {LaunchpadColor::green};
    double lengthBeats {4.0};
    double playbackBeat {0.0};
    double barsPlayed {0.0};

    LaunchQuantize launchQuantize {LaunchQuantize::bar_1};
    FollowAction followAction {FollowAction::none};
    double followActionBars {1.0};
};

// Non-Linear Session View & Matrix Clip Launcher Engine (Ableton Live / Bitwig style)
// Coordinates non-linear 8x8 clip launching, bar-quantized scheduling,
// generative follow actions, and two-way Launchpad/APC RGB matrix hardware synchronization.
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
    void stopClip(std::size_t trackIdx, bool immediate = false) noexcept;
    void fireScene(std::size_t sceneIdx, bool immediate = false) noexcept;
    void stopAllClips(bool immediate = false) noexcept;

    // Clock advancement (called each audio block or transport step)
    void advanceClock(double deltaBeats, double songBeat, double bpm) noexcept;

    // Hardware Launchpad / APC RGB Matrix Feedback
    [[nodiscard]] std::vector<MidiEvent> generateLaunchpadLedEvents() const;
    void handleLaunchpadPress(std::size_t trackIdx, std::size_t sceneIdx) noexcept;

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
};

} // namespace dawg::engine
