#pragma once

#include <dawg/engine/WsolaTimeStretcher.h>
#include <dawg/media/TransientDetector.h>
#include <dawg/project/ProjectTypes.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::media {

class AudioWarpService
{
public:
    AudioWarpService() = default;
    ~AudioWarpService() = default;

    /// Automatically detects onsets / transients and quantizes them to project beat grid (quarter notes).
    [[nodiscard]] static std::vector<project::WarpMarker> autoDetectBeatWarpMarkers(
        std::span<const float> monoAudio,
        double sampleRate,
        double projectBpm,
        float sensitivity = 1.8f);

    /// Warps multi-channel audio according to warp markers slice by slice using WSOLA time-stretching.
    [[nodiscard]] static std::vector<std::vector<float>> renderWarpedAudio(
        std::span<const std::span<const float>> inputChannels,
        const std::vector<project::WarpMarker>& markers,
        std::uint64_t totalSourceFrames);
};

} // namespace dawg::media
