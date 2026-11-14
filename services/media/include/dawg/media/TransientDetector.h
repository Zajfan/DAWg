#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::media {

struct TransientMarker
{
    std::uint64_t sampleOffset {0};
    float strength {0.0f};

    friend bool operator==(const TransientMarker&, const TransientMarker&) = default;
};

class TransientDetector final
{
public:
    // Detects transient onsets across an audio signal using High-Frequency Energy flux
    [[nodiscard]] static std::vector<TransientMarker> detect(
        std::span<const float> samples,
        double sampleRate = 48000.0,
        float sensitivity = 1.5f) noexcept;
};

} // namespace dawg::media
