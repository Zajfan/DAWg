#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace dawg::persistence {
namespace {
namespace fs = std::filesystem;
using Code = ProjectFileError::Code;
using Json = nlohmann::json;

std::unexpected<ProjectFileError> failure(const Code code, std::string message)
{
    return std::unexpected(ProjectFileError {code, std::move(message)});
}

bool validPath(const fs::path& path)
{
    const auto& native = path.native();
    return ! path.empty() && ! path.filename().empty() && path.filename() != "."
        && path.filename() != ".."
        && native.find(fs::path::value_type {}) == fs::path::string_type::npos;
}

// Exclusive creation prevents stale files or concurrent saves from sharing a
// temporary file. Cleanup owns only the file successfully created by this call.
struct TemporaryFile final
{
    fs::path path;
    std::FILE* handle {};
    bool owned {};

    explicit TemporaryFile(const fs::path& destination) : path {destination} {}
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile()
    {
        if (handle != nullptr)
            std::fclose(handle);
        if (owned)
        {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
};

std::expected<void, ProjectFileError> writeTransaction(
    const std::string& document, const fs::path& destination)
{
    static std::atomic<std::uint64_t> sequence {};
#if defined(_WIN32)
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    TemporaryFile temporary {destination};
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        temporary.path = destination;
        temporary.path += ".tmp-" + std::to_string(processId) + "-"
                        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
        _wfopen_s(&temporary.handle, temporary.path.c_str(), L"wbx");
#else
        temporary.handle = std::fopen(temporary.path.c_str(), "wbx");
#endif
        if (temporary.handle != nullptr)
        {
            temporary.owned = true;
            break;
        }
        if (errno != EEXIST)
            return failure(Code::ioFailure, "Cannot create sibling temporary project file");
    }
    if (! temporary.owned)
        return failure(Code::ioFailure, "Cannot reserve a unique temporary project file");

    if (std::fwrite(document.data(), 1, document.size(), temporary.handle) != document.size()
        || std::fflush(temporary.handle) != 0)
        return failure(Code::ioFailure, "Cannot write or flush temporary project file");

    auto* handle = std::exchange(temporary.handle, nullptr);
    if (std::fclose(handle) != 0)
        return failure(Code::ioFailure, "Cannot close temporary project file");

    std::error_code error;
    fs::rename(temporary.path, destination, error);
    if (error)
        return failure(Code::ioFailure, "Cannot replace project file: " + error.message());
    temporary.owned = false;
    return {};
}

std::expected<project::ProjectSnapshot, ProjectFileError> decode(const Json& document)
{
    const auto& version = document.at("formatVersion");
    if (! version.is_number_integer())
        return failure(Code::invalidFormat, "formatVersion must be an integer");
    // Compare before narrowing, including versions larger than uint32_t.
    if (version != Json(1))
        return failure(Code::unsupportedVersion, "Only project format version 1 is supported");

    const auto& id = document.at("projectId");
    const auto& tracks = document.at("tracks");
    if (! id.is_number_unsigned() || ! tracks.is_array())
        return failure(Code::invalidFormat, "Expected an unsigned projectId and a tracks array");

    project::ProjectSnapshot snapshot {
        1, id.get<project::ProjectId>(), document.at("name").get<std::string>(), {}, {}, {}};
    snapshot.tracks.reserve(tracks.size());
    for (const auto& track : tracks)
    {
        const auto& trackId = track.at("id");
        const auto& muted = track.at("muted");
        const auto& solo = track.at("solo");
        if (! trackId.is_number_unsigned() || ! muted.is_boolean() || ! solo.is_boolean())
            return failure(Code::invalidFormat, "Expected an unsigned track ID and boolean flags");
        snapshot.tracks.push_back({trackId.get<project::TrackId>(),
            track.at("name").get<std::string>(), muted.get<bool>(), solo.get<bool>()});
    }

    // Keep model invariants (including duplicate IDs) owned by the project layer.
    const auto loaded = project::Project::fromSnapshot(std::move(snapshot));
    if (! loaded)
        return failure(Code::invalidFormat, "Project IDs and names must be valid and track IDs unique");
    return loaded->snapshot();
}
} // namespace

std::expected<void, ProjectFileError> saveProject(
    const project::ProjectSnapshot& snapshot, const fs::path& path)
{
    try
    {
        if (! validPath(path))
            return failure(Code::invalidPath, "Expected a nonempty project file path without NUL characters");
        if (snapshot.formatVersion != 1)
            return failure(Code::unsupportedVersion, "Only project format version 1 is supported");
        if (! project::Project::fromSnapshot(snapshot))
            return failure(Code::invalidFormat, "Project IDs and names must be valid and track IDs unique");

        Json tracks = Json::array();
        for (const auto& track : snapshot.tracks)
            tracks.push_back({{"id", track.id}, {"name", track.name},
                              {"muted", track.muted}, {"solo", track.solo}});
        const Json document {{"formatVersion", snapshot.formatVersion}, {"projectId", snapshot.id},
                             {"name", snapshot.name}, {"tracks", std::move(tracks)}};
        // Serialize completely before creating a temporary file (e.g. invalid UTF-8).
        return writeTransaction(document.dump(2) + '\n', path);
    }
    catch (const Json::exception& error)
    {
        return failure(Code::invalidFormat, error.what());
    }
    catch (const fs::filesystem_error& error)
    {
        return failure(Code::ioFailure, error.what());
    }
}

std::expected<project::ProjectSnapshot, ProjectFileError> loadProject(const fs::path& path)
{
    try
    {
        if (! validPath(path))
            return failure(Code::invalidPath, "Expected a nonempty project file path without NUL characters");
        std::error_code error;
        if (! fs::is_regular_file(path, error) || error)
            return failure(Code::ioFailure, "Cannot read project file as a regular file");
        std::ifstream input(path, std::ios::binary);
        if (! input.is_open())
            return failure(Code::ioFailure, "Cannot open project file");
        try
        {
            // parse() consumes the whole document and rejects trailing data.
            const auto document = Json::parse(input);
            if (input.bad())
                return failure(Code::ioFailure, "Cannot read project file");
            return decode(document);
        }
        catch (const Json::exception& exception)
        {
            return failure(input.bad() ? Code::ioFailure : Code::invalidFormat, exception.what());
        }
    }
    catch (const fs::filesystem_error& error)
    {
        return failure(Code::ioFailure, error.what());
    }
    catch (const std::ios_base::failure& error)
    {
        return failure(Code::ioFailure, error.what());
    }
}

} // namespace dawg::persistence
