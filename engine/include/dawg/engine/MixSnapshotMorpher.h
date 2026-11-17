#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine {

struct TrackMixState
{
    std::uint64_t trackId {0};
    float volume {1.0f};
    float pan {0.0f};
    float sendA {0.0f};
    float sendB {0.0f};
    std::array<float, 8> macroKnobs {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
};

struct MixSnapshot
{
    std::string name {"Mix Snapshot"};
    std::vector<TrackMixState> tracks;
};

class MixSnapshotMorpher final
{
public:
    MixSnapshotMorpher() = default;
    ~MixSnapshotMorpher() = default;

    void setSnapshotA(MixSnapshot snapshot) noexcept;
    void setSnapshotB(MixSnapshot snapshot) noexcept;

    [[nodiscard]] const MixSnapshot& snapshotA() const noexcept { return m_snapshotA; }
    [[nodiscard]] const MixSnapshot& snapshotB() const noexcept { return m_snapshotB; }

    /// Morphs smoothly between Snapshot A (factor = 0.0) and Snapshot B (factor = 1.0).
    /// If equalPower is true, volumes are interpolated using constant power curves.
    [[nodiscard]] std::vector<TrackMixState> morph(float factor, bool equalPower = false) const;

private:
    MixSnapshot m_snapshotA;
    MixSnapshot m_snapshotB;
};

} // namespace dawg::engine
