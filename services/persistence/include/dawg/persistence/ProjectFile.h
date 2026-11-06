#pragma once

#include <dawg/project/ProjectSnapshot.h>

#include <expected>
#include <filesystem>
#include <string>

namespace dawg::persistence {

struct ProjectFileError
{
    enum class Code { invalidPath, ioFailure, invalidFormat, unsupportedVersion };
    Code code;
    std::string message;
};

// Synchronous filesystem operations; schedule off the message and audio threads.
// Saving flushes/closes a sibling temporary file before rename. Failure preserves
// an existing destination; this does not promise power-loss durability (fsync).
[[nodiscard]] std::expected<void, ProjectFileError> saveProject(
    const project::ProjectSnapshot& snapshot, const std::filesystem::path& path);
[[nodiscard]] std::expected<project::ProjectSnapshot, ProjectFileError> loadProject(
    const std::filesystem::path& path);

} // namespace dawg::persistence
