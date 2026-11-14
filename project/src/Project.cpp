#include <dawg/project/Project.h>

#include <algorithm>
#include <cmath>
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
    if ((snapshot.formatVersion != 2 && snapshot.formatVersion != 3)
        || snapshot.id == 0 || snapshot.name.empty())
        return std::nullopt;

    if (! std::isfinite(snapshot.tempo.bpm) || snapshot.tempo.bpm <= 0.0
        || snapshot.tempo.timeSigNumerator == 0 || snapshot.tempo.timeSigDenominator == 0)
        return std::nullopt;

    if (snapshot.loop.enabled && snapshot.loop.endSample <= snapshot.loop.startSample)
        return std::nullopt;

    std::unordered_set<TrackId> ids;
    TrackId largestId {};
    for (const auto& track : snapshot.tracks)
    {
        if (track.id == 0 || track.name.empty()
            || ! std::isfinite(track.gainLinear) || track.gainLinear < 0.0f
            || ! std::isfinite(track.pan) || track.pan < -1.0f || track.pan > 1.0f
            || ! ids.insert(track.id).second)
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
        if (clip.id == 0 || ! std::isfinite(clip.gainLinear) || clip.gainLinear < 0.0f
            || ! clipIds.insert(clip.id).second)
            return std::nullopt;
        if (clip.trackId != 0 && ! ids.contains(clip.trackId))
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

bool Project::setTrackMute(const TrackId id, const bool muted)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->muted = muted;
    return true;
}

bool Project::setTrackSolo(const TrackId id, const bool solo)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->solo = solo;
    return true;
}

bool Project::setTrackGainPan(const TrackId id, const float gainLinear, const float pan,
                              const bool phaseInvert)
{
    if (! std::isfinite(gainLinear) || gainLinear < 0.0f
        || ! std::isfinite(pan) || pan < -1.0f || pan > 1.0f)
    {
        return false;
    }

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->gainLinear = gainLinear;
    track->pan = pan;
    track->phaseInvert = phaseInvert;
    return true;
}

bool Project::setTrackRecordArm(const TrackId id, const bool armed)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                   [id](const Track& value) { return value.id == id; });
    if (track == m_state.tracks.end())
        return false;

    track->recordArmed = armed;
    return true;
}

bool Project::reorderTracks(const std::vector<TrackId>& trackIds)
{
    if (trackIds.size() != m_state.tracks.size())
        return false;

    std::vector<Track> reordered;
    reordered.reserve(trackIds.size());
    for (const auto id : trackIds)
    {
        const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                     [id](const Track& t) { return t.id == id; });
        if (it == m_state.tracks.end())
            return false;
        if (std::any_of(reordered.begin(), reordered.end(),
                        [id](const Track& t) { return t.id == id; }))
        {
            return false;
        }
        reordered.push_back(*it);
    }

    m_state.tracks = std::move(reordered);
    return true;
}

bool Project::removeClip(const ClipId id)
{
    const auto it = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                 [id](const AudioClip& c) { return c.id == id; });
    if (it == m_state.clips.end())
        return false;

    m_state.clips.erase(it);
    return true;
}

bool Project::moveClip(const ClipId id, const TrackId newTrackId, const std::uint64_t newStartSample)
{
    const auto clipIt = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                     [id](const AudioClip& c) { return c.id == id; });
    if (clipIt == m_state.clips.end())
        return false;

    if (newTrackId != 0 && std::none_of(m_state.tracks.begin(), m_state.tracks.end(),
                                        [newTrackId](const Track& t) { return t.id == newTrackId; }))
    {
        return false;
    }

    clipIt->trackId = newTrackId;
    clipIt->startSample = newStartSample;
    return true;
}

bool Project::trimClip(const ClipId id, const std::uint64_t newSourceOffset,
                       const std::uint64_t newLengthSamples)
{
    if (newLengthSamples == 0)
        return false;

    const auto clipIt = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                     [id](const AudioClip& c) { return c.id == id; });
    if (clipIt == m_state.clips.end())
        return false;

    const auto assetIt = std::find_if(m_state.assets.begin(), m_state.assets.end(),
                                      [assetId = clipIt->assetId](const AudioAsset& a) {
                                          return a.id == assetId;
                                      });
    if (assetIt == m_state.assets.end())
        return false;

    if (newSourceOffset > assetIt->frameCount
        || newLengthSamples > assetIt->frameCount - newSourceOffset)
    {
        return false;
    }

    clipIt->sourceOffset = newSourceOffset;
    clipIt->lengthSamples = newLengthSamples;
    return true;
}

bool Project::splitClip(const ClipId id, const std::uint64_t splitSample)
{
    const auto clipIt = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                     [id](const AudioClip& c) { return c.id == id; });
    if (clipIt == m_state.clips.end())
        return false;

    if (splitSample <= clipIt->startSample
        || splitSample >= clipIt->startSample + clipIt->lengthSamples)
    {
        return false;
    }

    if (m_nextClipId == 0)
        return false;

    const auto offsetInClip = splitSample - clipIt->startSample;
    const auto part2Length = clipIt->lengthSamples - offsetInClip;

    AudioClip part2 = *clipIt;
    part2.id = m_nextClipId;
    part2.startSample = splitSample;
    part2.sourceOffset = clipIt->sourceOffset + offsetInClip;
    part2.lengthSamples = part2Length;
    part2.fadeInSamples = 0;

    clipIt->lengthSamples = offsetInClip;
    clipIt->fadeOutSamples = 0;

    m_nextClipId = nextIdAfter(m_nextClipId);
    m_state.clips.push_back(std::move(part2));
    return true;
}

bool Project::duplicateClip(const ClipId id, const std::uint64_t newStartSample)
{
    const auto clipIt = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                     [id](const AudioClip& c) { return c.id == id; });
    if (clipIt == m_state.clips.end() || m_nextClipId == 0)
        return false;

    AudioClip dup = *clipIt;
    dup.id = m_nextClipId;
    dup.startSample = newStartSample;
    m_nextClipId = nextIdAfter(m_nextClipId);
    m_state.clips.push_back(std::move(dup));
    return true;
}

bool Project::setClipGainFade(const ClipId id, const float gainLinear,
                              const std::uint32_t fadeInSamples,
                              const std::uint32_t fadeOutSamples)
{
    if (! std::isfinite(gainLinear) || gainLinear < 0.0f)
        return false;

    const auto clipIt = std::find_if(m_state.clips.begin(), m_state.clips.end(),
                                     [id](const AudioClip& c) { return c.id == id; });
    if (clipIt == m_state.clips.end())
        return false;

    clipIt->gainLinear = gainLinear;
    clipIt->fadeInSamples = fadeInSamples;
    clipIt->fadeOutSamples = fadeOutSamples;
    return true;
}

bool Project::setProjectTempo(const double bpm, const std::uint8_t numerator,
                              const std::uint8_t denominator, const bool metronomeEnabled)
{
    if (! std::isfinite(bpm) || bpm <= 0.0 || numerator == 0 || denominator == 0)
        return false;

    m_state.tempo = TempoSetting {
        .bpm = bpm,
        .timeSigNumerator = numerator,
        .timeSigDenominator = denominator,
        .metronomeEnabled = metronomeEnabled
    };
    return true;
}

bool Project::setProjectLoop(const bool enabled, const std::uint64_t startSample,
                             const std::uint64_t endSample)
{
    if (enabled && endSample <= startSample)
        return false;

    m_state.loop = LoopSetting {
        .enabled = enabled,
        .startSample = startSample,
        .endSample = endSample
    };
    return true;
}

} // namespace dawg::project
