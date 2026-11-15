#include <dawg/engine/SessionMatrixEngine.h>

#include <cmath>

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

    if (immediate || target.launchQuantize == LaunchQuantize::none)
    {
        for (std::size_t s = 0; s < numScenes; ++s)
        {
            if (s != sceneIdx && m_grid[trackIdx][s].state != ClipSlotState::empty)
            {
                m_grid[trackIdx][s].state = ClipSlotState::stopped;
                m_grid[trackIdx][s].playbackBeat = 0.0;
            }
        }
        target.state = ClipSlotState::playing;
        target.playbackBeat = 0.0;
        target.barsPlayed = 0.0;
    }
    else
    {
        target.state = ClipSlotState::queuedToPlay;
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
                slot.state = ClipSlotState::stopped;
                slot.playbackBeat = 0.0;
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
    case LaunchQuantize::beat_1_4:
        return std::floor(prevBeat) != std::floor(nextBeat);
    case LaunchQuantize::bar_1:
        return std::floor(prevBeat * 0.25) != std::floor(nextBeat * 0.25);
    case LaunchQuantize::bars_2:
        return std::floor(prevBeat * 0.125) != std::floor(nextBeat * 0.125);
    case LaunchQuantize::bars_4:
        return std::floor(prevBeat * 0.0625) != std::floor(nextBeat * 0.0625);
    }
    return true;
}

void SessionMatrixEngine::triggerFollowAction(const std::size_t trackIdx, const std::size_t currentSceneIdx) noexcept
{
    auto& currentSlot = m_grid[trackIdx][currentSceneIdx];
    const auto action = currentSlot.followAction;

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
                            m_grid[t][other].state = ClipSlotState::stopped;
                            m_grid[t][other].playbackBeat = 0.0;
                        }
                    }
                    slot.state = ClipSlotState::playing;
                    slot.playbackBeat = 0.0;
                    slot.barsPlayed = 0.0;
                }
            }
            else if (slot.state == ClipSlotState::queuedToStop)
            {
                if (isQuantizeBoundaryCrossed(slot.launchQuantize, prevBeat, songBeat))
                {
                    slot.state = ClipSlotState::stopped;
                    slot.playbackBeat = 0.0;
                    slot.barsPlayed = 0.0;
                }
            }
            else if (slot.state == ClipSlotState::playing)
            {
                slot.playbackBeat += deltaBeats;
                slot.barsPlayed += deltaBeats * 0.25;

                // Check Follow Action threshold
                if (slot.followAction != FollowAction::none && slot.barsPlayed >= slot.followActionBars)
                {
                    triggerFollowAction(t, s);
                }
            }
        }
    }
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
