#pragma once

#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/ProjectSnapshot.h>

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dawg::persistence {

struct AssetPackageEntry
{
    std::string relativePath;
    std::uint64_t fileSizeBytes {0};
    std::string checksumHex;
};

struct PackageManifest
{
    std::string packageFormatVersion {"1.0"};
    std::string projectId;
    std::string projectName;
    std::string author;
    std::string timestampIso;
    std::vector<AssetPackageEntry> assets;
};

struct ProjectCommit
{
    std::string commitId;
    std::string parentCommitId;
    std::string branchName;
    std::string message;
    std::string author;
    std::string timestampIso;
    project::ProjectSnapshot snapshot;
};

struct StemExportDescriptor
{
    std::size_t trackIndex {0};
    std::string trackName {"Master"};
    bool preFx {false};                 // false = post-FX rendered, true = raw pre-FX stem
    float measuredLufs {-18.0f};
    float targetLufs {-14.0f};
    bool normalizeLoudness {true};
};

// Deterministic Project Bundler (.dawgpkg), Branching Version History & Lossless Stem Exporter
class ProjectBundlerService
{
public:
    ProjectBundlerService() = default;
    ~ProjectBundlerService() = default;

    // 1. Deterministic Project Bundler (.dawgpkg)
    [[nodiscard]] std::expected<void, ProjectFileError> bundleProject(
        const project::ProjectSnapshot& snapshot,
        const std::filesystem::path& packageDirectory,
        const std::vector<std::filesystem::path>& assetFiles,
        const std::string& author = "Producer");

    [[nodiscard]] std::expected<project::ProjectSnapshot, ProjectFileError> unbundleProject(
        const std::filesystem::path& packageDirectory);

    // 2. Branching & Song Commit History (Splice / Git style)
    bool createBranch(const std::string& branchName, const std::string& fromCommitId = "");
    std::string commit(
        const std::string& branchName,
        const std::string& message,
        const std::string& author,
        const project::ProjectSnapshot& snapshot);

    [[nodiscard]] std::optional<project::ProjectSnapshot> checkout(const std::string& branchOrCommitId);
    [[nodiscard]] std::vector<ProjectCommit> commitHistory(const std::string& branchName) const;
    [[nodiscard]] std::vector<std::string> branches() const;
    [[nodiscard]] std::string activeBranch() const { return m_activeBranch; }

    // 3. Lossless Stem Exporter Helpers
    static std::string formatStemFilename(
        const StemExportDescriptor& desc,
        double bpm,
        const std::string& musicalKey = "Cmaj");

    static float calculateLoudnessNormalizationGain(
        float measuredLufs,
        float targetLufs) noexcept;

    static std::string computeSimpleChecksum(const std::filesystem::path& filePath);

private:
    std::string m_activeBranch {"main"};
    std::vector<ProjectCommit> m_commits;
    std::vector<std::string> m_branches {"main"};
};

} // namespace dawg::persistence
