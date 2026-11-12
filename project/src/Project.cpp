#include <dawg/project/Project.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace dawg::project {

namespace {
bool isKnownPathKind(const AudioAssetPathKind kind)
{
    return kind == AudioAssetPathKind::projectRelative || kind == AudioAssetPathKind::absolute;
}

bool isValidAsset(const AudioAsset& asset)
{
    return ! asset.path.empty() && isKnownPathKind(asset.pathKind) && asset.sampleRate != 0 &&
           asset.channelCount != 0 && asset.frameCount != 0;
}

bool isValidClipForAsset(const AudioClip& clip, const AudioAsset& asset)
{
    if (clip.sourceOffset > asset.frameCount || clip.lengthSamples == 0)
        return false;
    const auto remainingFrames = asset.frameCount - clip.sourceOffset;
    return clip.lengthSamples <= remainingFrames;
}

template <typename Id>
Id nextIdAfter(const Id id)
{
    return id == std::numeric_limits<Id>::max() ? 0 : id + 1;
}
} // namespace

Project::Project(ProjectSnapshot state, const TrackId nextTrackId, const MediaAssetId nextAssetId,
                 const ClipId nextClipId)
    : m_state {std::move(state)}, m_nextTrackId {nextTrackId}, m_nextAssetId {nextAssetId},
      m_nextClipId {nextClipId}
{
}

Project Project::create(std::string name, const ProjectId id)
{
    return Project {{2, id, std::move(name), {}, {}, {}}, 1, 1, 1};
}

std::optional<Project> Project::fromSnapshot(ProjectSnapshot snapshot)
{
    if (snapshot.formatVersion != 2 || snapshot.id == 0 || snapshot.name.empty())
        return std::nullopt;

    std::unordered_set<TrackId> ids;
    TrackId largestId {};
    for (const auto& track : snapshot.tracks)
    {
        if (track.id == 0 || track.name.empty() || ! ids.insert(track.id).second)
            return std::nullopt;

        largestId = std::max(largestId, track.id);
    }

    std::unordered_set<MediaAssetId> assetIds;
    MediaAssetId largestAssetId {};
    for (const auto& asset : snapshot.assets)
    {
        if (asset.id == 0 || ! isValidAsset(asset) || ! assetIds.insert(asset.id).second)
            return std::nullopt;
        largestAssetId = std::max(largestAssetId, asset.id);
    }

    std::unordered_set<ClipId> clipIds;
    ClipId largestClipId {};
    for (const auto& clip : snapshot.clips)
    {
        if (clip.id == 0 || ! clipIds.insert(clip.id).second)
            return std::nullopt;
        const auto asset = std::find_if(snapshot.assets.begin(), snapshot.assets.end(),
                                        [&clip](const AudioAsset& value) {
                                            return value.id == clip.assetId;
                                        });
        if (asset == snapshot.assets.end() || ! isValidClipForAsset(clip, *asset))
            return std::nullopt;
        largestClipId = std::max(largestClipId, clip.id);
    }

    return Project {std::move(snapshot), nextIdAfter(largestId), nextIdAfter(largestAssetId),
                    nextIdAfter(largestClipId)};
}

ProjectSnapshot Project::snapshot() const
{
    return m_state;
}

TrackId Project::addAudioTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({id, std::move(name), false, false});
    // Unsigned wrap to the reserved invalid ID permanently rejects further adds.
    ++m_nextTrackId;
    return id;
}

MediaAssetId Project::addAudioAsset(AudioAsset asset)
{
    if (! isValidAsset(asset))
        return 0;

    if (asset.id == 0)
    {
        if (m_nextAssetId == 0)
            return 0;
        asset.id = m_nextAssetId;
    }
    else if (std::any_of(m_state.assets.begin(), m_state.assets.end(), [&asset](const AudioAsset& value) {
                 return value.id == asset.id;
             }))
        return 0;

    m_state.assets.push_back(std::move(asset));
    const auto id = m_state.assets.back().id;
    if (id >= m_nextAssetId)
        m_nextAssetId = nextIdAfter(id);
    return id;
}

ClipId Project::addAudioClip(AudioClip clip)
{
    const auto asset = std::find_if(m_state.assets.begin(), m_state.assets.end(), [&clip](const AudioAsset& value) {
        return value.id == clip.assetId;
    });
    if (asset == m_state.assets.end() || ! isValidClipForAsset(clip, *asset))
        return 0;

    if (clip.id == 0)
    {
        if (m_nextClipId == 0)
            return 0;
        clip.id = m_nextClipId;
    }
    else if (std::any_of(m_state.clips.begin(), m_state.clips.end(), [&clip](const AudioClip& value) {
                 return value.id == clip.id;
             }))
        return 0;

    m_state.clips.push_back(std::move(clip));
    const auto id = m_state.clips.back().id;
    if (id >= m_nextClipId)
        m_nextClipId = nextIdAfter(id);
    return id;
}

bool Project::removeTrack(const TrackId id)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    m_state.tracks.erase(track);
    return true;
}

bool Project::renameTrack(const TrackId id, std::string name)
{
    if (name.empty())
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->name = std::move(name);
    return true;
}

} // namespace dawg::project
