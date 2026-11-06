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
    auto original = project.snapshot();
    original.tracks.front().muted = true;
    original.tracks.back().solo = true;
    const auto path = root / "round-trip.dawg";
    require(saveProject(original, path).has_value(), "save succeeds");
    const auto loaded = loadProject(path);
    require(loaded.has_value(), "load succeeds");
    require(loaded->formatVersion == original.formatVersion, "version survives round trip");
    require(loaded->id == original.id && loaded->name == original.name,
            "project ID and escaped Unicode name survive round trip");
    require(loaded->tracks == original.tracks, "ordered tracks and flags survive round trip");
    const auto document = readDocument(path);
    for (const auto key : {"\"formatVersion\"", "\"projectId\"", "\"name\"", "\"tracks\"",
                           "\"id\"", "\"muted\"", "\"solo\""})
        require(document.find(key) != std::string::npos, "document has the public schema fields");

    const ProjectSnapshot empty {1, std::numeric_limits<ProjectId>::max(), "Empty", {}};
    require(saveProject(empty, path).has_value(), "existing file can be replaced with empty project");
    const auto reloaded = loadProject(path);
    require(reloaded.has_value() && reloaded->tracks.empty() && reloaded->id == empty.id,
            "empty tracks and maximum project ID survive replacement");
}

void testExternalDocument(const fs::path& root)
{
    const auto path = root / "external.dawg";
    writeDocument(path, R"({"formatVersion":1,"projectId":18446744073709551615,"name":"Loaded","tracks":[{"id":7,"name":"First","muted":true,"solo":true},{"id":4000,"name":"Lead","muted":false,"solo":true},{"id":3,"name":"Last","muted":false,"solo":false}]})");
    const auto loaded = loadProject(path);
    require(loaded.has_value(), "independent version-one fixture loads");
    require(loaded->formatVersion == 1 && loaded->id == std::numeric_limits<ProjectId>::max()
                && loaded->name == "Loaded", "external metadata loads exactly");
    require(loaded->tracks == std::vector<Track> {
                {7, "First", true, true}, {4000, "Lead", false, true}, {3, "Last", false, false}},
            "external tracks preserve IDs, flags and document order");
    auto editable = Project::fromSnapshot(*loaded);
    require(editable.has_value() && editable->addAudioTrack("New") == 4001,
            "loaded snapshot reconstructs with the largest-ID high-water mark");
    auto maximum = *loaded;
    maximum.tracks.front().id = std::numeric_limits<TrackId>::max();
    require(saveProject(maximum, path).has_value(), "maximum track ID saves without narrowing");
    const auto reloaded = loadProject(path);
    require(reloaded.has_value() && reloaded->tracks == maximum.tracks,
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
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[]} {})",
        R"({"formatVersion":1,"projectId":42,"name":"P","tracks":[]} trailing)"
    };
    for (const auto document : documents)
    {
        writeDocument(path, document);
        requireError(loadProject(path), Code::invalidFormat);
    }
    for (const auto version : {"0", "2", "999", "-1", "4294967297", "18446744073709551615"})
    {
        writeDocument(path, std::string {R"({"formatVersion":)"} + version +
                            R"(,"projectId":42,"name":"P","tracks":[]})");
        requireError(loadProject(path), Code::unsupportedVersion);
    }
}

void testRejectedSaves(const fs::path& root)
{
    const auto path = root / "preserved.dawg";
    const ProjectSnapshot valid {1, 42, "Original", {{1, "Track", false, true}}};
    require(saveProject(valid, path).has_value(), "create original destination");
    const auto before = readDocument(path);
    const auto entries = entryCount(root);
    const std::array<ProjectSnapshot, 6> invalid {{
        {1, 0, "P", {}}, {1, 1, "", {}},
        {1, 1, "P", {{0, "T", false, false}}},
        {1, 1, "P", {{1, "", false, false}}},
        {1, 1, "P", {{7, "T", false, false}, {7, "U", true, false}}},
        {1, 1, std::string(1, static_cast<char>(0xff)), {}}
    }};
    for (const auto& snapshot : invalid)
    {
        requireError(saveProject(snapshot, path), Code::invalidFormat);
        require(readDocument(path) == before, "invalid save preserves original bytes");
        require(entryCount(root) == entries, "invalid save leaves no temporary files");
    }
    auto unsupported = valid;
    unsupported.formatVersion = 999;
    requireError(saveProject(unsupported, path), Code::unsupportedVersion);
    require(readDocument(path) == before, "unsupported save preserves original bytes");
}

void testPathFailures(const fs::path& root)
{
    const ProjectSnapshot snapshot {1, 42, "Original", {}};
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
    const ProjectSnapshot snapshot {1, 7, "Replacement", {}};
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
    const ProjectSnapshot original {1, 1, "Long original name", {{99, "Original", true, true}}};
    require(saveProject(original, path).has_value(), "seed replaceable file");
    const auto entries = entryCount(root);
    const ProjectSnapshot replacement {1, 2, "New", {}};
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
    std::puts("PASS: 7 persistence behavior tests");
}
