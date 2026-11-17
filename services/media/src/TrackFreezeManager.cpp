#include <dawg/media/TrackFreezeManager.h>

#include <algorithm>
#include <system_error>

namespace dawg::media {

namespace fs = std::filesystem;

std::expected<std::string, MediaError> TrackFreezeManager::freezeTrack(
    project::Project& project,
    project::TrackId trackId,
    const fs::path& freezeDirectory,
    OfflineRenderService& renderService)
{
    const auto snapshot = project.snapshot();
    const auto it = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                 [trackId](const project::Track& t) { return t.id == trackId; });
    if (it == snapshot.tracks.end())
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidPath,
            "Track not found in project"
        });
    }

    if (it->isFrozen)
    {
        return std::unexpected(MediaError {
            MediaError::Code::cannotOpen,
            "Track is already frozen"
        });
    }

    std::error_code ec;
    fs::create_directories(freezeDirectory, ec);
    if (ec)
    {
        return std::unexpected(MediaError {
            MediaError::Code::cannotOpen,
            "Failed to create freeze directory: " + ec.message()
        });
    }

    const auto freezeFilePath = freezeDirectory / ("freeze_track_" + std::to_string(trackId) + ".wav");

    RenderOptions options;
    options.destinationPath = freezeFilePath.string();
    options.bitDepth = ExportBitDepth::float32;
    options.sampleRate = 48000;
    options.applyTpdfDither = false;
    options.exportStems = false;
    options.stemTrackIds = {trackId};

    const auto renderRes = renderService.render(snapshot, options);
    if (!renderRes)
    {
        return std::unexpected(renderRes.error());
    }

    if (!project.freezeTrack(trackId, freezeFilePath.string()))
    {
        return std::unexpected(MediaError {
            MediaError::Code::cannotOpen,
            "Failed to update project track freeze state"
        });
    }

    return freezeFilePath.string();
}

std::expected<void, MediaError> TrackFreezeManager::unfreezeTrack(
    project::Project& project,
    project::TrackId trackId,
    bool removeCachedFile)
{
    const auto snapshot = project.snapshot();
    const auto it = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                 [trackId](const project::Track& t) { return t.id == trackId; });
    if (it == snapshot.tracks.end())
    {
        return std::unexpected(MediaError {
            MediaError::Code::invalidPath,
            "Track not found in project"
        });
    }

    if (!it->isFrozen)
    {
        return std::unexpected(MediaError {
            MediaError::Code::cannotOpen,
            "Track is not frozen"
        });
    }

    const auto cachedPath = it->freezeAudioPath;

    if (!project.unfreezeTrack(trackId))
    {
        return std::unexpected(MediaError {
            MediaError::Code::cannotOpen,
            "Failed to unfreeze track"
        });
    }

    if (removeCachedFile && !cachedPath.empty())
    {
        std::error_code ec;
        fs::remove(cachedPath, ec);
    }

    return {};
}

bool TrackFreezeManager::isTrackFrozen(
    const project::ProjectSnapshot& snapshot,
    project::TrackId trackId) noexcept
{
    const auto it = std::find_if(snapshot.tracks.begin(), snapshot.tracks.end(),
                                 [trackId](const project::Track& t) { return t.id == trackId; });
    return (it != snapshot.tracks.end()) && it->isFrozen;
}

} // namespace dawg::media
