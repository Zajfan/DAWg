#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

struct TakeLane
{
    uint32_t takeId {0};
    std::string name;
    uint8_t starRating {3};      // 1 to 5 stars
    uint32_t colorRgba {0x3498DBFF}; // Default blue
    bool muted {false};
};

struct CompRegion
{
    std::size_t startFrame {0};
    std::size_t endFrame {0};
    uint32_t takeId {0};
};

struct TrackCompState
{
    uint32_t trackId {0};
    uint32_t groupId {0}; // 0 = unassigned / ungrouped
    std::vector<TakeLane> takes;
    std::vector<CompRegion> compRegions;
};

class MultitrackCompingEngine
{
public:
    MultitrackCompingEngine() noexcept = default;
    ~MultitrackCompingEngine() = default;

    // Track & Take Registration
    void registerTrack(uint32_t trackId, uint32_t groupId = 0);
    void unregisterTrack(uint32_t trackId) noexcept;
    void addTake(uint32_t trackId, const TakeLane& take);
    void removeTake(uint32_t trackId, uint32_t takeId) noexcept;

    void setTrackGroup(uint32_t trackId, uint32_t groupId) noexcept;
    [[nodiscard]] uint32_t trackGroup(uint32_t trackId) const noexcept;

    // Swipe Comping (Single Track)
    void swipeRegion(
        uint32_t trackId,
        uint32_t takeId,
        std::size_t startFrame,
        std::size_t endFrame);

    // Phase-Locked Group Comping (applies swipe across all tracks in the group)
    void swipeRegionGroup(
        uint32_t groupId,
        std::size_t takeIndex,
        std::size_t startFrame,
        std::size_t endFrame);

    [[nodiscard]] const std::vector<CompRegion>* compRegions(uint32_t trackId) const noexcept;
    [[nodiscard]] const TrackCompState* trackState(uint32_t trackId) const noexcept;

    // Flatten & Render composite take with equal-power micro-crossfades
    void renderCompositeTrack(
        uint32_t trackId,
        const std::vector<const float*>& takePointers,
        std::size_t takePointerCount,
        float* masterOut,
        std::size_t totalFrames,
        std::size_t crossfadeFrames = 256) const noexcept;

    void clear() noexcept;

private:
    std::vector<TrackCompState> m_tracks;

    [[nodiscard]] TrackCompState* findTrack(uint32_t trackId) noexcept;
    [[nodiscard]] const TrackCompState* findTrack(uint32_t trackId) const noexcept;
    void applySwipeInternal(TrackCompState& track, uint32_t takeId, std::size_t start, std::size_t end);
};

} // namespace dawg::engine
