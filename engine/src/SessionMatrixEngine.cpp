#include <dawg/engine/SessionMatrixEngine.h>

#include <cmath>
#include <map>

namespace dawg::engine {
namespace {

inline float nextRandFloat(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0x00FFFFFF) / static_cast<float>(0x01000000);
}

} // namespace

SessionMatrixEngine::SessionMatrixEngine() noexcept
{
    for (auto& row : m_grid)
    {
        for (auto& slot : row)
        {
            slot.state = ClipSlotState::empty;
        }
    }
}

void SessionMatrixEngine::setClip(const std::size_t trackIdx, const std::size_t sceneIdx, const SessionClipSlot& slot)
{
    if (trackIdx < numTracks && sceneIdx < numScenes)
    {
        m_grid[trackIdx][sceneIdx] = slot;
    }
}

void SessionMatrixEngine::clearClip(const std::size_t trackIdx, const std::size_t sceneIdx)
{
    if (trackIdx < numTracks && sceneIdx < numScenes)
    {
        m_grid[trackIdx][sceneIdx] = SessionClipSlot {};
    }
}

const SessionClipSlot& SessionMatrixEngine::getClip(const std::size_t trackIdx, const std::size_t sceneIdx) const noexcept
{
    static constexpr SessionClipSlot emptySlot {};
    if (trackIdx < numTracks && sceneIdx < numScenes)
    {
        return m_grid[trackIdx][sceneIdx];
    }
    return emptySlot;
}

SessionClipSlot& SessionMatrixEngine::getClipRef(const std::size_t trackIdx, const std::size_t sceneIdx) noexcept
{
    static SessionClipSlot emptySlot {};
    if (trackIdx < numTracks && sceneIdx < numScenes)
    {
        return m_grid[trackIdx][sceneIdx];
    }
    return emptySlot;
}

void SessionMatrixEngine::fireClip(const std::size_t trackIdx, const std::size_t sceneIdx, const bool immediate) noexcept
{
    if (trackIdx >= numTracks || sceneIdx >= numScenes)
    {
        return;
    }

    auto& target = m_grid[trackIdx][sceneIdx];
    if (target.state == ClipSlotState::empty)
    {
        return;
    }

    // Toggle mode: If already playing or queued to play, stop it instead
    if (target.launchMode == ClipLaunchMode::toggle &&
        (target.state == ClipSlotState::playing || target.state == ClipSlotState::queuedToPlay))
    {
        stopClip(trackIdx, immediate);
        return;
    }

    // Capture currently playing clip's playhead position for Legato transfer
    double currentPlayingBeat = 0.0;
    bool hadPlayingClip = false;
    for (std::size_t s = 0; s < numScenes; ++s)
    {
        if (m_grid[trackIdx][s].state == ClipSlotState::playing)
        {
            currentPlayingBeat = m_grid[trackIdx][s].playbackBeat;
            hadPlayingClip = true;
            break;
        }
    }

    if (immediate || target.launchQuantize == LaunchQuantize::none)
    {
        for (std::size_t s = 0; s < numScenes; ++s)
        {
            if (s != sceneIdx && m_grid[trackIdx][s].state != ClipSlotState::empty)
            {
                if (m_grid[trackIdx][s].state == ClipSlotState::playing && m_isJamRecording)
                {
                    m_jamEvents.push_back(RecordedJamEvent{
                        .songBeat = m_currentSongBeat,
                        .trackIdx = trackIdx,
                        .sceneIdx = s,
                        .type = RecordedJamEvent::Type::clipStopped,
                        .clipName = m_grid[trackIdx][s].name,
                        .durationBeats = 0.0,
                        .macroValue = 0.0f,
                        .macroIndex = 0
                    });
                }
                m_grid[trackIdx][s].state = ClipSlotState::stopped;
                m_grid[trackIdx][s].playbackBeat = 0.0;
            }
        }

        target.state = ClipSlotState::playing;
        if (target.isLegato && hadPlayingClip && target.lengthBeats > 0.0)
        {
            target.playbackBeat = std::fmod(currentPlayingBeat, target.lengthBeats);
        }
        else
        {
            target.playbackBeat = 0.0;
        }
        target.barsPlayed = 0.0;

        if (m_isJamRecording)
        {
            m_jamEvents.push_back(RecordedJamEvent{
                .songBeat = m_currentSongBeat,
                .trackIdx = trackIdx,
                .sceneIdx = sceneIdx,
                .type = RecordedJamEvent::Type::clipFired,
                .clipName = target.name,
                .durationBeats = target.lengthBeats,
                .macroValue = 0.0f,
                .macroIndex = 0
            });
        }
    }
    else
    {
        target.state = ClipSlotState::queuedToPlay;
    }
}

void SessionMatrixEngine::releaseClip(const std::size_t trackIdx, const std::size_t sceneIdx) noexcept
{
    if (trackIdx >= numTracks || sceneIdx >= numScenes)
    {
        return;
    }

    const auto& slot = m_grid[trackIdx][sceneIdx];
    // Gate mode: releases and stops playback
    if (slot.launchMode == ClipLaunchMode::gate || slot.launchMode == ClipLaunchMode::repeat)
    {
        stopClip(trackIdx, slot.launchQuantize == LaunchQuantize::none);
    }
}

void SessionMatrixEngine::stopClip(const std::size_t trackIdx, const bool immediate) noexcept
{
    if (trackIdx >= numTracks)
    {
        return;
    }

    for (std::size_t s = 0; s < numScenes; ++s)
    {
        auto& slot = m_grid[trackIdx][s];
        if (slot.state == ClipSlotState::playing || slot.state == ClipSlotState::queuedToPlay)
        {
            if (immediate)
            {
                if (slot.state == ClipSlotState::playing && m_isJamRecording)
                {
                    m_jamEvents.push_back(RecordedJamEvent{
                        .songBeat = m_currentSongBeat,
                        .trackIdx = trackIdx,
                        .sceneIdx = s,
                        .type = RecordedJamEvent::Type::clipStopped,
                        .clipName = slot.name,
                        .durationBeats = 0.0,
                        .macroValue = 0.0f,
                        .macroIndex = 0
                    });
                }
                slot.state = ClipSlotState::stopped;
                slot.playbackBeat = 0.0;
                slot.barsPlayed = 0.0;
            }
            else
            {
                slot.state = ClipSlotState::queuedToStop;
            }
        }
    }
}

void SessionMatrixEngine::fireScene(const std::size_t sceneIdx, const bool immediate) noexcept
{
    if (sceneIdx >= numScenes)
    {
        return;
    }

    if (m_isJamRecording)
    {
        m_jamEvents.push_back(RecordedJamEvent{
            .songBeat = m_currentSongBeat,
            .trackIdx = 0,
            .sceneIdx = sceneIdx,
            .type = RecordedJamEvent::Type::sceneFired,
            .clipName = "Scene " + std::to_string(sceneIdx + 1),
            .durationBeats = 0.0,
            .macroValue = 0.0f,
            .macroIndex = 0
        });
    }

    for (std::size_t t = 0; t < numTracks; ++t)
    {
        if (m_grid[t][sceneIdx].state != ClipSlotState::empty)
        {
            fireClip(t, sceneIdx, immediate);
        }
    }
}

void SessionMatrixEngine::stopAllClips(const bool immediate) noexcept
{
    if (m_isJamRecording)
    {
        m_jamEvents.push_back(RecordedJamEvent{
            .songBeat = m_currentSongBeat,
            .trackIdx = 0,
            .sceneIdx = 0,
            .type = RecordedJamEvent::Type::stopAll,
            .clipName = "Stop All",
            .durationBeats = 0.0,
            .macroValue = 0.0f,
            .macroIndex = 0
        });
    }

    for (std::size_t t = 0; t < numTracks; ++t)
    {
        stopClip(t, immediate);
    }
}

bool SessionMatrixEngine::isQuantizeBoundaryCrossed(
    const LaunchQuantize quantize,
    const double prevBeat,
    const double nextBeat) const noexcept
{
    switch (quantize)
    {
    case LaunchQuantize::none:
        return true;
    case LaunchQuantize::beat_1_16:
        return std::floor(prevBeat * 4.0) != std::floor(nextBeat * 4.0);
    case LaunchQuantize::beat_1_8:
        return std::floor(prevBeat * 2.0) != std::floor(nextBeat * 2.0);
    case LaunchQuantize::beat_1_4:
        return std::floor(prevBeat) != std::floor(nextBeat);
    case LaunchQuantize::beat_1_2:
        return std::floor(prevBeat * 0.5) != std::floor(nextBeat * 0.5);
    case LaunchQuantize::bar_1:
        return std::floor(prevBeat * 0.25) != std::floor(nextBeat * 0.25);
    case LaunchQuantize::bars_2:
        return std::floor(prevBeat * 0.125) != std::floor(nextBeat * 0.125);
    case LaunchQuantize::bars_4:
        return std::floor(prevBeat * 0.0625) != std::floor(nextBeat * 0.0625);
    case LaunchQuantize::triplet_1_4:
        return std::floor(prevBeat * 1.5) != std::floor(nextBeat * 1.5);
    case LaunchQuantize::triplet_1_8:
        return std::floor(prevBeat * 3.0) != std::floor(nextBeat * 3.0);
    }
    return true;
}

void SessionMatrixEngine::triggerFollowAction(const std::size_t trackIdx, const std::size_t currentSceneIdx) noexcept
{
    auto& currentSlot = m_grid[trackIdx][currentSceneIdx];

    // Dual probabilistic follow action resolution
    const float roll = nextRandFloat(m_randState);
    const bool useActionA = (roll < currentSlot.chanceA);
    const auto action = useActionA ? currentSlot.followActionA : currentSlot.followActionB;
    const auto jumpTarget = useActionA ? currentSlot.jumpSceneA : currentSlot.jumpSceneB;

    std::size_t nextScene = currentSceneIdx;

    switch (action)
    {
    case FollowAction::none:
        return;
    case FollowAction::stop:
        stopClip(trackIdx, false);
        return;
    case FollowAction::nextClip:
        nextScene = (currentSceneIdx + 1) % numScenes;
        break;
    case FollowAction::prevClip:
        nextScene = (currentSceneIdx + numScenes - 1) % numScenes;
        break;
    case FollowAction::firstClip:
        nextScene = 0;
        break;
    case FollowAction::lastClip:
        nextScene = numScenes - 1;
        break;
    case FollowAction::randomClip:
    {
        const auto r = static_cast<std::size_t>(nextRandFloat(m_randState) * static_cast<float>(numScenes));
        nextScene = std::clamp(r, std::size_t(0), numScenes - 1);
        break;
    }
    case FollowAction::jumpToScene:
        nextScene = std::clamp(jumpTarget, std::size_t(0), numScenes - 1);
        break;
    }

    if (m_grid[trackIdx][nextScene].state != ClipSlotState::empty)
    {
        fireClip(trackIdx, nextScene, false);
    }
}

void SessionMatrixEngine::advanceClock(
    const double deltaBeats,
    const double songBeat,
    [[maybe_unused]] const double bpm) noexcept
{
    const double prevBeat = m_currentSongBeat;
    m_currentSongBeat = songBeat;

    for (std::size_t t = 0; t < numTracks; ++t)
    {
        // Check if there was a playing clip to transfer legato playhead
        double lastPlayingBeat = 0.0;
        bool hasActivePlaying = false;
        for (std::size_t s = 0; s < numScenes; ++s)
        {
            if (m_grid[t][s].state == ClipSlotState::playing)
            {
                lastPlayingBeat = m_grid[t][s].playbackBeat;
                hasActivePlaying = true;
                break;
            }
        }

        for (std::size_t s = 0; s < numScenes; ++s)
        {
            auto& slot = m_grid[t][s];

            if (slot.state == ClipSlotState::queuedToPlay)
            {
                if (isQuantizeBoundaryCrossed(slot.launchQuantize, prevBeat, songBeat))
                {
                    // Transition previous playing clips to stopped
                    for (std::size_t other = 0; other < numScenes; ++other)
                    {
                        if (other != s && m_grid[t][other].state != ClipSlotState::empty)
                        {
                            if (m_grid[t][other].state == ClipSlotState::playing && m_isJamRecording)
                            {
                                m_jamEvents.push_back(RecordedJamEvent{
                                    .songBeat = songBeat,
                                    .trackIdx = t,
                                    .sceneIdx = other,
                                    .type = RecordedJamEvent::Type::clipStopped,
                                    .clipName = m_grid[t][other].name,
                                    .durationBeats = 0.0,
                                    .macroValue = 0.0f,
                                    .macroIndex = 0
                                });
                            }
                            m_grid[t][other].state = ClipSlotState::stopped;
                            m_grid[t][other].playbackBeat = 0.0;
                        }
                    }

                    slot.state = ClipSlotState::playing;
                    if (slot.isLegato && hasActivePlaying && slot.lengthBeats > 0.0)
                    {
                        slot.playbackBeat = std::fmod(lastPlayingBeat, slot.lengthBeats);
                    }
                    else
                    {
                        slot.playbackBeat = 0.0;
                    }
                    slot.barsPlayed = 0.0;

                    if (m_isJamRecording)
                    {
                        m_jamEvents.push_back(RecordedJamEvent{
                            .songBeat = songBeat,
                            .trackIdx = t,
                            .sceneIdx = s,
                            .type = RecordedJamEvent::Type::clipFired,
                            .clipName = slot.name,
                            .durationBeats = slot.lengthBeats,
                            .macroValue = 0.0f,
                            .macroIndex = 0
                        });
                    }
                }
            }
            else if (slot.state == ClipSlotState::queuedToStop)
            {
                if (isQuantizeBoundaryCrossed(slot.launchQuantize, prevBeat, songBeat))
                {
                    if (m_isJamRecording)
                    {
                        m_jamEvents.push_back(RecordedJamEvent{
                            .songBeat = songBeat,
                            .trackIdx = t,
                            .sceneIdx = s,
                            .type = RecordedJamEvent::Type::clipStopped,
                            .clipName = slot.name,
                            .durationBeats = 0.0,
                            .macroValue = 0.0f,
                            .macroIndex = 0
                        });
                    }
                    slot.state = ClipSlotState::stopped;
                    slot.playbackBeat = 0.0;
                    slot.barsPlayed = 0.0;
                }
            }
            else if (slot.state == ClipSlotState::playing)
            {
                slot.playbackBeat += deltaBeats;
                slot.barsPlayed += deltaBeats * 0.25;

                // Handle repeat launch mode: re-trigger clip periodically at quantize rate
                if (slot.launchMode == ClipLaunchMode::repeat)
                {
                    if (isQuantizeBoundaryCrossed(slot.launchQuantize, prevBeat, songBeat))
                    {
                        slot.playbackBeat = 0.0;
                    }
                }

                // Check Follow Action threshold
                if ((slot.followActionA != FollowAction::none || slot.followActionB != FollowAction::none) &&
                    slot.barsPlayed >= slot.followActionBars)
                {
                    triggerFollowAction(t, s);
                }
            }
        }
    }
}

void SessionMatrixEngine::startJamRecording(const double currentSongBeat) noexcept
{
    m_isJamRecording = true;
    m_jamRecordingStartBeat = currentSongBeat;

    // Snapshot currently playing clips into the recording stream
    for (std::size_t t = 0; t < numTracks; ++t)
    {
        for (std::size_t s = 0; s < numScenes; ++s)
        {
            if (m_grid[t][s].state == ClipSlotState::playing)
            {
                m_jamEvents.push_back(RecordedJamEvent{
                    .songBeat = currentSongBeat,
                    .trackIdx = t,
                    .sceneIdx = s,
                    .type = RecordedJamEvent::Type::clipFired,
                    .clipName = m_grid[t][s].name,
                    .durationBeats = m_grid[t][s].lengthBeats,
                    .macroValue = 0.0f,
                    .macroIndex = 0
                });
            }
        }
    }
}

void SessionMatrixEngine::stopJamRecording() noexcept
{
    m_isJamRecording = false;
}

void SessionMatrixEngine::recordMacroChange(const std::size_t macroIdx, const float value) noexcept
{
    if (m_isJamRecording)
    {
        m_jamEvents.push_back(RecordedJamEvent{
            .songBeat = m_currentSongBeat,
            .trackIdx = 0,
            .sceneIdx = 0,
            .type = RecordedJamEvent::Type::macroChanged,
            .clipName = "",
            .durationBeats = 0.0,
            .macroValue = value,
            .macroIndex = macroIdx
        });
    }
}

std::vector<ConsolidatedJamClip> SessionMatrixEngine::consolidateRecordedClips() const
{
    std::vector<ConsolidatedJamClip> result;
    // Map of active (trackIdx) -> start event
    std::map<std::size_t, RecordedJamEvent> activeTrackClips;

    for (const auto& ev : m_jamEvents)
    {
        if (ev.type == RecordedJamEvent::Type::clipFired)
        {
            if (activeTrackClips.contains(ev.trackIdx))
            {
                const auto& prior = activeTrackClips[ev.trackIdx];
                const double len = std::max(0.25, ev.songBeat - prior.songBeat);
                result.push_back(ConsolidatedJamClip{
                    .trackIdx = prior.trackIdx,
                    .clipName = prior.clipName,
                    .startBeat = prior.songBeat,
                    .lengthBeats = len,
                    .sourceSceneIdx = prior.sceneIdx
                });
            }
            activeTrackClips[ev.trackIdx] = ev;
        }
        else if (ev.type == RecordedJamEvent::Type::clipStopped)
        {
            if (activeTrackClips.contains(ev.trackIdx))
            {
                const auto& prior = activeTrackClips[ev.trackIdx];
                const double len = std::max(0.25, ev.songBeat - prior.songBeat);
                result.push_back(ConsolidatedJamClip{
                    .trackIdx = prior.trackIdx,
                    .clipName = prior.clipName,
                    .startBeat = prior.songBeat,
                    .lengthBeats = len,
                    .sourceSceneIdx = prior.sceneIdx
                });
                activeTrackClips.erase(ev.trackIdx);
            }
        }
        else if (ev.type == RecordedJamEvent::Type::stopAll)
        {
            for (const auto& [trackIdx, prior] : activeTrackClips)
            {
                const double len = std::max(0.25, ev.songBeat - prior.songBeat);
                result.push_back(ConsolidatedJamClip{
                    .trackIdx = prior.trackIdx,
                    .clipName = prior.clipName,
                    .startBeat = prior.songBeat,
                    .lengthBeats = len,
                    .sourceSceneIdx = prior.sceneIdx
                });
            }
            activeTrackClips.clear();
        }
    }

    // Close any still active at end of recording
    for (const auto& [trackIdx, prior] : activeTrackClips)
    {
        const double len = std::max(prior.durationBeats, 4.0);
        result.push_back(ConsolidatedJamClip{
            .trackIdx = prior.trackIdx,
            .clipName = prior.clipName,
            .startBeat = prior.songBeat,
            .lengthBeats = len,
            .sourceSceneIdx = prior.sceneIdx
        });
    }

    return result;
}

void SessionMatrixEngine::clearJamRecording() noexcept
{
    m_jamEvents.clear();
    m_isJamRecording = false;
    m_jamRecordingStartBeat = 0.0;
}

std::vector<MidiEvent> SessionMatrixEngine::generateLaunchpadLedEvents() const
{
    std::vector<MidiEvent> events;
    events.reserve(numTracks * numScenes);

    for (std::size_t t = 0; t < numTracks; ++t)
    {
        for (std::size_t s = 0; s < numScenes; ++s)
        {
            const auto& slot = m_grid[t][s];

            LaunchpadColor color = LaunchpadColor::off;
            LaunchpadLedMode mode = LaunchpadLedMode::staticColor;

            switch (slot.state)
            {
            case ClipSlotState::empty:
                color = LaunchpadColor::off;
                mode = LaunchpadLedMode::staticColor;
                break;
            case ClipSlotState::stopped:
                color = slot.color;
                mode = LaunchpadLedMode::staticColor;
                break;
            case ClipSlotState::queuedToPlay:
                color = slot.color;
                mode = LaunchpadLedMode::flashing;
                break;
            case ClipSlotState::playing:
                color = slot.color;
                mode = LaunchpadLedMode::pulsing;
                break;
            case ClipSlotState::queuedToStop:
                color = LaunchpadColor::red;
                mode = LaunchpadLedMode::flashing;
                break;
            }

            events.push_back(MidiControllerManager::makeLaunchpadPad(
                static_cast<std::uint8_t>(t),
                static_cast<std::uint8_t>(s),
                color,
                mode));
        }
    }
    return events;
}

void SessionMatrixEngine::handleLaunchpadPress(const std::size_t trackIdx, const std::size_t sceneIdx) noexcept
{
    if (trackIdx >= numTracks || sceneIdx >= numScenes)
    {
        return;
    }

    const auto& slot = m_grid[trackIdx][sceneIdx];
    if (slot.state == ClipSlotState::empty)
    {
        return;
    }

    if (slot.state == ClipSlotState::playing || slot.state == ClipSlotState::queuedToPlay)
    {
        stopClip(trackIdx, false);
    }
    else
    {
        fireClip(trackIdx, sceneIdx, false);
    }
}

void SessionMatrixEngine::handleLaunchpadRelease(const std::size_t trackIdx, const std::size_t sceneIdx) noexcept
{
    releaseClip(trackIdx, sceneIdx);
}

std::size_t SessionMatrixEngine::activePlayingCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& row : m_grid)
    {
        for (const auto& slot : row)
        {
            if (slot.state == ClipSlotState::playing)
            {
                ++count;
            }
        }
    }
    return count;
}

} // namespace dawg::engine
