#include <dawg/engine/MixSnapshotMorpher.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace dawg::engine {

void MixSnapshotMorpher::setSnapshotA(MixSnapshot snapshot) noexcept
{
    m_snapshotA = std::move(snapshot);
}

void MixSnapshotMorpher::setSnapshotB(MixSnapshot snapshot) noexcept
{
    m_snapshotB = std::move(snapshot);
}

std::vector<TrackMixState> MixSnapshotMorpher::morph(const float factor, const bool equalPower) const
{
    const float clampedFactor = std::clamp(factor, 0.0f, 1.0f);

    // Fast index for snapshot B
    std::unordered_map<std::uint64_t, const TrackMixState*> bMap;
    for (const auto& tb : m_snapshotB.tracks)
    {
        bMap[tb.trackId] = &tb;
    }

    std::vector<TrackMixState> morphed;
    morphed.reserve(m_snapshotA.tracks.size());

    constexpr float piOver2 = 1.5707963267948966f;
    const float gainA = equalPower ? std::cos(clampedFactor * piOver2) : (1.0f - clampedFactor);
    const float gainB = equalPower ? std::sin(clampedFactor * piOver2) : clampedFactor;

    for (const auto& ta : m_snapshotA.tracks)
    {
        TrackMixState outState = ta;
        const auto it = bMap.find(ta.trackId);
        if (it != bMap.end())
        {
            const auto* tb = it->second;
            // Interpolate volume
            if (equalPower)
            {
                outState.volume = ta.volume * gainA + tb->volume * gainB;
            }
            else
            {
                outState.volume = ta.volume + (tb->volume - ta.volume) * clampedFactor;
            }

            // Interpolate pan
            outState.pan = ta.pan + (tb->pan - ta.pan) * clampedFactor;

            // Interpolate sends
            outState.sendA = ta.sendA + (tb->sendA - ta.sendA) * clampedFactor;
            outState.sendB = ta.sendB + (tb->sendB - ta.sendB) * clampedFactor;

            // Interpolate macros
            for (std::size_t m = 0; m < 8; ++m)
            {
                outState.macroKnobs[m] = ta.macroKnobs[m] + (tb->macroKnobs[m] - ta.macroKnobs[m]) * clampedFactor;
            }
        }
        morphed.push_back(outState);
    }

    return morphed;
}

} // namespace dawg::engine
