#include "dawg/engine/MultitrackCompingEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine {

void MultitrackCompingEngine::registerTrack(uint32_t trackId, uint32_t groupId)
{
    if (findTrack(trackId) != nullptr) {
        return;
    }
    TrackCompState state;
    state.trackId = trackId;
    state.groupId = groupId;
    m_tracks.push_back(std::move(state));
}

void MultitrackCompingEngine::unregisterTrack(uint32_t trackId) noexcept
{
    std::erase_if(m_tracks, [trackId](const auto& t) { return t.trackId == trackId; });
}

TrackCompState* MultitrackCompingEngine::findTrack(uint32_t trackId) noexcept
{
    for (auto& t : m_tracks) {
        if (t.trackId == trackId) {
            return &t;
        }
    }
    return nullptr;
}

const TrackCompState* MultitrackCompingEngine::findTrack(uint32_t trackId) const noexcept
{
    for (const auto& t : m_tracks) {
        if (t.trackId == trackId) {
            return &t;
        }
    }
    return nullptr;
}

void MultitrackCompingEngine::addTake(uint32_t trackId, const TakeLane& take)
{
    auto* t = findTrack(trackId);
    if (t != nullptr) {
        t->takes.push_back(take);
    }
}

void MultitrackCompingEngine::removeTake(uint32_t trackId, uint32_t takeId) noexcept
{
    auto* t = findTrack(trackId);
    if (t != nullptr) {
        std::erase_if(t->takes, [takeId](const auto& tk) { return tk.takeId == takeId; });
        std::erase_if(t->compRegions, [takeId](const auto& r) { return r.takeId == takeId; });
    }
}

void MultitrackCompingEngine::setTrackGroup(uint32_t trackId, uint32_t groupId) noexcept
{
    auto* t = findTrack(trackId);
    if (t != nullptr) {
        t->groupId = groupId;
    }
}

uint32_t MultitrackCompingEngine::trackGroup(uint32_t trackId) const noexcept
{
    const auto* t = findTrack(trackId);
    return t != nullptr ? t->groupId : 0;
}

void MultitrackCompingEngine::applySwipeInternal(TrackCompState& track, uint32_t takeId, std::size_t start, std::size_t end)
{
    if (start >= end) {
        return;
    }

    std::vector<CompRegion> updated;
    updated.reserve(track.compRegions.size() + 2);

    for (const auto& r : track.compRegions) {
        // Disjoint before
        if (r.endFrame <= start) {
            updated.push_back(r);
            continue;
        }
        // Disjoint after
        if (r.startFrame >= end) {
            updated.push_back(r);
            continue;
        }
        // Region completely covered by new swipe
        if (r.startFrame >= start && r.endFrame <= end) {
            continue;
        }
        // Swipe splits existing region into two
        if (r.startFrame < start && r.endFrame > end) {
            updated.push_back(CompRegion {r.startFrame, start, r.takeId});
            updated.push_back(CompRegion {end, r.endFrame, r.takeId});
            continue;
        }
        // Overlap at end of existing region
        if (r.startFrame < start && r.endFrame > start) {
            updated.push_back(CompRegion {r.startFrame, start, r.takeId});
            continue;
        }
        // Overlap at start of existing region
        if (r.startFrame < end && r.endFrame > end) {
            updated.push_back(CompRegion {end, r.endFrame, r.takeId});
            continue;
        }
    }

    // Insert new swiped region
    updated.push_back(CompRegion {start, end, takeId});

    // Sort by startFrame
    std::sort(updated.begin(), updated.end(), [](const auto& a, const auto& b) {
        return a.startFrame < b.startFrame;
    });

    // Merge adjacent regions with the same takeId
    std::vector<CompRegion> merged;
    merged.reserve(updated.size());
    for (const auto& r : updated) {
        if (!merged.empty() && merged.back().takeId == r.takeId && merged.back().endFrame >= r.startFrame) {
            merged.back().endFrame = std::max(merged.back().endFrame, r.endFrame);
        } else {
            merged.push_back(r);
        }
    }

    track.compRegions = std::move(merged);
}

void MultitrackCompingEngine::swipeRegion(
    uint32_t trackId,
    uint32_t takeId,
    std::size_t startFrame,
    std::size_t endFrame)
{
    auto* t = findTrack(trackId);
    if (t != nullptr) {
        applySwipeInternal(*t, takeId, startFrame, endFrame);
    }
}

void MultitrackCompingEngine::swipeRegionGroup(
    uint32_t groupId,
    std::size_t takeIndex,
    std::size_t startFrame,
    std::size_t endFrame)
{
    if (groupId == 0 || startFrame >= endFrame) {
        return;
    }

    for (auto& track : m_tracks) {
        if (track.groupId == groupId && takeIndex < track.takes.size()) {
            const uint32_t takeId = track.takes[takeIndex].takeId;
            applySwipeInternal(track, takeId, startFrame, endFrame);
        }
    }
}

const std::vector<CompRegion>* MultitrackCompingEngine::compRegions(uint32_t trackId) const noexcept
{
    const auto* t = findTrack(trackId);
    return t != nullptr ? &t->compRegions : nullptr;
}

const TrackCompState* MultitrackCompingEngine::trackState(uint32_t trackId) const noexcept
{
    return findTrack(trackId);
}

void MultitrackCompingEngine::renderCompositeTrack(
    uint32_t trackId,
    const std::vector<const float*>& takePointers,
    std::size_t takePointerCount,
    float* masterOut,
    std::size_t totalFrames,
    std::size_t crossfadeFrames) const noexcept
{
    if (masterOut == nullptr || totalFrames == 0) {
        return;
    }

    std::fill_n(masterOut, totalFrames, 0.0f);
    const auto* t = findTrack(trackId);
    if (t == nullptr || t->compRegions.empty()) {
        return;
    }

    // Helper to find audio buffer pointer for a given takeId
    auto getTakeData = [&](uint32_t id) -> const float* {
        for (std::size_t i = 0; i < t->takes.size() && i < takePointerCount; ++i) {
            if (t->takes[i].takeId == id) {
                return takePointers[i];
            }
        }
        return nullptr;
    };

    for (std::size_t rIdx = 0; rIdx < t->compRegions.size(); ++rIdx) {
        const auto& r = t->compRegions[rIdx];
        const float* takeData = getTakeData(r.takeId);
        if (takeData == nullptr) {
            continue;
        }

        const std::size_t start = std::min(r.startFrame, totalFrames);
        const std::size_t end = std::min(r.endFrame, totalFrames);
        if (start >= end) {
            continue;
        }

        for (std::size_t i = start; i < end; ++i) {
            masterOut[i] = takeData[i];
        }

        // Apply equal-power crossfade at the boundary with the previous region
        if (rIdx > 0 && crossfadeFrames > 0) {
            const auto& prevR = t->compRegions[rIdx - 1];
            if (prevR.endFrame == r.startFrame) {
                const float* prevTakeData = getTakeData(prevR.takeId);
                if (prevTakeData != nullptr) {
                    const std::size_t xfadeLen = std::min({
                        crossfadeFrames,
                        prevR.endFrame - prevR.startFrame,
                        r.endFrame - r.startFrame
                    });

                    const std::size_t xfadeStart = r.startFrame > xfadeLen / 2 ? r.startFrame - xfadeLen / 2 : 0;
                    const std::size_t xfadeEnd = std::min(xfadeStart + xfadeLen, totalFrames);

                    for (std::size_t i = xfadeStart; i < xfadeEnd; ++i) {
                        const float tNorm = static_cast<float>(i - xfadeStart) / static_cast<float>(xfadeLen);
                        const float angle = tNorm * 0.5f * std::numbers::pi_v<float>;
                        const float gainOut = std::cos(angle);
                        const float gainIn = std::sin(angle);

                        masterOut[i] = prevTakeData[i] * gainOut + takeData[i] * gainIn;
                    }
                }
            }
        }
    }
}

void MultitrackCompingEngine::clear() noexcept
{
    m_tracks.clear();
}

} // namespace dawg::engine
