#pragma once

#include <dawg/project/ProjectTypes.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::project {

struct TakeLane
{
    std::uint32_t takeId {0};
    TrackId trackId {0};
    std::string name {"Take 1"};
    std::string colorHex {"#4A90E2"};
    AudioClip clip {};
    bool isAuditionSolo {false};

    friend bool operator==(const TakeLane&, const TakeLane&) = default;
};

struct CompSlice
{
    std::uint32_t sliceId {0};
    std::uint32_t takeId {0};
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};
    std::uint32_t crossfadeSamples {192}; // ~4 ms at 48 kHz

    friend bool operator==(const CompSlice&, const CompSlice&) = default;
};

// Advanced Audio Comping & Multi-Take Lane Editor (Logic / Pro Tools style)
// Manages multi-take recording lanes, interactive swipe-comping with equal-power micro-crossfades,
// take audition soloing, and "Flatten & Merge" into a unified composite audio clip.
class TakeCompManager
{
public:
    explicit TakeCompManager(TrackId trackId) noexcept;
    ~TakeCompManager() = default;

    [[nodiscard]] TrackId trackId() const noexcept { return m_trackId; }

    // Multi-Take Lane Management
    std::uint32_t addTake(AudioClip clip, const std::string& name = "", const std::string& colorHex = "");
    bool removeTake(std::uint32_t takeId);
    [[nodiscard]] const std::vector<TakeLane>& takeLanes() const noexcept { return m_takes; }
    [[nodiscard]] const TakeLane* findTake(std::uint32_t takeId) const noexcept;

    // Swipe-Comping Selection
    // Selecting a region automatically splices and updates active comp slices
    void selectCompSlice(
        std::uint32_t takeId,
        std::uint64_t startSample,
        std::uint64_t endSample,
        std::uint32_t crossfadeSamples = 192);

    void clearCompSlices() noexcept;
    [[nodiscard]] const std::vector<CompSlice>& compSlices() const noexcept { return m_slices; }

    // Take Audition Solo
    void setTakeAuditionSolo(std::uint32_t takeId, bool solo) noexcept;
    [[nodiscard]] std::uint32_t soloedTakeId() const noexcept { return m_soloedTakeId; }

    // Flatten & Merge into Consolidated Audio Clip
    [[nodiscard]] AudioClip flattenAndMergeComp(
        ClipId newClipId,
        MediaAssetId consolidatedAssetId,
        const std::string& compName = "Comp Lead Vocal") const;

    // Crossfade Calculation Utility
    static void applyEqualPowerCrossfade(
        float* outOutgoing,
        float* outIncoming,
        std::size_t fadeLength) noexcept;

private:
    TrackId m_trackId {0};
    std::uint32_t m_nextTakeId {1};
    std::uint32_t m_nextSliceId {1};
    std::uint32_t m_soloedTakeId {0}; // 0 = no solo active

    std::vector<TakeLane> m_takes;
    std::vector<CompSlice> m_slices;
};

} // namespace dawg::project
