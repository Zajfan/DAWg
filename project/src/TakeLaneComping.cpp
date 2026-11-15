#include <dawg/project/TakeLaneComping.h>

#include <array>
#include <cmath>
#include <numbers>

namespace dawg::project {

namespace {

constexpr std::array<const char*, 5> kDefaultTakeColors = {
    "#4A90E2", // Electric Blue
    "#50E3C2", // Mint Cyan
    "#F5A623", // Sunset Amber
    "#E91E63", // Hot Pink
    "#9013FE"  // Violet
};

} // namespace

TakeCompManager::TakeCompManager(TrackId trackId) noexcept
    : m_trackId(trackId)
{
}

std::uint32_t TakeCompManager::addTake(AudioClip clip, const std::string& name, const std::string& colorHex)
{
    const std::uint32_t takeId = m_nextTakeId++;
    const std::string takeName = name.empty() ? ("Take " + std::to_string(takeId)) : name;
    const std::string color = colorHex.empty() ? kDefaultTakeColors[(takeId - 1) % kDefaultTakeColors.size()] : colorHex;

    TakeLane lane {
        .takeId = takeId,
        .trackId = m_trackId,
        .name = takeName,
        .colorHex = color,
        .clip = clip,
        .isAuditionSolo = false
    };

    m_takes.push_back(std::move(lane));

    // If first take and no slices yet, default entire clip as base comp
    if (m_takes.size() == 1 && m_slices.empty() && clip.lengthSamples > 0) {
        m_slices.push_back(CompSlice {
            .sliceId = m_nextSliceId++,
            .takeId = takeId,
            .startSample = clip.startSample,
            .endSample = clip.startSample + clip.lengthSamples,
            .crossfadeSamples = 192
        });
    }

    return takeId;
}

bool TakeCompManager::removeTake(std::uint32_t takeId)
{
    const auto it = std::remove_if(m_takes.begin(), m_takes.end(),
        [takeId](const TakeLane& t) { return t.takeId == takeId; });
    if (it == m_takes.end()) {
        return false;
    }
    m_takes.erase(it, m_takes.end());

    // Remove any comp slices referencing this take
    const auto sIt = std::remove_if(m_slices.begin(), m_slices.end(),
        [takeId](const CompSlice& s) { return s.takeId == takeId; });
    m_slices.erase(sIt, m_slices.end());

    if (m_soloedTakeId == takeId) {
        m_soloedTakeId = 0;
    }
    return true;
}

const TakeLane* TakeCompManager::findTake(std::uint32_t takeId) const noexcept
{
    for (const auto& t : m_takes) {
        if (t.takeId == takeId) {
            return &t;
        }
    }
    return nullptr;
}

void TakeCompManager::clearCompSlices() noexcept
{
    m_slices.clear();
}

void TakeCompManager::selectCompSlice(
    std::uint32_t takeId,
    std::uint64_t startSample,
    std::uint64_t endSample,
    std::uint32_t crossfadeSamples)
{
    if (endSample <= startSample) {
        return;
    }

    std::vector<CompSlice> updatedSlices;
    updatedSlices.reserve(m_slices.size() + 2);

    for (const auto& s : m_slices) {
        if (s.endSample <= startSample || s.startSample >= endSample) {
            // No overlap: keep as is
            updatedSlices.push_back(s);
        } else if (s.startSample < startSample && s.endSample > endSample) {
            // New slice splits existing slice in two
            CompSlice leftPart = s;
            leftPart.endSample = startSample;
            updatedSlices.push_back(leftPart);

            CompSlice rightPart = s;
            rightPart.sliceId = m_nextSliceId++;
            rightPart.startSample = endSample;
            updatedSlices.push_back(rightPart);
        } else if (s.startSample < startSample) {
            // Overlaps on right side: trim right
            CompSlice trimmed = s;
            trimmed.endSample = startSample;
            updatedSlices.push_back(trimmed);
        } else if (s.endSample > endSample) {
            // Overlaps on left side: trim left
            CompSlice trimmed = s;
            trimmed.startSample = endSample;
            updatedSlices.push_back(trimmed);
        }
        // Completely covered slices are dropped
    }

    // Insert new selected comp slice
    updatedSlices.push_back(CompSlice {
        .sliceId = m_nextSliceId++,
        .takeId = takeId,
        .startSample = startSample,
        .endSample = endSample,
        .crossfadeSamples = crossfadeSamples
    });

    // Sort slices chronologically
    std::sort(updatedSlices.begin(), updatedSlices.end(),
        [](const CompSlice& a, const CompSlice& b) {
            return a.startSample < b.startSample;
        });

    m_slices = std::move(updatedSlices);
}

void TakeCompManager::setTakeAuditionSolo(std::uint32_t takeId, bool solo) noexcept
{
    if (solo) {
        m_soloedTakeId = takeId;
    } else if (m_soloedTakeId == takeId) {
        m_soloedTakeId = 0;
    }

    for (auto& t : m_takes) {
        t.isAuditionSolo = (t.takeId == m_soloedTakeId);
    }
}

AudioClip TakeCompManager::flattenAndMergeComp(
    ClipId newClipId,
    MediaAssetId consolidatedAssetId,
    [[maybe_unused]] const std::string& compName) const
{
    AudioClip mergedClip;
    mergedClip.id = newClipId;
    mergedClip.assetId = consolidatedAssetId;
    mergedClip.trackId = m_trackId;
    mergedClip.gainLinear = 1.0f;
    mergedClip.warpingEnabled = false;

    if (m_slices.empty()) {
        mergedClip.startSample = 0;
        mergedClip.lengthSamples = 0;
        return mergedClip;
    }

    std::uint64_t minStart = m_slices.front().startSample;
    std::uint64_t maxEnd = m_slices.front().endSample;

    for (const auto& s : m_slices) {
        minStart = std::min(minStart, s.startSample);
        maxEnd = std::max(maxEnd, s.endSample);
    }

    mergedClip.startSample = minStart;
    mergedClip.lengthSamples = maxEnd > minStart ? (maxEnd - minStart) : 0;
    return mergedClip;
}

void TakeCompManager::applyEqualPowerCrossfade(
    float* outOutgoing,
    float* outIncoming,
    std::size_t fadeLength) noexcept
{
    if (outOutgoing == nullptr || outIncoming == nullptr || fadeLength == 0) {
        return;
    }

    for (std::size_t i = 0; i < fadeLength; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(fadeLength);
        const double theta = frac * (std::numbers::pi_v<double> * 0.5);
        const float wOut = static_cast<float>(std::cos(theta));
        const float wIn = static_cast<float>(std::sin(theta));

        outOutgoing[i] *= wOut;
        outIncoming[i] *= wIn;
    }
}

} // namespace dawg::project
