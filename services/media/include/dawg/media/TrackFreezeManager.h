#pragma once

#include <dawg/media/OfflineRenderService.h>
#include <dawg/project/Project.h>

#include <expected>
#include <filesystem>
#include <string>

namespace dawg::media {

class TrackFreezeManager
{
public:
    TrackFreezeManager() = default;
    ~TrackFreezeManager() = default;

    // Freeze a track: renders the track's audio output with all inserts and routing to a 32-bit float BWF cache
    // and stashes original inserts in frozenInserts on the track model.
    [[nodiscard]] std::expected<std::string, MediaError> freezeTrack(
        project::Project& project,
        project::TrackId trackId,
        const std::filesystem::path& freezeDirectory,
        OfflineRenderService& renderService);

    // Unfreeze a track: restores original inserts from frozenInserts, clears the freeze state,
    // and optionally removes the cached audio file from disk.
    [[nodiscard]] std::expected<void, MediaError> unfreezeTrack(
        project::Project& project,
        project::TrackId trackId,
        bool removeCachedFile = true);

    // Checks if a track is currently frozen
    [[nodiscard]] static bool isTrackFrozen(
        const project::ProjectSnapshot& snapshot,
        project::TrackId trackId) noexcept;
};

} // namespace dawg::media
