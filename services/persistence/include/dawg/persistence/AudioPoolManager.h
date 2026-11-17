#pragma once

#include <dawg/project/ProjectSnapshot.h>
#include <dawg/project/ProjectTypes.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dawg::persistence {

struct AudioPoolItem
{
    project::MediaAssetId assetId {0};
    std::string originalPath;
    std::string resolvedPath;
    bool isMissing {false};
    std::uint32_t sampleRate {0};
    std::uint16_t channelCount {0};
    std::uint64_t frameCount {0};
    double durationSeconds {0.0};
    std::uint64_t fileSizeBytes {0};
    std::uint32_t arrangementUsageCount {0};
    std::uint32_t takeUsageCount {0};
    std::uint32_t totalUsageCount {0};
};

struct RelinkResult
{
    project::MediaAssetId assetId {0};
    std::string oldPath;
    std::string newPath;
    bool relinked {false};
};

// Project Audio Pool Manager & Missing Asset Relinker
// - Catalogs all WAV/BWF assets with durations, file sizes, and clip usage counters.
// - Resolves missing audio assets with recursive folder search.
// - Identifies and purges unused takes to reclaim disk space.
// - Consolidates external audio into the project audio directory.
class AudioPoolManager final
{
public:
    AudioPoolManager() = default;

    void scanProject(
        const project::ProjectSnapshot& snapshot,
        const std::filesystem::path& projectDirectory);

    [[nodiscard]] const std::vector<AudioPoolItem>& poolItems() const noexcept { return m_items; }
    [[nodiscard]] std::size_t missingAssetCount() const noexcept;
    [[nodiscard]] std::size_t totalAssetCount() const noexcept { return m_items.size(); }
    [[nodiscard]] std::uint64_t totalPoolBytes() const noexcept;

    std::vector<RelinkResult> relinkMissingAssets(
        const std::filesystem::path& searchDirectory,
        bool recursive = true);

    [[nodiscard]] std::vector<AudioPoolItem> findUnusedAssets() const;

    std::vector<project::MediaAssetId> purgeUnusedAssets(bool deleteFromDisk = false);

    bool consolidatePool(
        const std::filesystem::path& targetAudioDirectory,
        project::ProjectSnapshot& inOutSnapshot);

private:
    std::vector<AudioPoolItem> m_items;
    std::filesystem::path m_projectDirectory;
};

} // namespace dawg::persistence
