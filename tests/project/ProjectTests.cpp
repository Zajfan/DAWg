#include <dawg/project/Project.h>
#include <dawg/project/TakeLaneComping.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <type_traits>

namespace {
using namespace dawg::project;

static_assert(std::is_same_v<ProjectId, std::uint64_t>);
static_assert(std::is_same_v<TrackId, std::uint64_t>);
static_assert(std::is_same_v<MediaAssetId, std::uint64_t>);
static_assert(std::is_same_v<ClipId, std::uint64_t>);

void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

// Encode every value, length-prefixing strings so arbitrary names are unambiguous.
// This is a test comparison helper, not a project file format.
std::string serialize(const ProjectSnapshot& snapshot)
{
    std::string bytes;
    const auto appendInteger = [&bytes](const std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
            bytes.push_back(static_cast<char>((value >> shift) & 0xff));
    };
    const auto appendString = [&bytes, &appendInteger](const std::string& value)
    {
        appendInteger(value.size());
        bytes.append(value);
    };
    appendInteger(snapshot.formatVersion);
    appendInteger(snapshot.id);
    appendString(snapshot.name);
    appendInteger(snapshot.tracks.size());
    for (const auto& track : snapshot.tracks)
    {
        appendInteger(track.id);
        appendString(track.name);
        bytes.push_back(track.muted ? '\1' : '\0');
        bytes.push_back(track.solo ? '\1' : '\0');
        bytes.push_back(static_cast<char>(track.kind));
    }
    appendInteger(snapshot.assets.size());
    for (const auto& asset : snapshot.assets)
    {
        appendInteger(asset.id);
        appendString(asset.path);
        appendInteger(static_cast<std::uint8_t>(asset.pathKind));
        appendInteger(asset.sampleRate);
        appendInteger(asset.channelCount);
        appendInteger(asset.frameCount);
    }
    appendInteger(snapshot.clips.size());
    for (const auto& clip : snapshot.clips)
    {
        appendInteger(clip.id);
        appendInteger(clip.assetId);
        appendInteger(clip.startSample);
        appendInteger(clip.sourceOffset);
        appendInteger(clip.lengthSamples);
    }
    appendInteger(snapshot.midiClips.size());
    for (const auto& mClip : snapshot.midiClips)
    {
        appendInteger(mClip.id);
        appendInteger(mClip.trackId);
        appendInteger(mClip.startSample);
        appendInteger(mClip.lengthSamples);
        appendInteger(mClip.notes.size());
        for (const auto& note : mClip.notes)
        {
            appendInteger(note.id);
            bytes.push_back(static_cast<char>(note.pitch));
            bytes.push_back(static_cast<char>(note.velocity));
            appendInteger(note.startSample);
            appendInteger(note.lengthSamples);
        }
    }
    appendInteger(snapshot.automationLanes.size());
    for (const auto& lane : snapshot.automationLanes)
    {
        appendInteger(lane.id);
        appendInteger(static_cast<std::uint8_t>(lane.target.kind));
        appendInteger(lane.target.trackId);
        appendInteger(lane.points.size());
        for (const auto& pt : lane.points)
        {
            appendInteger(pt.id);
            appendInteger(pt.samplePosition);
        }
    }
    return bytes;
}

void testIdentityAndEdits()
{
    auto project = Project::create("Untitled", 1);
    const auto empty = project.snapshot();
    require(empty.formatVersion == 2 && empty.id == 1 && empty.name == "Untitled",
            "new snapshot preserves version, supplied project ID and name");
    require(empty.tracks.empty(), "new project has no tracks");

    const auto first = project.addAudioTrack("Audio 1");
    const auto second = project.addAudioTrack("Audio 2");
    require(first != 0 && second != 0 && first != second,
            "tracks have distinct nonzero stable IDs");
    require(project.renameTrack(first, "Vocal"), "existing track can be renamed");
    require(project.removeTrack(second), "existing track can be removed");
    require(! project.removeTrack(second), "removed track cannot be removed twice");

    const auto snapshot = project.snapshot();
    require(snapshot.formatVersion == 2 && snapshot.id == 1 && snapshot.name == "Untitled",
            "edits preserve project identity, name and format version");
    require(snapshot.tracks == std::vector<Track> {{first, "Vocal", false, false}},
            "snapshot contains remaining renamed track with stable ID and default flags");
    require(project.addAudioTrack("Audio 3") > second, "removed IDs are never reused");
}

void testTrackOrderAndIdLifetime()
{
    auto project = Project::create("Arrangement", 42);
    const auto first = project.addAudioTrack("Same name");
    const auto middle = project.addAudioTrack("Middle");
    const auto last = project.addAudioTrack("Same name");
    require(first < middle && middle < last, "track IDs increase monotonically");
    require(project.renameTrack(last, "Last"), "last track renames by ID despite duplicate names");
    require(project.removeTrack(middle), "middle track removes by ID");
    const auto added = project.addAudioTrack("New");
    require(added > last, "add after middle removal preserves the ID high-water mark");
    require(project.snapshot().tracks == std::vector<Track> {
                {first, "Same name", false, false}, {last, "Last", false, false},
                {added, "New", false, false}},
            "removal preserves survivor order and add appends");
    require(project.removeTrack(first) && project.removeTrack(last) && project.removeTrack(added),
            "all tracks can be removed");
    require(project.snapshot().tracks.empty(), "removing every track leaves an empty project");
    require(project.addAudioTrack("Restart") > added,
            "emptying a project does not reset the track ID high-water mark");
}

void testRejectedEdits()
{
    auto project = Project::create("Validation", 7);
    const auto first = project.addAudioTrack("Keep");
    const auto before = serialize(project.snapshot());
    require(project.addAudioTrack("") == 0, "empty add returns invalid track ID zero");
    require(serialize(project.snapshot()) == before, "empty add leaves all snapshot values unchanged");
    require(! project.renameTrack(first, ""), "empty rename is rejected");
    require(serialize(project.snapshot()) == before, "empty rename leaves all snapshot values unchanged");
    for (const auto missing : std::array<TrackId, 3> {0, first + 1,
                                                    std::numeric_limits<TrackId>::max()})
    {
        require(! project.removeTrack(missing), "unknown track cannot be removed");
        require(! project.renameTrack(missing, "Missing"), "unknown track cannot be renamed");
        require(serialize(project.snapshot()) == before, "unknown IDs leave all snapshot values unchanged");
    }
    require(project.addAudioTrack("Next") == first + 1, "failed add does not consume an ID");
}

void testCommands()
{
    auto project = Project::create("Commands", 99);
    require(project.apply(AddAudioTrack {"Audio 1"}), "add command succeeds");
    const auto first = project.snapshot().tracks.front().id;
    require(project.apply(AddAudioTrack {"Audio 2"}), "second add command succeeds");
    const auto second = project.snapshot().tracks.back().id;
    require(project.apply(RenameTrack {first, "Vocal"}), "rename command succeeds");
    require(project.apply(RemoveTrack {second}), "remove command succeeds");
    require(project.snapshot().tracks == std::vector<Track> {{first, "Vocal", false, false}},
            "commands produce the same ordered track state as direct edits");

    const auto before = serialize(project.snapshot());
    const std::array<ProjectCommand, 7> rejected {
        AddAudioTrack {""}, RemoveTrack {second}, RemoveTrack {},
        RenameTrack {first, ""}, RenameTrack {second, "Removed"},
        RenameTrack {0, "Invalid"}, RenameTrack {std::numeric_limits<TrackId>::max(), "Missing"}
    };
    for (const auto& command : rejected)
    {
        require(! project.apply(command), "invalid command reports failure");
        require(serialize(project.snapshot()) == before,
                "failed command preserves byte-for-byte test serialization of every snapshot value");
    }
    require(project.apply(RenameTrack {first, "Vocal"}), "renaming to the current name succeeds");
    require(project.apply(AddAudioTrack {"Next"}), "valid add after rejected commands succeeds");
    require(project.snapshot().tracks.back().id == second + 1,
            "failed commands do not consume IDs or reuse removed IDs");
}

void testSnapshotIsolation()
{
    auto project = Project::create("Snapshot", 17);
    project.addAudioTrack("Original");
    const auto first = project.snapshot().tracks.front().id;
    auto copy = project.snapshot();
    copy.formatVersion = 999;
    copy.id = 100;
    copy.name = "Changed";
    copy.tracks.front() = {900, "Changed", true, true};
    copy.tracks.push_back({901, "Added", true, true});
    const auto original = project.snapshot();
    require(original.formatVersion == 2 && original.id == 17 && original.name == "Snapshot",
            "editing a snapshot does not change project metadata");
    require(original.tracks == std::vector<Track> {{first, "Original", false, false}},
            "editing a snapshot does not change project tracks or flags");
    require(project.renameTrack(first, "Later"), "project can change after taking a snapshot");
    require(project.removeTrack(first), "project can remove a snapshotted track");
    require(original.tracks == std::vector<Track> {{first, "Original", false, false}},
            "project edits leave earlier snapshot storage intact");
}

void testSnapshotReconstruction()
{
    ProjectSnapshot loaded {2, 42, "Loaded arrangement", {
        {17, "Same name", true, false}, {4000, "Lead", false, true},
        {3, "Same name", true, true}, {900, "Drums", false, false}}, {}, {}};
    const auto before = serialize(loaded);
    auto project = Project::fromSnapshot(loaded);
    require(project.has_value(), "valid sparse snapshot can reconstruct an editable project");
    require(serialize(project->snapshot()) == before,
            "reconstruction preserves every metadata field, track ID, name, flag and order");
    loaded.name = "Changed input";
    loaded.tracks.front() = {1, "Changed input", false, false};
    require(serialize(project->snapshot()) == before,
            "reconstructed project owns its metadata and track storage");
    require(project->addAudioTrack("") == 0, "empty add after reconstruction is rejected");
    require(serialize(project->snapshot()) == before,
            "failed add preserves the loaded snapshot");
    require(project->removeTrack(4000), "largest loaded ID can be removed");
    require(project->addAudioTrack("New") == 4001,
            "next ID exceeds the largest loaded ID even after its removal");
    require(project->apply(AddAudioTrack {"Command"}), "loaded project accepts add commands");
    require(project->snapshot().tracks == std::vector<Track> {
                {17, "Same name", true, false}, {3, "Same name", true, true},
                {900, "Drums", false, false}, {4001, "New", false, false},
                {4002, "Command", false, false}},
            "subsequent allocations append in order with default flags and monotonic IDs");

    auto empty = Project::fromSnapshot(ProjectSnapshot {2, 9, "Empty", {}, {}, {}});
    require(empty.has_value(), "empty snapshot can reconstruct a project");
    require(empty->addAudioTrack("First") == 1, "empty loaded project allocates from ID one");
}

void testSnapshotIdExhaustion()
{
    constexpr auto maximum = std::numeric_limits<TrackId>::max();
    auto nearLimit = Project::fromSnapshot(ProjectSnapshot {
        2, 1, "Near limit", {{maximum - 1, "Existing", false, false}}, {}, {}});
    require(nearLimit.has_value(), "snapshot with penultimate track ID is accepted");
    require(nearLimit->addAudioTrack("Last") == maximum,
            "largest representable ID can be allocated exactly once after loading");
    const auto full = serialize(nearLimit->snapshot());
    require(nearLimit->addAudioTrack("Overflow") == 0, "allocation rejects exhausted loaded IDs");
    require(! nearLimit->apply(AddAudioTrack {"Overflow command"}),
            "add command also rejects exhausted loaded IDs");
    require(serialize(nearLimit->snapshot()) == full, "exhausted adds leave snapshot unchanged");
    require(nearLimit->removeTrack(maximum) && nearLimit->removeTrack(maximum - 1),
            "tracks can still be removed after ID exhaustion");
    require(nearLimit->addAudioTrack("Reuse") == 0, "removal cannot reset exhausted IDs");

    auto exhausted = Project::fromSnapshot(ProjectSnapshot {
        2, 2, "Exhausted", {{maximum, "Maximum", true, true}, {4, "Lower", false, false}}, {}, {}});
    require(exhausted.has_value(), "snapshot containing maximum ID remains loadable");
    const auto before = serialize(exhausted->snapshot());
    require(exhausted->addAudioTrack("Overflow") == 0,
            "loaded maximum ID prevents allocation without unsigned wraparound reuse");
    require(serialize(exhausted->snapshot()) == before, "loaded exhaustion preserves state");
    require(exhausted->renameTrack(maximum, "Renamed"), "exhausted project remains editable");
}

void testInvalidSnapshots()
{
    const std::array<ProjectSnapshot, 8> invalid {{
        {0, 1, "Version zero", {}, {}, {}},
        {1, 1, "Unsupported version", {}, {}, {}},
        {std::numeric_limits<std::uint32_t>::max(), 1, "Unsupported version", {}, {}, {}},
        {2, 0, "Invalid project ID", {}, {}, {}},
        {2, 1, "", {}, {}, {}},
        {2, 1, "Invalid track ID", {{0, "Track", false, false}}, {}, {}},
        {2, 1, "Empty track name", {{5, "", false, false}}, {}, {}},
        {2, 1, "Duplicate track IDs", {
            {7, "First", false, false}, {9, "Middle", true, false}, {7, "Last", false, true}}, {}, {}}
    }};
    for (const auto& snapshot : invalid)
    {
        const auto before = serialize(snapshot);
        require(! Project::fromSnapshot(snapshot).has_value(), "invalid snapshot is rejected");
        require(serialize(snapshot) == before, "rejected reconstruction leaves input unchanged");
    }
}

void testAudioAssetsAndClips()
{
    const ProjectSnapshot valueSnapshot {
        2,
        1,
        "Playback",
        {},
        {{1, "fixtures/tone.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8}},
        {{1, 1, 0, 0, 8}}
    };
    require(valueSnapshot.assets.front().path == "fixtures/tone.wav", "asset path is owned");
    require(valueSnapshot.clips.front().lengthSamples == 8, "clip length is expressed in samples");

    auto project = Project::create("Playback", 1);
    const auto assetId = project.addAudioAsset({0, "tone.wav", AudioAssetPathKind::projectRelative,
                                                48000, 1, 8});
    require(assetId == 1, "first asset receives ID one");
    require(project.addAudioClip({0, assetId, 0, 0, 8}) == 1, "first clip receives ID one");
    require(project.addAudioClip({0, 999, 0, 0, 8}) == 0, "unknown asset is rejected");
    require(project.addAudioClip({0, assetId, 0, 9, 1}) == 0,
            "source offset beyond asset frames is rejected");
    require(project.addAudioClip({0, assetId, 0, 0, 0}) == 0, "empty clip range is rejected");
    require(project.addAudioClip({0, assetId, 0, 7, 2}) == 0,
            "clip range cannot exceed asset frames");

    const auto before = serialize(project.snapshot());
    for (const auto& asset : std::array<AudioAsset, 5> {{
             {0, "", AudioAssetPathKind::projectRelative, 48000, 1, 8},
             {0, "bad-kind.wav", static_cast<AudioAssetPathKind>(2), 48000, 1, 8},
             {0, "no-rate.wav", AudioAssetPathKind::absolute, 0, 1, 8},
             {0, "no-channels.wav", AudioAssetPathKind::absolute, 48000, 0, 8},
             {0, "no-frames.wav", AudioAssetPathKind::absolute, 48000, 1, 0},
         }})
    {
        require(project.addAudioAsset(asset) == 0, "invalid asset is rejected");
        require(serialize(project.snapshot()) == before, "invalid asset leaves every value unchanged");
    }
    require(project.addAudioAsset({assetId, "duplicate.wav", AudioAssetPathKind::absolute, 1, 1, 1}) == 0,
            "duplicate explicit asset ID is rejected");
    require(project.addAudioAsset({0, "next.wav", AudioAssetPathKind::absolute, 44100, 2, 1}) == 2,
            "rejected assets do not consume IDs");
    require(project.addAudioClip({0, assetId, 0, 0, 1}) == 2,
            "rejected clips do not consume IDs");
    require(project.addAudioClip({1, assetId, 0, 0, 1}) == 0,
            "duplicate explicit clip ID is rejected");

    auto copy = project.snapshot();
    copy.assets.front().path = "changed.wav";
    copy.assets.push_back({99, "extra.wav", AudioAssetPathKind::absolute, 1, 1, 1});
    copy.clips.front().lengthSamples = 1;
    copy.clips.push_back({99, assetId, 0, 0, 1});
    const auto original = project.snapshot();
    require(original.assets.front().path == "tone.wav" && original.assets.size() == 2,
            "snapshot asset paths and vectors do not alias project storage");
    require(original.clips.front().lengthSamples == 8 && original.clips.size() == 2,
            "snapshot clips do not alias project storage");

    auto loaded = Project::fromSnapshot({2, 9, "Sparse", {},
                                         {{8, "eight.wav", AudioAssetPathKind::absolute, 1, 1, 9}},
                                         {{11, 8, 0, 1, 8}}});
    require(loaded.has_value(), "valid media snapshot reconstructs");
    require(loaded->addAudioAsset({0, "later.wav", AudioAssetPathKind::absolute, 1, 1, 1}) == 9,
            "loaded asset allocation resumes above its independent maximum");
    require(loaded->addAudioClip({0, 8, 0, 0, 1}) == 12,
            "loaded clip allocation resumes above its independent maximum");

    const std::array<ProjectSnapshot, 8> invalid {{
        {2, 1, "Duplicate asset", {}, {{1, "a.wav", AudioAssetPathKind::absolute, 1, 1, 1},
                                         {1, "b.wav", AudioAssetPathKind::absolute, 1, 1, 1}}, {}},
        {2, 1, "Zero asset", {}, {{0, "a.wav", AudioAssetPathKind::absolute, 1, 1, 1}}, {}},
        {2, 1, "Zero sample rate", {}, {{1, "a.wav", AudioAssetPathKind::absolute, 0, 1, 1}}, {}},
        {2, 1, "Invalid kind", {}, {{1, "a.wav", static_cast<AudioAssetPathKind>(2), 1, 1, 1}}, {}},
        {2, 1, "Unknown asset", {}, {}, {{1, 7, 0, 0, 1}}},
        {2, 1, "Zero clip", {}, {{1, "a.wav", AudioAssetPathKind::absolute, 1, 1, 1}}, {{0, 1, 0, 0, 1}}},
        {2, 1, "Duplicate clip", {}, {{1, "a.wav", AudioAssetPathKind::absolute, 1, 1, 1}},
         {{1, 1, 0, 0, 1}, {1, 1, 1, 0, 1}}},
        {2, 1, "Invalid range", {}, {{1, "a.wav", AudioAssetPathKind::absolute, 1, 1, 1}}, {{1, 1, 0, 1, 1}}},
    }};
    for (const auto& snapshot : invalid)
        require(! Project::fromSnapshot(snapshot).has_value(), "invalid media snapshot is rejected");
}

void testTrackChannelStripCommands()
{
    auto project = Project::create("Mix", 1);
    const auto trackId = project.addAudioTrack("Lead");
    require(trackId == 1, "track created");

    require(project.apply(SetTrackMute {trackId, true}), "SetTrackMute succeeds");
    require(project.snapshot().tracks.front().muted, "track is muted");

    require(project.apply(SetTrackSolo {trackId, true}), "SetTrackSolo succeeds");
    require(project.snapshot().tracks.front().solo, "track is soloed");

    require(project.apply(SetTrackGainPan {trackId, 0.5f, -0.5f, true}),
            "SetTrackGainPan succeeds");
    const auto track = project.snapshot().tracks.front();
    require(track.gainLinear == 0.5f && track.pan == -0.5f && track.phaseInvert,
            "track stores gainLinear, pan, and phaseInvert");

    require(! project.apply(SetTrackGainPan {999, 0.5f, 0.0f, false}),
            "SetTrackGainPan rejects unknown track");
    require(! project.apply(SetTrackGainPan {trackId, -0.1f, 0.0f, false}),
            "SetTrackGainPan rejects negative linear gain");
    require(! project.apply(SetTrackGainPan {trackId, 1.0f, -1.5f, false}),
            "SetTrackGainPan rejects out-of-range pan");
}

void testProjectTempoAndLoopCommands()
{
    auto project = Project::create("Timebase", 1);
    require(project.snapshot().tempo == TempoSetting {120.0, 4, 4, false},
            "default project tempo is 120 BPM 4/4 metronome off");
    require(project.snapshot().loop == LoopSetting {false, 0, 0},
            "default project loop is disabled [0, 0)");

    require(project.apply(SetProjectTempo {140.0, 7, 8, true}),
            "SetProjectTempo accepts valid BPM and 7/8 time signature");
    require(project.snapshot().tempo == TempoSetting {140.0, 7, 8, true},
            "snapshot stores updated TempoSetting");

    require(! project.apply(SetProjectTempo {0.0, 4, 4, false}),
            "SetProjectTempo rejects non-positive BPM");
    require(! project.apply(SetProjectTempo {120.0, 0, 4, false}),
            "SetProjectTempo rejects zero numerator");
    require(! project.apply(SetProjectTempo {120.0, 4, 0, false}),
            "SetProjectTempo rejects zero denominator");

    require(project.apply(SetProjectLoop {true, 48000, 96000}),
            "SetProjectLoop accepts valid active loop range");
    require(project.snapshot().loop == LoopSetting {true, 48000, 96000},
            "snapshot stores updated LoopSetting");

    require(! project.apply(SetProjectLoop {true, 96000, 48000}),
            "SetProjectLoop rejects enabled loop where endSample <= startSample");
    require(project.apply(SetProjectLoop {false, 0, 0}),
            "SetProjectLoop accepts disabled loop with [0, 0)");
}

void testExtendedTimelineCommands()
{
    auto project = Project::create("Editing", 1);
    const auto t1 = project.addAudioTrack("Drums");
    const auto t2 = project.addAudioTrack("Bass");
    require(t1 == 1 && t2 == 2, "tracks added");

    // Track record arming
    require(project.apply(SetTrackRecordArm {t1, true}), "arm track 1");
    require(project.snapshot().tracks[0].recordArmed, "track 1 is armed");
    require(project.apply(SetTrackRecordArm {t1, false}), "disarm track 1");
    require(! project.snapshot().tracks[0].recordArmed, "track 1 is disarmed");
    require(! project.apply(SetTrackRecordArm {99, true}), "reject arm on unknown track");

    // Reorder tracks
    require(project.apply(ReorderTracks {{t2, t1}}), "reorder tracks 2, 1");
    require(project.snapshot().tracks[0].id == t2 && project.snapshot().tracks[1].id == t1,
            "tracks reordered");
    require(! project.apply(ReorderTracks {{t1}}), "reject reorder with mismatched count");
    require(! project.apply(ReorderTracks {{t1, 99}}), "reject reorder with invalid ID");
    require(! project.apply(ReorderTracks {{t1, t1}}), "reject reorder with duplicate ID");

    // Audio asset + clip commands
    const auto a1 = project.addAudioAsset({1, "loop.wav", AudioAssetPathKind::projectRelative, 48000, 2, 1000});
    require(a1 == 1, "asset added");
    const auto c1 = project.addAudioClip({1, a1, 0, 0, 800, t1, 1.0f, 0, 0});
    require(c1 == 1, "clip added");

    // Move clip
    require(project.apply(MoveClip {c1, t2, 500}), "move clip to track 2 at sample 500");
    require(project.snapshot().clips[0].trackId == t2 && project.snapshot().clips[0].startSample == 500,
            "clip moved to track 2 sample 500");
    require(! project.apply(MoveClip {99, t1, 0}), "reject move unknown clip");
    require(! project.apply(MoveClip {c1, 99, 0}), "reject move to unknown track");

    // Trim clip
    require(project.apply(TrimClip {c1, 100, 600}), "trim clip to offset 100 length 600");
    require(project.snapshot().clips[0].sourceOffset == 100 && project.snapshot().clips[0].lengthSamples == 600,
            "clip trimmed");
    require(! project.apply(TrimClip {c1, 100, 0}), "reject trim zero length");
    require(! project.apply(TrimClip {c1, 900, 200}), "reject trim past asset bounds");

    // SetClipGainFade
    require(project.apply(SetClipGainFade {c1, 0.75f, 50, 100}), "set clip gain and fades");
    require(project.snapshot().clips[0].gainLinear == 0.75f &&
            project.snapshot().clips[0].fadeInSamples == 50 &&
            project.snapshot().clips[0].fadeOutSamples == 100, "clip gain and fades updated");
    require(! project.apply(SetClipGainFade {c1, -0.5f, 0, 0}), "reject negative clip gain");

    // Duplicate clip
    require(project.apply(DuplicateClip {c1, 2000}), "duplicate clip at sample 2000");
    require(project.snapshot().clips.size() == 2, "second clip created");
    const auto c2 = project.snapshot().clips[1];
    require(c2.id == 2 && c2.startSample == 2000 && c2.sourceOffset == 100 && c2.lengthSamples == 600,
            "duplicated clip has new ID and updated startSample while preserving source range");

    // Split clip
    // Clip 1 is at startSample 500, length 600 (covers 500..1100). Split at 800.
    require(project.apply(SplitClip {c1, 800}), "split clip at sample 800");
    require(project.snapshot().clips.size() == 3, "three clips after split");
    const auto part1 = project.snapshot().clips[0];
    const auto part2 = project.snapshot().clips[2];
    require(part1.id == c1 && part1.startSample == 500 && part1.lengthSamples == 300,
            "part 1 retains original ID with length 300 (500..800)");
    require(part2.id == 3 && part2.startSample == 800 && part2.lengthSamples == 300 && part2.sourceOffset == 400,
            "part 2 gets new ID 3 with length 300 (800..1100) and offset advanced to 400");
    require(! project.apply(SplitClip {c1, 500}), "reject split at startSample");
    require(! project.apply(SplitClip {c1, 400}), "reject split before startSample");
    require(! project.apply(SplitClip {c1, 800}), "reject split at end of part 1");

    // Remove clip
    require(project.apply(RemoveClip {part2.id}), "remove split part 2");
    require(project.snapshot().clips.size() == 2, "clip removed");
    require(! project.apply(RemoveClip {99}), "reject remove unknown clip");
}

void testMidiTracksClipsAndNotes()
{
    auto project = Project::create("Midi Project", 1);
    const auto trackId = project.addInstrumentTrack("Synth Lead");
    require(trackId != 0, "addInstrumentTrack succeeded");
    require(project.snapshot().tracks[0].kind == TrackKind::instrument, "track kind is instrument");

    // Add MIDI clip
    const auto clipId = project.addMidiClip(MidiClip {
        .id = 0,
        .trackId = trackId,
        .startSample = 0,
        .lengthSamples = 96000,
        .notes = {}
    });
    require(clipId != 0, "addMidiClip succeeded with assigned ID");
    require(project.snapshot().midiClips.size() == 1, "one midi clip in snapshot");

    // Add MIDI note
    const auto note1 = project.addMidiNote(clipId, MidiNote {
        .id = 0,
        .pitch = 60, // C4
        .velocity = 100,
        .startSample = 0,
        .lengthSamples = 24000
    });
    require(note1 != 0, "addMidiNote succeeded");
    const auto note2 = project.addMidiNote(clipId, MidiNote {
        .id = 0,
        .pitch = 64, // E4
        .velocity = 90,
        .startSample = 24000,
        .lengthSamples = 24000
    });
    require(note2 != 0 && note2 != note1, "second note assigned distinct ID");
    require(project.snapshot().midiClips[0].notes.size() == 2, "two notes in clip");

    // Move note
    require(project.apply(MoveMidiNote {clipId, note1, 12000, 62}), "move note pitch and startSample");
    require(project.snapshot().midiClips[0].notes[0].pitch == 62, "pitch updated to D4");
    require(project.snapshot().midiClips[0].notes[0].startSample == 12000, "startSample updated");

    // Resize note
    require(project.apply(ResizeMidiNote {clipId, note1, 36000}), "resize note length");
    require(project.snapshot().midiClips[0].notes[0].lengthSamples == 36000, "length updated");

    // Reconstruct from snapshot
    const auto snap = project.snapshot();
    const auto restored = Project::fromSnapshot(snap);
    require(restored.has_value(), "fromSnapshot succeeded with MIDI data");
    require(restored->snapshot() == snap, "restored project exactly equals original snapshot");

    // Remove note
    require(project.apply(RemoveMidiNote {clipId, note2}), "remove note");
    require(project.snapshot().midiClips[0].notes.size() == 1, "one note remaining");

    // Remove clip
    require(project.apply(RemoveMidiClip {clipId}), "remove midi clip");
    require(project.snapshot().midiClips.empty(), "all midi clips removed");
}

void testAutomationLanesAndPoints()
{
    auto project = Project::create("Automation Project", 1);
    const auto trackId = project.addAudioTrack("Vocal");
    require(trackId != 0, "addAudioTrack succeeded");

    // Add automation lane
    const auto laneId = project.addAutomationLane(AutomationLane {
        .id = 0,
        .target = AutomationTarget {
            .kind = AutomationTargetKind::trackVolume,
            .trackId = trackId,
            .slotIndex = 0,
            .parameterIndex = 0
        },
        .points = {}
    });
    require(laneId != 0, "addAutomationLane succeeded");
    require(project.snapshot().automationLanes.size() == 1, "one lane in snapshot");

    // Add points
    const auto pt1 = project.addAutomationPoint(laneId, AutomationPoint {
        .id = 0,
        .samplePosition = 0,
        .value = 0.0f,
        .curveTension = 0.0f
    });
    const auto pt2 = project.addAutomationPoint(laneId, AutomationPoint {
        .id = 0,
        .samplePosition = 48000,
        .value = 1.0f,
        .curveTension = 0.5f
    });
    require(pt1 != 0 && pt2 != 0 && pt1 != pt2, "two points added with distinct IDs");
    require(project.snapshot().automationLanes[0].points.size() == 2, "two points in lane");

    // Move point
    require(project.apply(MoveAutomationPoint {laneId, pt2, 96000, 0.8f, -0.2f}), "move point");
    const auto postMoveSnap = project.snapshot();
    const auto& updatedPt2 = postMoveSnap.automationLanes[0].points[1];
    require(updatedPt2.samplePosition == 96000, "samplePosition moved to 96000");
    require(std::abs(updatedPt2.value - 0.8f) < 1e-4f, "value updated to 0.8");
    require(std::abs(updatedPt2.curveTension - (-0.2f)) < 1e-4f, "tension updated to -0.2");

    // Snapshot restoration
    const auto snap = project.snapshot();
    const auto restored = Project::fromSnapshot(snap);
    require(restored.has_value(), "fromSnapshot restored automation lane");
    require(restored->snapshot() == snap, "restored snapshot equals original");

    // Remove point
    require(project.apply(RemoveAutomationPoint {laneId, pt1}), "remove point");
    require(project.snapshot().automationLanes[0].points.size() == 1, "one point remains");

    // Remove lane
    require(project.apply(RemoveAutomationLane {laneId}), "remove lane");
    require(project.snapshot().automationLanes.empty(), "automation lane removed");
}

void testAuxSendsSubmixBusesAndSwipeComping()
{
    auto project = Project::create("Mix & Comp Project", 1);
    const auto vocalTrackId = project.addAudioTrack("Lead Vocal");
    const auto reverbTrackId = project.addAuxReturnTrack("Plate Reverb");
    const auto drumBusId = project.addSubmixGroupTrack("Drum Bus");

    require(vocalTrackId != 0 && reverbTrackId != 0 && drumBusId != 0, "tracks created successfully");
    const auto snap1 = project.snapshot();
    require(snap1.tracks[1].kind == TrackKind::auxReturn, "aux return track kind is auxReturn");
    require(snap1.tracks[2].kind == TrackKind::submixGroup, "submix bus track kind is submixGroup");

    // Aux Send
    require(project.apply(AddTrackSend {vocalTrackId, TrackSend {.targetTrackId = reverbTrackId, .levelLinear = 0.5f, .preFader = false}}),
            "add send from Lead Vocal to Plate Reverb");
    require(project.snapshot().tracks[0].sends.size() == 1, "one send on vocal track");
    require(project.snapshot().tracks[0].sends[0].levelLinear == 0.5f, "send level is 0.5");

    require(project.apply(SetTrackSendLevel {vocalTrackId, reverbTrackId, 0.75f}), "update send level to 0.75");
    require(project.snapshot().tracks[0].sends[0].levelLinear == 0.75f, "send level updated");

    // Submix Bus routing
    require(project.apply(SetTrackOutputBus {vocalTrackId, drumBusId}), "route vocal track into submix bus");
    require(project.snapshot().tracks[0].outputBusTrackId == drumBusId, "outputBusTrackId matches submix bus");

    // Vocal Comping & Take Lanes
    const auto assetId = project.addAudioAsset(AudioAsset {
        .id = 0,
        .path = "audio/takes.wav",
        .pathKind = AudioAssetPathKind::projectRelative,
        .sampleRate = 48000,
        .channelCount = 1,
        .frameCount = 960000
    });
    require(assetId != 0, "take asset added");

    // Add Take 1 and Take 2
    require(project.apply(AddTakeToTrack {
        vocalTrackId,
        Take {.id = 1, .assetId = assetId, .sourceOffset = 0, .lengthSamples = 480000, .name = "Take 1"}
    }), "add Take 1 to vocal track");

    require(project.apply(AddTakeToTrack {
        vocalTrackId,
        Take {.id = 2, .assetId = assetId, .sourceOffset = 480000, .lengthSamples = 480000, .name = "Take 2"}
    }), "add Take 2 to vocal track");

    require(project.snapshot().tracks[0].takes.size() == 2, "two takes on vocal track");

    // Toggle take lanes expanded
    require(! project.snapshot().tracks[0].takeLanesExpanded, "take lanes collapsed by default");
    require(project.apply(ToggleTakeLanesExpanded {vocalTrackId}), "toggle take lanes expanded");
    require(project.snapshot().tracks[0].takeLanesExpanded, "take lanes are now expanded");

    // Initial base clip on track (samples 0 to 200,000)
    const auto baseClipId = project.addAudioClip(AudioClip {
        .id = 0,
        .assetId = assetId,
        .startSample = 0,
        .sourceOffset = 0,
        .lengthSamples = 200000,
        .trackId = vocalTrackId,
        .gainLinear = 1.0f,
        .fadeInSamples = 0,
        .fadeOutSamples = 0
    });
    require(baseClipId != 0, "base clip placed");

    // Swipe comping: promote Take 2 in region [50,000, 100,000)
    // This should split/trim the base clip into [0, 50,000) and [100,000, 200,000) with crossfades!
    require(project.apply(PromoteTakeRegion {
        vocalTrackId,
        2,      // takeId 2
        50000,  // startSample
        50000   // lengthSamples (50,000 to 100,000)
    }), "promote take 2 region [50k, 100k)");

    const auto compedClips = project.snapshot().clips;
    require(compedClips.size() == 3, "comping creates 3 clips (left part, promoted take 2, right part)");

    // Verify promoted clip has automatic crossfade handles
    const auto promoted = std::find_if(compedClips.begin(), compedClips.end(),
                                       [](const auto& c) { return c.startSample == 50000; });
    require(promoted != compedClips.end(), "promoted clip found at sample 50,000");
    require(promoted->lengthSamples == 50000, "promoted clip length is 50,000");
    require(promoted->fadeInSamples == 256, "automatic equal-power fadeInSamples is 256");
    require(promoted->fadeOutSamples == 256, "automatic equal-power fadeOutSamples is 256");

    // Remove send
    require(project.apply(RemoveTrackSend {vocalTrackId, reverbTrackId}), "remove track send");
    require(project.snapshot().tracks[0].sends.empty(), "send removed");
}

void testTakeLaneManagerAndEqualPowerCrossfades()
{
    TakeCompManager compManager(101);
    require(compManager.trackId() == 101, "CompManager trackId matches");

    // 1. Add Multiple Take Lanes
    AudioClip takeClip1;
    takeClip1.id = 1;
    takeClip1.assetId = 10;
    takeClip1.startSample = 0;
    takeClip1.lengthSamples = 96000; // 2 seconds

    AudioClip takeClip2;
    takeClip2.id = 2;
    takeClip2.assetId = 11;
    takeClip2.startSample = 0;
    takeClip2.lengthSamples = 96000;

    const auto t1 = compManager.addTake(takeClip1, "Lead Vocal Take 1");
    const auto t2 = compManager.addTake(takeClip2, "Lead Vocal Take 2");

    require(compManager.takeLanes().size() == 2, "2 take lanes registered");
    require(compManager.findTake(t1) != nullptr, "Take 1 found");
    require(compManager.findTake(t2) != nullptr, "Take 2 found");

    // 2. Swipe-Comping Selection: Splice Hero Take from Take 2 in middle [24000, 72000)
    compManager.selectCompSlice(t2, 24000, 72000, 256);

    const auto slices = compManager.compSlices();
    require(slices.size() == 3, "Swipe-comping splices into 3 slices: [0, 24k) Take 1, [24k, 72k) Take 2, [72k, 96k) Take 1");
    require(slices[0].takeId == t1 && slices[0].startSample == 0 && slices[0].endSample == 24000, "Slice 0 is Take 1 head");
    require(slices[1].takeId == t2 && slices[1].startSample == 24000 && slices[1].endSample == 72000, "Slice 1 is Take 2 mid hero take");
    require(slices[2].takeId == t1 && slices[2].startSample == 72000 && slices[2].endSample == 96000, "Slice 2 is Take 1 tail");

    // 3. Take Audition Solo
    compManager.setTakeAuditionSolo(t2, true);
    require(compManager.soloedTakeId() == t2, "Take 2 soloed for auditioning");
    require(compManager.findTake(t2)->isAuditionSolo, "Take 2 lane isAuditionSolo is true");

    compManager.setTakeAuditionSolo(t2, false);
    require(compManager.soloedTakeId() == 0, "Take 2 solo cleared");

    // 4. Equal-Power Crossfade Calculation Verification
    constexpr std::size_t fadeLen = 128;
    std::vector<float> outgoing(fadeLen, 1.0f);
    std::vector<float> incoming(fadeLen, 1.0f);

    TakeCompManager::applyEqualPowerCrossfade(outgoing.data(), incoming.data(), fadeLen);

    // Sum of squared amplitudes should equal 1.0 across entire fade
    for (std::size_t i = 0; i < fadeLen; ++i) {
        const float powerSum = (outgoing[i] * outgoing[i]) + (incoming[i] * incoming[i]);
        require(std::abs(powerSum - 1.0f) < 1e-4f, "Equal-power crossfade preserves constant acoustic power");
    }

    // 5. Flatten & Merge Comp into Consolidated AudioClip
    const AudioClip merged = compManager.flattenAndMergeComp(999, 50, "Comp Lead Vocal Master");
    require(merged.id == 999, "Consolidated clip ID is 999");
    require(merged.assetId == 50, "Consolidated asset ID is 50");
    require(merged.startSample == 0, "Consolidated startSample is 0");
    require(merged.lengthSamples == 96000, "Consolidated length is 96000");
}

void testPitchBlobEditorAndGrooveQuantizer()
{
    // 1. Zero-Crossing Auto-Snapping in Comping
    std::vector<float> audioBuffer(1024, 0.0f);
    for (std::size_t i = 0; i < 1024; ++i) {
        audioBuffer[i] = std::sin(static_cast<float>(i) * 0.1f);
    }
    const auto snapped = TakeCompManager::findNearestZeroCrossing(audioBuffer.data(), 1024, 30);
    require(std::abs(audioBuffer[snapped]) < 0.15f, "Nearest zero crossing found");

    // 2. Pitch Blob Editor: Note segmentation & scale quantization
    PitchBlobEditor blobEditor;
    std::vector<float> vocalSynth(48000, 0.0f); // 1 second vocal simulation
    // Syllable 1: A4 (440 Hz) for 0.4s
    for (std::size_t i = 0; i < 19200; ++i) {
        vocalSynth[i] = 0.6f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * (static_cast<float>(i) / 48000.0f));
    }
    // Syllable 2: C5 (523.25 Hz) for 0.4s (starting at 0.5s)
    for (std::size_t i = 24000; i < 43200; ++i) {
        vocalSynth[i] = 0.6f * std::sin(2.0f * std::numbers::pi_v<float> * 523.25f * (static_cast<float>(i) / 48000.0f));
    }

    blobEditor.analyzeAudio(vocalSynth.data(), vocalSynth.size(), 48000.0);
    const auto& blobs = blobEditor.blobs();
    require(!blobs.empty(), "Vocal syllables segmented into pitch blobs");

    // Scale Quantization (e.g. A Minor: A, B, C, D, E, F, G -> classes 9, 11, 0, 2, 4, 5, 7)
    blobEditor.quantizeToScale({9, 11, 0, 2, 4, 5, 7}, 1.0f);
    for (const auto& blob : blobEditor.blobs()) {
        const int noteClass = (static_cast<int>(std::round(blob.targetMidiPitch)) % 12 + 12) % 12;
        require(noteClass == 9 || noteClass == 0 || noteClass == 2 || noteClass == 4 || noteClass == 5 || noteClass == 7 || noteClass == 11,
            "Pitch blob quantized to A Minor scale");
    }

    // Blob parameter edits
    if (!blobs.empty()) {
        const auto firstId = blobs.front().blobId;
        require(blobEditor.setBlobTuning(firstId, 69.0f, -5.0f), "Tuning updated");
        require(blobEditor.setBlobVibrato(firstId, 0.5f), "Vibrato updated");
        require(blobEditor.setBlobFormant(firstId, 2.0f), "Formant shifted");
    }

    // 3. Groove Quantizer
    const auto mpcGroove = GrooveQuantizer::mpc60Swing(0.58f);
    require(mpcGroove.stepTimingOffsets.size() == 16, "MPC 16-step groove template initialized");
    require(mpcGroove.swingAmount == 0.58f, "Swing amount is 58%");

    std::vector<double> noteBeats = { 0.0, 0.25, 0.50, 0.75, 1.0, 1.25, 1.50, 1.75 };
    GrooveQuantizer::applyGrooveToBeatTimestamps(noteBeats, mpcGroove, 1.0f);
    require(noteBeats[1] > 0.25, "Offbeat 16th note shifted forward by MPC swing");

    const auto audioGroove = GrooveQuantizer::extractGrooveFromAudio(vocalSynth.data(), vocalSynth.size(), 48000.0, 120.0);
    require(audioGroove.stepTimingOffsets.size() == 16, "Extracted groove offsets from audio transient grid");
}
}

int main()
{
    testIdentityAndEdits();
    testTrackOrderAndIdLifetime();
    testRejectedEdits();
    testCommands();
    testSnapshotIsolation();
    testSnapshotReconstruction();
    testSnapshotIdExhaustion();
    testInvalidSnapshots();
    testAudioAssetsAndClips();
    testTrackChannelStripCommands();
    testProjectTempoAndLoopCommands();
    testExtendedTimelineCommands();
    testMidiTracksClipsAndNotes();
    testAutomationLanesAndPoints();
    testAuxSendsSubmixBusesAndSwipeComping();
    testTakeLaneManagerAndEqualPowerCrossfades();
    testPitchBlobEditorAndGrooveQuantizer();
    std::puts("PASS: 17 project behavior tests");
}
