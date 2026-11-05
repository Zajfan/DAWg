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
    return bytes;
}

void testIdentityAndEdits()
{
    auto project = Project::create("Untitled", 1);
    const auto empty = project.snapshot();
    require(empty.formatVersion == 1 && empty.id == 1 && empty.name == "Untitled",
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
    require(snapshot.formatVersion == 1 && snapshot.id == 1 && snapshot.name == "Untitled",
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
    require(original.formatVersion == 1 && original.id == 17 && original.name == "Snapshot",
            "editing a snapshot does not change project metadata");
    require(original.tracks == std::vector<Track> {{first, "Original", false, false}},
            "editing a snapshot does not change project tracks or flags");
    require(project.renameTrack(first, "Later"), "project can change after taking a snapshot");
    require(project.removeTrack(first), "project can remove a snapshotted track");
    require(original.tracks == std::vector<Track> {{first, "Original", false, false}},
            "project edits leave earlier snapshot storage intact");
}
}

int main()
{
    testIdentityAndEdits();
    testTrackOrderAndIdLifetime();
    testRejectedEdits();
    testCommands();
    testSnapshotIsolation();
    std::puts("PASS: 5 project behavior tests");
}
