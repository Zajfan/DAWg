#include <dawg/project/Project.h>

#include <array>
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
    std::puts("PASS: 12 project behavior tests");
}
