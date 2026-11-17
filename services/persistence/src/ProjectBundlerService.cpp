#include <dawg/persistence/ProjectBundlerService.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dawg::persistence {

namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string ProjectBundlerService::computeSimpleChecksum(const fs::path& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return "00000000";
    }

    // 64-bit FNV-1a hash
    std::uint64_t hash = 14695981039346656037ULL;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        const auto bytesRead = static_cast<std::size_t>(file.gcount());
        for (std::size_t i = 0; i < bytesRead; ++i) {
            hash ^= static_cast<std::uint8_t>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

std::expected<void, ProjectFileError> ProjectBundlerService::bundleProject(
    const project::ProjectSnapshot& snapshot,
    const fs::path& packageDirectory,
    const std::vector<fs::path>& assetFiles,
    const std::string& author)
{
    std::error_code ec;
    fs::create_directories(packageDirectory / "assets", ec);
    if (ec) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::ioFailure,
            "Failed to create package directory: " + ec.message()
        });
    }

    // 1. Save core project snapshot JSON
    const auto projectPath = packageDirectory / "project.json";
    auto saveRes = saveProject(snapshot, projectPath);
    if (!saveRes) {
        return saveRes;
    }

    // 2. Copy assets and compute checksums
    PackageManifest manifest;
    manifest.packageFormatVersion = "1.0";
    manifest.projectId = std::to_string(snapshot.id);
    manifest.projectName = snapshot.name;
    manifest.author = author;

    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeStr;
    timeStr << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
    manifest.timestampIso = timeStr.str();

    for (const auto& asset : assetFiles) {
        if (!fs::exists(asset)) {
            continue;
        }

        const auto destAsset = packageDirectory / "assets" / asset.filename();
        fs::copy_file(asset, destAsset, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            continue;
        }

        AssetPackageEntry entry;
        entry.relativePath = "assets/" + asset.filename().string();
        entry.fileSizeBytes = fs::file_size(destAsset, ec);
        entry.checksumHex = computeSimpleChecksum(destAsset);
        manifest.assets.push_back(entry);
    }

    // 3. Write manifest.json
    Json manifestJson;
    manifestJson["packageFormatVersion"] = manifest.packageFormatVersion;
    manifestJson["projectId"] = manifest.projectId;
    manifestJson["projectName"] = manifest.projectName;
    manifestJson["author"] = manifest.author;
    manifestJson["timestampIso"] = manifest.timestampIso;

    Json assetsArray = Json::array();
    for (const auto& a : manifest.assets) {
        Json aObj;
        aObj["relativePath"] = a.relativePath;
        aObj["fileSizeBytes"] = a.fileSizeBytes;
        aObj["checksumHex"] = a.checksumHex;
        assetsArray.push_back(aObj);
    }
    manifestJson["assets"] = assetsArray;

    std::ofstream manifestFile(packageDirectory / "manifest.json");
    if (!manifestFile.is_open()) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::ioFailure,
            "Failed to write manifest.json"
        });
    }
    manifestFile << manifestJson.dump(2);

    return {};
}

std::expected<project::ProjectSnapshot, ProjectFileError> ProjectBundlerService::unbundleProject(
    const fs::path& packageDirectory)
{
    const auto manifestPath = packageDirectory / "manifest.json";
    const auto projectPath = packageDirectory / "project.json";

    if (!fs::exists(manifestPath) || !fs::exists(projectPath)) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::invalidFormat,
            "Package is missing manifest.json or project.json"
        });
    }

    // Verify manifest assets checksums
    std::ifstream manifestFile(manifestPath);
    if (!manifestFile.is_open()) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::ioFailure,
            "Could not open manifest.json"
        });
    }

    try {
        Json manifestJson = Json::parse(manifestFile);
        if (manifestJson.contains("assets") && manifestJson["assets"].is_array()) {
            for (const auto& a : manifestJson["assets"]) {
                const std::string relPath = a.value("relativePath", "");
                const std::string expectedChecksum = a.value("checksumHex", "");

                const auto assetFullPath = packageDirectory / relPath;
                if (fs::exists(assetFullPath)) {
                    const std::string actualChecksum = computeSimpleChecksum(assetFullPath);
                    if (actualChecksum != expectedChecksum) {
                        return std::unexpected(ProjectFileError {
                            ProjectFileError::Code::invalidFormat,
                            "Checksum mismatch for asset: " + relPath
                        });
                    }
                }
            }
        }
    } catch (...) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::invalidFormat,
            "Failed to parse manifest.json"
        });
    }

    return loadProject(projectPath);
}

bool ProjectBundlerService::createBranch(const std::string& branchName, const std::string& fromCommitId)
{
    if (branchName.empty()) {
        return false;
    }

    for (const auto& b : m_branches) {
        if (b == branchName) {
            return false; // Branch already exists
        }
    }

    m_branches.push_back(branchName);

    if (!fromCommitId.empty()) {
        for (const auto& c : m_commits) {
            if (c.commitId == fromCommitId) {
                // Point new branch to base commit snapshot
                commit(branchName, "Branch created from " + fromCommitId, c.author, c.snapshot);
                break;
            }
        }
    }

    m_activeBranch = branchName;
    return true;
}

std::string ProjectBundlerService::commit(
    const std::string& branchName,
    const std::string& message,
    const std::string& author,
    const project::ProjectSnapshot& snapshot)
{
    std::string parentId = "";
    for (auto it = m_commits.rbegin(); it != m_commits.rend(); ++it) {
        if (it->branchName == branchName) {
            parentId = it->commitId;
            break;
        }
    }

    // Generate commit ID
    std::ostringstream idGen;
    idGen << std::hex << std::setfill('0') << std::setw(8)
          << (m_commits.size() + 1)
          << "_" << (std::hash<std::string>{}(message + branchName) & 0xFFFFFF);
    const std::string newId = idGen.str();

    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeStr;
    timeStr << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");

    m_commits.push_back(ProjectCommit {
        .commitId = newId,
        .parentCommitId = parentId,
        .branchName = branchName,
        .message = message,
        .author = author,
        .timestampIso = timeStr.str(),
        .snapshot = snapshot
    });

    m_activeBranch = branchName;
    return newId;
}

std::optional<project::ProjectSnapshot> ProjectBundlerService::checkout(const std::string& branchOrCommitId)
{
    // Try commit ID first
    for (const auto& c : m_commits) {
        if (c.commitId == branchOrCommitId) {
            m_activeBranch = c.branchName;
            return c.snapshot;
        }
    }

    // Try branch name (latest commit on branch)
    for (auto it = m_commits.rbegin(); it != m_commits.rend(); ++it) {
        if (it->branchName == branchOrCommitId) {
            m_activeBranch = it->branchName;
            return it->snapshot;
        }
    }

    return std::nullopt;
}

std::vector<ProjectCommit> ProjectBundlerService::commitHistory(const std::string& branchName) const
{
    std::vector<ProjectCommit> history;
    for (const auto& c : m_commits) {
        if (branchName.empty() || c.branchName == branchName) {
            history.push_back(c);
        }
    }
    return history;
}

std::vector<std::string> ProjectBundlerService::branches() const
{
    return m_branches;
}

std::string ProjectBundlerService::formatStemFilename(
    const StemExportDescriptor& desc,
    double bpm,
    const std::string& musicalKey)
{
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << desc.trackIndex << "_"
       << desc.trackName << "_"
       << static_cast<int>(std::round(bpm)) << "bpm_"
       << musicalKey << "_"
       << (desc.preFx ? "preFX" : "postFX") << "_"
       << std::fixed << std::setprecision(1) << desc.targetLufs << "lufs.wav";
    return ss.str();
}

std::string ProjectBundlerService::expandNamingTokens(
    const std::string& pattern,
    const StemExportDescriptor& desc,
    double bpm,
    const std::string& musicalKey)
{
    std::string result = pattern;
    auto replaceAll = [&](const std::string& token, const std::string& val) {
        std::size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.length(), val);
            pos += val.length();
        }
    };

    std::ostringstream numStr;
    numStr << std::setfill('0') << std::setw(2) << desc.trackIndex;
    replaceAll("{TrackNum}", numStr.str());
    replaceAll("{TrackName}", desc.trackName);

    std::ostringstream bpmStr;
    bpmStr << static_cast<int>(std::round(bpm)) << "BPM";
    replaceAll("{BPM}", bpmStr.str());

    replaceAll("{Key}", musicalKey);
    replaceAll("{Type}", desc.preFx ? "Dry" : "Wet");

    std::ostringstream lufsStr;
    lufsStr << std::fixed << std::setprecision(1) << desc.targetLufs << "LUFS";
    replaceAll("{LUFS}", lufsStr.str());

    return result;
}

std::expected<project::ProjectSnapshot, ProjectFileError> ProjectBundlerService::consolidateAndArchive(
    const project::ProjectSnapshot& snapshot,
    const fs::path& destinationFolder,
    const fs::path& projectSourceRoot)
{
    std::error_code ec;
    const auto audioDir = destinationFolder / "Audio";
    fs::create_directories(audioDir, ec);
    if (ec) {
        return std::unexpected(ProjectFileError {
            ProjectFileError::Code::ioFailure,
            "Failed to create bundle Audio directory: " + ec.message()
        });
    }

    project::ProjectSnapshot consolidated = snapshot;
    PackageManifest manifest;
    manifest.packageFormatVersion = "1.0";
    manifest.projectId = std::to_string(snapshot.id);
    manifest.projectName = snapshot.name;
    manifest.author = "Producer";

    const auto now = std::chrono::system_clock::now();
    const auto timeT = std::chrono::system_clock::to_time_t(now);
    std::ostringstream timeStr;
    timeStr << std::put_time(std::gmtime(&timeT), "%Y-%m-%dT%H:%M:%SZ");
    manifest.timestampIso = timeStr.str();

    for (auto& asset : consolidated.assets) {
        fs::path sourceFile;
        if (asset.pathKind == project::AudioAssetPathKind::absolute || fs::path(asset.path).is_absolute()) {
            sourceFile = asset.path;
        } else if (!projectSourceRoot.empty()) {
            sourceFile = projectSourceRoot / asset.path;
        } else {
            sourceFile = asset.path;
        }

        const auto filename = fs::path(asset.path).filename();
        const auto targetFile = audioDir / filename;

        if (fs::exists(sourceFile) && sourceFile != targetFile) {
            fs::copy_file(sourceFile, targetFile, fs::copy_options::overwrite_existing, ec);
        }

        asset.path = "Audio/" + filename.string();
        asset.pathKind = project::AudioAssetPathKind::projectRelative;

        AssetPackageEntry entry;
        entry.relativePath = asset.path;
        entry.fileSizeBytes = fs::exists(targetFile) ? fs::file_size(targetFile, ec) : 0;
        entry.checksumHex = fs::exists(targetFile) ? computeSimpleChecksum(targetFile) : "00000000";
        manifest.assets.push_back(std::move(entry));
    }

    // Save project.json
    const auto projectJsonPath = destinationFolder / "project.json";
    const auto saveRes = saveProject(consolidated, projectJsonPath);
    if (!saveRes) {
        return std::unexpected(saveRes.error());
    }

    // Save manifest.json
    Json manifestJson;
    manifestJson["packageFormatVersion"] = manifest.packageFormatVersion;
    manifestJson["projectId"] = manifest.projectId;
    manifestJson["projectName"] = manifest.projectName;
    manifestJson["author"] = manifest.author;
    manifestJson["timestampIso"] = manifest.timestampIso;
    Json assetsArray = Json::array();
    for (const auto& a : manifest.assets) {
        assetsArray.push_back({
            {"relativePath", a.relativePath},
            {"fileSizeBytes", a.fileSizeBytes},
            {"checksumHex", a.checksumHex}
        });
    }
    manifestJson["assets"] = std::move(assetsArray);

    std::ofstream manifestFile(destinationFolder / "manifest.json");
    if (manifestFile.is_open()) {
        manifestFile << manifestJson.dump(2);
    }

    return consolidated;
}

float ProjectBundlerService::calculateLoudnessNormalizationGain(
    float measuredLufs,
    float targetLufs) noexcept
{
    const float deltaDb = targetLufs - measuredLufs;
    return std::pow(10.0f, deltaDb / 20.0f);
}

} // namespace dawg::persistence
