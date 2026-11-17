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
                 const ClipId nextClipId, const NoteId nextNoteId,
                 const AutomationLaneId nextLaneId, const AutomationPointId nextPointId)
    : m_state {std::move(state)}, m_nextTrackId {nextTrackId}, m_nextAssetId {nextAssetId},
      m_nextClipId {nextClipId}, m_nextNoteId {nextNoteId}, m_nextLaneId {nextLaneId},
      m_nextPointId {nextPointId}
{
}

Project Project::create(std::string name, const ProjectId id)
{
    return Project {{2, id, std::move(name), {}, {}, {}, {}, {}, TempoSetting {}, LoopSetting {}}, 1, 1, 1, 1, 1, 1};
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

    NoteId largestNoteId {};
    for (const auto& mClip : snapshot.midiClips)
    {
        if (mClip.id == 0 || ! clipIds.insert(mClip.id).second)
            return std::nullopt;
        if (mClip.trackId != 0 && ! ids.contains(mClip.trackId))
            return std::nullopt;
        largestClipId = std::max(largestClipId, mClip.id);

        std::unordered_set<NoteId> noteIds;
        for (const auto& note : mClip.notes)
        {
            if (note.id == 0 || note.pitch > 127 || ! noteIds.insert(note.id).second)
                return std::nullopt;
            largestNoteId = std::max(largestNoteId, note.id);
        }
    }

    std::unordered_set<AutomationLaneId> laneIds;
    AutomationLaneId largestLaneId {};
    AutomationPointId largestPointId {};

    for (const auto& lane : snapshot.automationLanes)
    {
        if (lane.id == 0 || ! laneIds.insert(lane.id).second)
            return std::nullopt;
        if (lane.target.trackId != 0 && ! ids.contains(lane.target.trackId))
            return std::nullopt;
        largestLaneId = std::max(largestLaneId, lane.id);

        std::unordered_set<AutomationPointId> pointIds;
        for (const auto& pt : lane.points)
        {
            if (pt.id == 0 || ! pointIds.insert(pt.id).second)
                return std::nullopt;
            largestPointId = std::max(largestPointId, pt.id);
        }
    }

    return Project {std::move(snapshot), nextIdAfter(largestId), nextIdAfter(largestAssetId),
                    nextIdAfter(largestClipId), nextIdAfter(largestNoteId),
                    nextIdAfter(largestLaneId), nextIdAfter(largestPointId)};
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

TrackId Project::addInstrumentTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({
        .id = id,
        .name = std::move(name),
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::instrument
    });
    ++m_nextTrackId;
    return id;
}

TrackId Project::addAuxReturnTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({
        .id = id,
        .name = std::move(name),
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::auxReturn
    });
    ++m_nextTrackId;
    return id;
}

TrackId Project::addSubmixGroupTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({
        .id = id,
        .name = std::move(name),
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::submixGroup
    });
    ++m_nextTrackId;
    return id;
}

TrackId Project::addFolderTrack(std::string name)
{
    if (name.empty() || m_nextTrackId == 0)
        return 0;

    const auto id = m_nextTrackId;
    m_state.tracks.push_back({
        .id = id,
        .name = std::move(name),
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::folder,
        .inserts = {},
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = true,
        .folderCollapsed = false,
        .compGroupId = 0
    });
    ++m_nextTrackId;
    return id;
}

bool Project::setTrackParentFolder(const TrackId trackId, const TrackId folderId)
{
    if (trackId == 0 || trackId == folderId)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    if (folderId != 0)
    {
        const auto folder = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                         [folderId](const Track& t) { return t.id == folderId; });
        if (folder == m_state.tracks.end() || (!folder->isFolder && folder->kind != TrackKind::folder))
            return false;
    }

    track->parentFolderId = folderId;
    return true;
}

bool Project::setFolderCollapsed(const TrackId folderId, const bool collapsed)
{
    if (folderId == 0)
        return false;

    const auto folder = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                     [folderId](const Track& t) { return t.id == folderId; });
    if (folder == m_state.tracks.end() || (!folder->isFolder && folder->kind != TrackKind::folder))
        return false;

    folder->folderCollapsed = collapsed;
    return true;
}

bool Project::setTrackCompGroup(const TrackId trackId, const std::uint32_t compGroupId)
{
    if (trackId == 0)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    track->compGroupId = compGroupId;
    return true;
}

bool Project::addTrackSend(const TrackId trackId, TrackSend send)
{
    if (trackId == 0 || send.targetTrackId == 0 || trackId == send.targetTrackId)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    const auto target = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                     [targetId = send.targetTrackId](const Track& t) { return t.id == targetId; });
    if (track == m_state.tracks.end() || target == m_state.tracks.end())
        return false;

    if (std::any_of(track->sends.begin(), track->sends.end(),
                    [targetId = send.targetTrackId](const TrackSend& s) { return s.targetTrackId == targetId; }))
    {
        return false;
    }

    track->sends.push_back(send);
    return true;
}

bool Project::removeTrackSend(const TrackId trackId, const TrackId targetTrackId)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    const auto it = std::find_if(track->sends.begin(), track->sends.end(),
                                 [targetTrackId](const TrackSend& s) { return s.targetTrackId == targetTrackId; });
    if (it == track->sends.end())
        return false;

    track->sends.erase(it);
    return true;
}

bool Project::setTrackSendLevel(const TrackId trackId, const TrackId targetTrackId, const float levelLinear)
{
    if (! std::isfinite(levelLinear) || levelLinear < 0.0f)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    const auto it = std::find_if(track->sends.begin(), track->sends.end(),
                                 [targetTrackId](const TrackSend& s) { return s.targetTrackId == targetTrackId; });
    if (it == track->sends.end())
        return false;

    it->levelLinear = levelLinear;
    return true;
}

bool Project::setTrackOutputBus(const TrackId trackId, const TrackId outputBusTrackId)
{
    if (trackId == 0 || trackId == outputBusTrackId)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    if (outputBusTrackId != 0)
    {
        const auto bus = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                      [outputBusTrackId](const Track& t) { return t.id == outputBusTrackId; });
        if (bus == m_state.tracks.end() || bus->kind != TrackKind::submixGroup)
            return false;
    }

    track->outputBusTrackId = outputBusTrackId;
    return true;
}

bool Project::addTakeToTrack(const TrackId trackId, Take take)
{
    if (trackId == 0 || take.lengthSamples == 0)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    if (take.id == 0)
    {
        take.id = static_cast<std::uint32_t>(track->takes.size() + 1);
    }
    track->takes.push_back(std::move(take));
    return true;
}

bool Project::toggleTakeLanesExpanded(const TrackId trackId)
{
    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    track->takeLanesExpanded = ! track->takeLanesExpanded;
    return true;
}

bool Project::promoteTakeRegion(const TrackId trackId, const std::uint32_t takeId,
                                const std::uint64_t startSample, const std::uint64_t lengthSamples)
{
    if (trackId == 0 || lengthSamples == 0)
        return false;

    const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                    [trackId](const Track& t) { return t.id == trackId; });
    if (track == m_state.tracks.end())
        return false;

    const auto takeIt = std::find_if(track->takes.begin(), track->takes.end(),
                                     [takeId](const Take& tk) { return tk.id == takeId; });
    if (takeIt == track->takes.end())
        return false;

    const auto promoteStart = startSample;
    const auto promoteEnd = startSample + lengthSamples;

    std::vector<AudioClip> preservedClips;
    for (auto& clip : m_state.clips)
    {
        if (clip.trackId != trackId)
        {
            preservedClips.push_back(clip);
            continue;
        }

        const auto clipStart = clip.startSample;
        const auto clipEnd = clip.startSample + clip.lengthSamples;

        if (clipEnd <= promoteStart || clipStart >= promoteEnd)
        {
            preservedClips.push_back(clip);
            continue;
        }

        if (clipStart >= promoteStart && clipEnd <= promoteEnd)
        {
            continue;
        }

        if (clipStart < promoteStart && clipEnd <= promoteEnd)
        {
            clip.lengthSamples = promoteStart - clipStart;
            preservedClips.push_back(clip);
            continue;
        }

        if (clipStart >= promoteStart && clipEnd > promoteEnd)
        {
            const auto cut = promoteEnd - clipStart;
            clip.startSample = promoteEnd;
            clip.sourceOffset += cut;
            clip.lengthSamples -= cut;
            preservedClips.push_back(clip);
            continue;
        }

        if (clipStart < promoteStart && clipEnd > promoteEnd)
        {
            AudioClip rightPart = clip;
            rightPart.id = m_nextClipId;
            m_nextClipId = nextIdAfter(m_nextClipId);
            rightPart.startSample = promoteEnd;
            rightPart.sourceOffset = clip.sourceOffset + (promoteEnd - clipStart);
            rightPart.lengthSamples = clipEnd - promoteEnd;

            clip.lengthSamples = promoteStart - clipStart;

            preservedClips.push_back(clip);
            preservedClips.push_back(rightPart);
            continue;
        }
    }

    m_state.clips = std::move(preservedClips);

    constexpr std::uint32_t kCrossfadeSamples = 256;
    AudioClip promotedClip {
        .id = m_nextClipId,
        .assetId = takeIt->assetId,
        .startSample = promoteStart,
        .sourceOffset = takeIt->sourceOffset,
        .lengthSamples = lengthSamples,
        .trackId = trackId,
        .gainLinear = 1.0f,
        .fadeInSamples = kCrossfadeSamples,
        .fadeOutSamples = kCrossfadeSamples,
        .warpingEnabled = false,
        .warpMarkers = {}
    };
    m_nextClipId = nextIdAfter(m_nextClipId);
    m_state.clips.push_back(std::move(promotedClip));
    return true;
}

bool Project::addTrackInsert(const TrackId trackId, DspNodeDescriptor descriptor)
{
    const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                 [trackId](const Track& t) { return t.id == trackId; });
    if (it == m_state.tracks.end())
        return false;

    for (auto& ins : it->inserts)
    {
        if (ins.slotIndex == descriptor.slotIndex)
        {
            ins = std::move(descriptor);
            return true;
        }
    }
    it->inserts.push_back(std::move(descriptor));
    return true;
}

bool Project::removeTrackInsert(const TrackId trackId, const std::uint32_t slotIndex)
{
    const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                 [trackId](const Track& t) { return t.id == trackId; });
    if (it == m_state.tracks.end())
        return false;

    const auto insIt = std::find_if(it->inserts.begin(), it->inserts.end(),
                                    [slotIndex](const DspNodeDescriptor& d) { return d.slotIndex == slotIndex; });
    if (insIt == it->inserts.end())
        return false;

    it->inserts.erase(insIt);
    return true;
}

bool Project::setTrackInsertBypassed(const TrackId trackId, const std::uint32_t slotIndex, const bool bypassed)
{
    const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                 [trackId](const Track& t) { return t.id == trackId; });
    if (it == m_state.tracks.end())
        return false;

    const auto insIt = std::find_if(it->inserts.begin(), it->inserts.end(),
                                    [slotIndex](const DspNodeDescriptor& d) { return d.slotIndex == slotIndex; });
    if (insIt == it->inserts.end())
        return false;

    insIt->bypassed = bypassed;
    return true;
}

bool Project::freezeTrack(const TrackId id, std::string freezeAudioPath)
{
    const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                 [id](const Track& t) { return t.id == id; });
    if (it == m_state.tracks.end() || it->isFrozen)
        return false;

    it->isFrozen = true;
    it->freezeAudioPath = std::move(freezeAudioPath);
    it->frozenInserts = std::move(it->inserts);
    it->inserts.clear();
    return true;
}

bool Project::unfreezeTrack(const TrackId id)
{
    const auto it = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                 [id](const Track& t) { return t.id == id; });
    if (it == m_state.tracks.end() || ! it->isFrozen)
        return false;

    it->isFrozen = false;
    it->freezeAudioPath.clear();
    it->inserts = std::move(it->frozenInserts);
    it->frozenInserts.clear();
    return true;
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

MidiClipId Project::addMidiClip(MidiClip clip)
{
    if (clip.trackId != 0)
    {
        const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                        [&clip](const Track& t) { return t.id == clip.trackId; });
        if (track == m_state.tracks.end())
            return 0;
    }

    if (clip.id == 0)
    {
        if (m_nextClipId == 0)
            return 0;
        clip.id = m_nextClipId;
    }
    else if (std::any_of(m_state.clips.begin(), m_state.clips.end(), [&clip](const AudioClip& c) { return c.id == clip.id; })
             || std::any_of(m_state.midiClips.begin(), m_state.midiClips.end(), [&clip](const MidiClip& c) { return c.id == clip.id; }))
    {
        return 0;
    }

    m_state.midiClips.push_back(std::move(clip));
    const auto id = m_state.midiClips.back().id;
    if (id >= m_nextClipId)
        m_nextClipId = nextIdAfter(id);
    return id;
}

bool Project::removeMidiClip(const MidiClipId id)
{
    if (id == 0)
        return false;
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [id](const MidiClip& c) { return c.id == id; });
    if (it == m_state.midiClips.end())
        return false;
    m_state.midiClips.erase(it);
    return true;
}

NoteId Project::addMidiNote(const MidiClipId clipId, MidiNote note)
{
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [clipId](const MidiClip& c) { return c.id == clipId; });
    if (it == m_state.midiClips.end() || note.pitch > 127)
        return 0;

    if (note.id == 0)
    {
        if (m_nextNoteId == 0)
            return 0;
        note.id = m_nextNoteId;
    }
    else if (std::any_of(it->notes.begin(), it->notes.end(), [&note](const MidiNote& n) { return n.id == note.id; }))
    {
        return 0;
    }

    it->notes.push_back(std::move(note));
    const auto id = it->notes.back().id;
    if (id >= m_nextNoteId)
        m_nextNoteId = nextIdAfter(id);
    return id;
}

bool Project::removeMidiNote(const MidiClipId clipId, const NoteId noteId)
{
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [clipId](const MidiClip& c) { return c.id == clipId; });
    if (it == m_state.midiClips.end())
        return false;

    const auto noteIt = std::find_if(it->notes.begin(), it->notes.end(),
                                     [noteId](const MidiNote& n) { return n.id == noteId; });
    if (noteIt == it->notes.end())
        return false;
    it->notes.erase(noteIt);
    return true;
}

bool Project::moveMidiNote(const MidiClipId clipId, const NoteId noteId, const std::uint64_t newStartSample, const std::uint8_t newPitch)
{
    if (newPitch > 127)
        return false;
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [clipId](const MidiClip& c) { return c.id == clipId; });
    if (it == m_state.midiClips.end())
        return false;

    const auto noteIt = std::find_if(it->notes.begin(), it->notes.end(),
                                     [noteId](const MidiNote& n) { return n.id == noteId; });
    if (noteIt == it->notes.end())
        return false;
    noteIt->startSample = newStartSample;
    noteIt->pitch = newPitch;
    return true;
}

bool Project::resizeMidiNote(const MidiClipId clipId, const NoteId noteId, const std::uint64_t newLengthSamples)
{
    if (newLengthSamples == 0)
        return false;
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [clipId](const MidiClip& c) { return c.id == clipId; });
    if (it == m_state.midiClips.end())
        return false;

    const auto noteIt = std::find_if(it->notes.begin(), it->notes.end(),
                                     [noteId](const MidiNote& n) { return n.id == noteId; });
    if (noteIt == it->notes.end())
        return false;
    noteIt->lengthSamples = newLengthSamples;
    return true;
}

bool Project::setMidiNoteVelocity(const MidiClipId clipId, const NoteId noteId, const std::uint8_t newVelocity)
{
    if (newVelocity > 127)
        return false;
    const auto it = std::find_if(m_state.midiClips.begin(), m_state.midiClips.end(),
                                 [clipId](const MidiClip& c) { return c.id == clipId; });
    if (it == m_state.midiClips.end())
        return false;

    const auto noteIt = std::find_if(it->notes.begin(), it->notes.end(),
                                     [noteId](const MidiNote& n) { return n.id == noteId; });
    if (noteIt == it->notes.end())
        return false;
    noteIt->velocity = newVelocity;
    return true;
}

AutomationLaneId Project::addAutomationLane(AutomationLane lane)
{
    if (lane.target.trackId != 0)
    {
        const auto track = std::find_if(m_state.tracks.begin(), m_state.tracks.end(),
                                        [&lane](const Track& t) { return t.id == lane.target.trackId; });
        if (track == m_state.tracks.end())
            return 0;
    }

    if (lane.id == 0)
    {
        if (m_nextLaneId == 0)
            return 0;
        lane.id = m_nextLaneId;
    }
    else if (std::any_of(m_state.automationLanes.begin(), m_state.automationLanes.end(),
                         [&lane](const AutomationLane& l) { return l.id == lane.id; }))
    {
        return 0;
    }

    m_state.automationLanes.push_back(std::move(lane));
    const auto id = m_state.automationLanes.back().id;
    if (id >= m_nextLaneId)
        m_nextLaneId = nextIdAfter(id);
    return id;
}

bool Project::removeAutomationLane(const AutomationLaneId laneId)
{
    if (laneId == 0)
        return false;
    const auto it = std::find_if(m_state.automationLanes.begin(), m_state.automationLanes.end(),
                                 [laneId](const AutomationLane& l) { return l.id == laneId; });
    if (it == m_state.automationLanes.end())
        return false;
    m_state.automationLanes.erase(it);
    return true;
}

AutomationPointId Project::addAutomationPoint(const AutomationLaneId laneId, AutomationPoint point)
{
    const auto it = std::find_if(m_state.automationLanes.begin(), m_state.automationLanes.end(),
                                 [laneId](const AutomationLane& l) { return l.id == laneId; });
    if (it == m_state.automationLanes.end() || ! std::isfinite(point.value) || ! std::isfinite(point.curveTension))
        return 0;

    point.curveTension = std::clamp(point.curveTension, -1.0f, 1.0f);

    if (point.id == 0)
    {
        if (m_nextPointId == 0)
            return 0;
        point.id = m_nextPointId;
    }
    else if (std::any_of(it->points.begin(), it->points.end(),
                         [&point](const AutomationPoint& p) { return p.id == point.id; }))
    {
        return 0;
    }

    const auto insertPos = std::upper_bound(it->points.begin(), it->points.end(), point,
                                            [](const AutomationPoint& a, const AutomationPoint& b) {
                                                return a.samplePosition < b.samplePosition;
                                            });
    it->points.insert(insertPos, point);
    const auto id = point.id;
    if (id >= m_nextPointId)
        m_nextPointId = nextIdAfter(id);
    return id;
}

bool Project::removeAutomationPoint(const AutomationLaneId laneId, const AutomationPointId pointId)
{
    const auto it = std::find_if(m_state.automationLanes.begin(), m_state.automationLanes.end(),
                                 [laneId](const AutomationLane& l) { return l.id == laneId; });
    if (it == m_state.automationLanes.end())
        return false;

    const auto ptIt = std::find_if(it->points.begin(), it->points.end(),
                                   [pointId](const AutomationPoint& p) { return p.id == pointId; });
    if (ptIt == it->points.end())
        return false;
    it->points.erase(ptIt);
    return true;
}

bool Project::moveAutomationPoint(const AutomationLaneId laneId, const AutomationPointId pointId,
                                  const std::uint64_t newSamplePosition, const float newValue,
                                  const float newTension)
{
    if (! std::isfinite(newValue) || ! std::isfinite(newTension))
        return false;

    const auto it = std::find_if(m_state.automationLanes.begin(), m_state.automationLanes.end(),
                                 [laneId](const AutomationLane& l) { return l.id == laneId; });
    if (it == m_state.automationLanes.end())
        return false;

    const auto ptIt = std::find_if(it->points.begin(), it->points.end(),
                                   [pointId](const AutomationPoint& p) { return p.id == pointId; });
    if (ptIt == it->points.end())
        return false;

    ptIt->samplePosition = newSamplePosition;
    ptIt->value = newValue;
    ptIt->curveTension = std::clamp(newTension, -1.0f, 1.0f);

    std::sort(it->points.begin(), it->points.end(), [](const auto& a, const auto& b) {
        return a.samplePosition < b.samplePosition;
    });
    return true;
}

} // namespace dawg::project
