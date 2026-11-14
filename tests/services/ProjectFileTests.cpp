#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace {
using namespace dawg::project;
using namespace dawg::persistence;
namespace fs = std::filesystem;
using Code = ProjectFileError::Code;

void require(const bool condition, const char* message)
{
    if (! condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

struct TestDirectory
{
    fs::path root;
    TestDirectory()
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt)
        {
            root = fs::temp_directory_path() / ("dawg-project-file-tests-" +
                std::to_string(suffix) + "-" + std::to_string(attempt));
            if (fs::create_directory(root))
                return;
        }
        require(false, "create isolated test directory");
    }
    ~TestDirectory()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
};

void writeDocument(const fs::path& path, const std::string_view document)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(document.data(), static_cast<std::streamsize>(document.size()));
    output.close();
    require(! output.fail(), "write fixture document");
}

std::string readDocument(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "read fixture document");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename T>
void requireError(const std::expected<T, ProjectFileError>& result, const Code code)
{
    require(! result.has_value(), "operation rejects invalid input");
    require(result.error().code == code, "failure has the expected typed code");
    require(! result.error().message.empty(), "failure has a diagnostic message");
}

std::size_t entryCount(const fs::path& root)
{
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(root),
                                                fs::directory_iterator()));
}

void testRoundTrip(const fs::path& root)
{
    auto project = Project::create("Round Trip \"mix\"\n音楽", 42);
    project.addAudioTrack("Audio 1");
    project.addAudioTrack("Audio 2");
    require(project.addAudioAsset(
                {4, "audio/tone.wav", AudioAssetPathKind::projectRelative, 48000, 2, 960}) == 4,
            "register relative asset");
    require(project.addAudioAsset(
                {7, "/tmp/lead.wav", AudioAssetPathKind::absolute, 44100, 1, 480}) == 7,
            "register absolute asset");
    require(project.addAudioClip({9, 4, 48000, 120, 840}) == 9, "register first clip");
    require(project.addAudioClip({12, 7, 0, 0, 480}) == 12, "register second clip");
    auto original = project.snapshot();
    original.tracks.front().muted = true;
    original.tracks.back().solo = true;
    const auto path = root / "round-trip.dawg";
    require(saveProject(original, path).has_value(), "v2 project saves");
    const auto loaded = loadProject(path);
    require(loaded.has_value() && loaded->formatVersion == 2, "v2 project loads");
    require(loaded->id == original.id && loaded->name == original.name,
            "project ID and escaped Unicode name survive round trip");
    require(loaded->tracks == original.tracks, "ordered tracks and flags survive round trip");
    require(loaded->assets == original.assets && loaded->clips == original.clips,
            "asset metadata and clip timing survive round trip");
    const auto document = readDocument(path);
    for (const auto key : {"\"formatVersion\"", "\"projectId\"", "\"name\"", "\"tracks\"",
                           "\"assets\"", "\"clips\"", "\"id\"", "\"muted\"", "\"solo\"",
                           "\"path\"", "\"pathKind\"", "\"projectRelative\"", "\"absolute\"",
                           "\"sampleRate\"", "\"channelCount\"", "\"frameCount\"",
                           "\"assetId\"", "\"startSample\"", "\"sourceOffset\"", "\"lengthSamples\""})
        require(document.find(key) != std::string::npos, "document has the public v2 schema fields");

    const ProjectSnapshot empty {2, std::numeric_limits<ProjectId>::max(), "Empty", {}, {}, {}};
    require(saveProject(empty, path).has_value(), "existing file can be replaced with empty project");
    const auto reloaded = loadProject(path);
    require(reloaded.has_value() && reloaded->formatVersion == 2 && reloaded->tracks.empty()
                && reloaded->assets.empty() && reloaded->clips.empty() && reloaded->id == empty.id,
            "empty collections and maximum project ID survive replacement");
}

void testExternalDocument(const fs::path& root)
{
    const auto path = root / "external.dawg";
    writeDocument(path, R"({"formatVersion":1,"projectId":18446744073709551615,"name":"Loaded","tracks":[{"id":7,"name":"First","muted":true,"solo":true},{"id":4000,"name":"Lead","muted":false,"solo":true},{"id":3,"name":"Last","muted":false,"solo":false}]})");
    const auto loaded = loadProject(path);
    require(loaded.has_value(), "independent version-one fixture loads");
    require(loaded->formatVersion == 2 && loaded->id == std::numeric_limits<ProjectId>::max()
                && loaded->name == "Loaded" && loaded->assets.empty() && loaded->clips.empty(),
            "version-one fixture normalizes to version two with empty media arrays");
    require(loaded->tracks == std::vector<Track> {
                {7, "First", true, true}, {4000, "Lead", false, true}, {3, "Last", false, false}},
            "external tracks preserve IDs, flags and document order");
    auto editable = Project::fromSnapshot(*loaded);
    require(editable.has_value() && editable->addAudioTrack("New") == 4001,
            "loaded snapshot reconstructs with the largest-ID high-water mark");
    auto maximum = *loaded;
    maximum.tracks.front().id = std::numeric_limits<TrackId>::max();
    require(saveProject(maximum, path).has_value(),
            "normalized v1 snapshot with maximum track ID saves back as v2 without narrowing");
    const auto reloaded = loadProject(path);
    require(reloaded.has_value() && reloaded->formatVersion == 2
                && reloaded->tracks == maximum.tracks,
            "maximum track ID reloads without narrowing");
}

void testMalformedDocuments(const fs::path& root)
{
    const auto path = root / "malformed.dawg";
    // Removing validation or coercing JSON values would accept one of these fixtures.
    const std::array documents {
        "", "{", "[]", "null", "42",
        R"({"projectId":42,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":42,"tracks":[]})",
        R"({"formatVersion":1,"projectId":42,"name":"P"})",
        R"({"formatVersion":"1","projectId":42,"name":"P","tracks":[]})",
        R"({"formatVersion":1.0,"projectId":42,"name":"P","tracks":[]})",
        R"({"formatVersion":true,"projectId":42,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":0,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":-1,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":1.5,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":"42","name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":18446744073709551616,"name":"P","tracks":[]})",
        R"({"formatVersion":1,"projectId":42,"name":"","tracks":[]})",
        R"({"formatVersion":1,"projectId":42,"name":null,"tracks":[]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":{}})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[null]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"name":"T","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"T","solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"T","muted":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":0,"name":"T","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":-1,"name":"T","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1.5,"name":"T","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":18446744073709551616,"name":"T","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"","muted":false,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"T","muted":0,"solo":false}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"T","muted":false,"solo":"false"}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[{"id":1,"name":"T","muted":false,"solo":false},{"id":1,"name":"U","muted":true,"solo":true}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":{},"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[],"clips":{}})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[null],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[],"clips":[null]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":0,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"unknown","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":0,"sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":0,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":4294967296,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":0,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":65536,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":0}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8},{"id":1,"path":"b.wav","pathKind":"absolute","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":0,"assetId":1,"startSample":0,"sourceOffset":0,"lengthSamples":8}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":1,"assetId":99,"startSample":0,"sourceOffset":0,"lengthSamples":8}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":1,"assetId":1,"startSample":0,"sourceOffset":9,"lengthSamples":1}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":1,"assetId":1,"startSample":0,"sourceOffset":0,"lengthSamples":0}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":1,"assetId":1,"startSample":0,"sourceOffset":4,"lengthSamples":5}]})",
        R"({"formatVersion":2,"projectId":42,"name":"P","tracks":[],"assets":[{"id":1,"path":"a.wav","pathKind":"projectRelative","sampleRate":48000,"channelCount":1,"frameCount":8}],"clips":[{"id":1,"assetId":1,"startSample":0,"sourceOffset":0,"lengthSamples":4},{"id":1,"assetId":1,"startSample":4,"sourceOffset":0,"lengthSamples":4}]})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[]} {})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[]} trailing)"
    };
    for (const auto document : documents)
    {
        writeDocument(path, document);
        requireError(loadProject(path), Code::invalidFormat);
    }
    for (const auto version : {"0", "4", "999", "-1", "4294967297", "18446744073709551615"})
    {
        writeDocument(path, std::string {R"({"formatVersion":)"} + version +
                            R"(,"projectId":42,"name":"P","tracks":[]})");
        requireError(loadProject(path), Code::unsupportedVersion);
    }
}

void testVersionThreeFivePillarRoundTrip(const fs::path& root)
{
    auto project = Project::create("Five Pillar Session", 77);
    const auto track1 = project.addAudioTrack("Drums");
    const auto track2 = project.addAudioTrack("Bass");
    require(project.setTrackGainPan(track1, 0.75f, -0.25f, true), "set Drums channel strip");
    require(project.setTrackMute(track2, true), "mute Bass");
    require(project.setTrackSolo(track1, true), "solo Drums");
    require(project.setTrackGainPan(track2, 1.25f, 0.5f, false), "set Bass channel strip");

    require(project.addAudioAsset({1, "audio/drums.wav", AudioAssetPathKind::projectRelative, 48000, 2, 96000}) == 1,
            "add drums asset");
    require(project.addAudioClip({
                .id = 1,
                .assetId = 1,
                .startSample = 1200,
                .sourceOffset = 400,
                .lengthSamples = 48000,
                .trackId = track1,
                .gainLinear = 0.8f,
                .fadeInSamples = 128,
                .fadeOutSamples = 256
            }) == 1,
            "add clip with fades and track assignment");
    require(project.apply(SetProjectTempo {128.5, 6, 8, true}), "set project tempo 128.5 BPM 6/8");
    require(project.apply(SetProjectLoop {true, 4800, 52800}), "set project loop region");

    auto v3Snapshot = project.snapshot();
    v3Snapshot.formatVersion = 3;

    const auto path = root / "session-v3.dawg";
    require(saveProject(v3Snapshot, path).has_value(), "save v3 project");

    const auto loaded = loadProject(path);
    require(loaded.has_value() && loaded->formatVersion == 3, "load v3 project");
    require(loaded->tracks == v3Snapshot.tracks,
            "v3 track channel strip parameters (gainLinear, pan, phaseInvert, muted, solo) survive round trip");
    require(loaded->clips == v3Snapshot.clips,
            "v3 clip parameters (trackId, gainLinear, fadeInSamples, fadeOutSamples) survive round trip");
    require(loaded->tempo == v3Snapshot.tempo,
            "v3 TempoSetting (bpm, timeSignature, metronomeEnabled) survives round trip");
    require(loaded->loop == v3Snapshot.loop,
            "v3 LoopSetting (enabled, startSample, endSample) survives round trip");
}

void testRejectedSaves(const fs::path& root)
{
    const auto path = root / "preserved.dawg";
    const ProjectSnapshot valid {2, 42, "Original", {{1, "Track", false, true}}, {}, {}};
    require(saveProject(valid, path).has_value(), "create original destination");
    const auto before = readDocument(path);
    const auto entries = entryCount(root);
    const std::array<ProjectSnapshot, 11> invalid {{
        {2, 0, "P", {}, {}, {}}, {2, 1, "", {}, {}, {}},
        {2, 1, "P", {{0, "T", false, false}}, {}, {}},
        {2, 1, "P", {{1, "", false, false}}, {}, {}},
        {2, 1, "P", {{7, "T", false, false}, {7, "U", true, false}}, {}, {}},
        {2, 1, std::string(1, static_cast<char>(0xff)), {}, {}, {}},
        {2, 1, "P", {}, {{1, "a.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8},
                         {1, "b.wav", AudioAssetPathKind::absolute, 48000, 1, 8}}, {}},
        {2, 1, "P", {}, {{1, "a.wav", static_cast<AudioAssetPathKind>(9), 48000, 1, 8}}, {}},
        {2, 1, "P", {}, {}, {{1, 99, 0, 0, 8}}},
        {2, 1, "P", {}, {{1, "a.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8}},
         {{1, 1, 0, 4, 5}}},
        {2, 1, "P", {}, {{1, "a.wav", AudioAssetPathKind::projectRelative, 48000, 1, 8}},
         {{1, 1, 0, 0, 4}, {1, 1, 4, 0, 4}}}
    }};
    for (const auto& snapshot : invalid)
    {
        requireError(saveProject(snapshot, path), Code::invalidFormat);
        require(readDocument(path) == before, "invalid save preserves original bytes");
        require(entryCount(root) == entries, "invalid save leaves no temporary files");
    }
    for (const auto version : {0u, 1u, 999u})
    {
        auto unsupported = valid;
        unsupported.formatVersion = version;
        requireError(saveProject(unsupported, path), Code::unsupportedVersion);
        require(readDocument(path) == before, "unsupported save preserves original bytes");
    }
}

void testPathFailures(const fs::path& root)
{
    const ProjectSnapshot snapshot {2, 42, "Original", {}, {}, {}};
    for (const auto& path : {fs::path {}, root / ".", root / "..",
                             root / std::string("nul\0suffix", 10)})
    {
        requireError(saveProject(snapshot, path), Code::invalidPath);
        requireError(loadProject(path), Code::invalidPath);
    }
    requireError(loadProject(root / "absent.dawg"), Code::ioFailure);
    requireError(loadProject(root), Code::ioFailure);
    requireError(saveProject(snapshot, root / "absent-parent" / "new.dawg"), Code::ioFailure);
    require(! fs::exists(root / "absent-parent"), "save does not create missing parents");

    const auto original = root / "original.dawg";
    require(saveProject(snapshot, original).has_value(), "seed valid destination");
    const auto before = readDocument(original);
    const auto entries = entryCount(root);
    requireError(saveProject(snapshot, original / "invalid-parent.dawg"), Code::ioFailure);
    require(readDocument(original) == before && loadProject(original).has_value(),
            "using original file as an invalid parent leaves it unchanged and readable");
    require(entryCount(root) == entries, "failed temporary open leaves no siblings");
}

void testRenameFailure(const fs::path& root)
{
    const ProjectSnapshot snapshot {2, 7, "Replacement", {}, {}, {}};
    const auto destination = root / "directory.dawg";
    fs::create_directory(destination);
    const auto marker = destination / "keep.dawg";
    require(saveProject(snapshot, marker).has_value(), "seed marker in destination directory");
    const auto before = readDocument(marker);
    const auto entries = entryCount(root);
    requireError(saveProject(snapshot, destination), Code::ioFailure);
    require(fs::is_directory(destination) && readDocument(marker) == before,
            "failed rename preserves destination directory and contents");
    require(entryCount(root) == entries, "failed rename removes only its temporary file");
    require(entryCount(destination) == 1, "failed rename leaves destination contents intact");
}

void testReplacement(const fs::path& root)
{
    const auto path = root / "replace.dawg";
    const ProjectSnapshot original {2, 1, "Long original name", {{99, "Original", true, true}}, {}, {}};
    require(saveProject(original, path).has_value(), "seed replaceable file");
    const auto entries = entryCount(root);
    const ProjectSnapshot replacement {2, 2, "New", {}, {}, {}};
    require(saveProject(replacement, path).has_value(), "replace existing destination");
    const auto loaded = loadProject(path);
    require(loaded.has_value() && loaded->id == 2 && loaded->name == "New" && loaded->tracks.empty(),
            "replacement publishes complete new content without leftover bytes");
    require(entryCount(root) == entries, "successful replacement leaves no temporary files");
}
}

int main()
{
    const TestDirectory directory;
    testRoundTrip(directory.root);
    testExternalDocument(directory.root);
    testMalformedDocuments(directory.root);
    testRejectedSaves(directory.root);
    testPathFailures(directory.root);
    testRenameFailure(directory.root);
    testReplacement(directory.root);
    testVersionThreeFivePillarRoundTrip(directory.root);
    std::puts("PASS: 8 persistence behavior tests");
}
