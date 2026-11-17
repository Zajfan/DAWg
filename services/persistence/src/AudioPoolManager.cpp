#include <dawg/persistence/AudioPoolManager.h>

#include <algorithm>
#include <system_error>

namespace dawg::persistence {

void AudioPoolManager::scanProject(
    const project::ProjectSnapshot& snapshot,
    const std::filesystem::path& projectDirectory)
{
    m_projectDirectory = projectDirectory;
    m_items.clear();
    m_items.reserve(snapshot.assets.size());

    for (const auto& asset : snapshot.assets)
    {
        AudioPoolItem item {};
        item.assetId = asset.id;
        item.originalPath = asset.path;
        item.sampleRate = asset.sampleRate;
        item.channelCount = asset.channelCount;
        item.frameCount = asset.frameCount;
        item.durationSeconds = (asset.sampleRate > 0)
            ? static_cast<double>(asset.frameCount) / static_cast<double>(asset.sampleRate)
            : 0.0;

        // Resolve absolute filesystem path
        std::filesystem::path fullPath = (asset.pathKind == project::AudioAssetPathKind::projectRelative)
            ? (projectDirectory / asset.path)
            : std::filesystem::path(asset.path);

        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec))
        {
            item.resolvedPath = fullPath.lexically_normal().string();
            item.isMissing = false;
            item.fileSizeBytes = std::filesystem::file_size(fullPath, ec);
            if (ec) item.fileSizeBytes = 0;
        }
        else
        {
            item.resolvedPath = fullPath.string();
            item.isMissing = true;
            item.fileSizeBytes = 0;
        }

        // Count arrangement usages
        for (const auto& clip : snapshot.clips)
        {
            if (clip.assetId == asset.id)
            {
                item.arrangementUsageCount++;
            }
        }

        // Count take usages across tracks
        for (const auto& track : snapshot.tracks)
        {
            for (const auto& take : track.takes)
            {
                if (take.assetId == asset.id)
                {
                    item.takeUsageCount++;
                }
            }
        }

        item.totalUsageCount = item.arrangementUsageCount + item.takeUsageCount;
        m_items.push_back(std::move(item));
    }
}

std::size_t AudioPoolManager::missingAssetCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& item : m_items)
    {
        if (item.isMissing)
            ++count;
    }
    return count;
}

std::uint64_t AudioPoolManager::totalPoolBytes() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& item : m_items)
    {
        total += item.fileSizeBytes;
    }
    return total;
}

std::vector<RelinkResult> AudioPoolManager::relinkMissingAssets(
    const std::filesystem::path& searchDirectory,
    bool recursive)
{
    std::vector<RelinkResult> results;
    std::error_code ec;
    if (!std::filesystem::exists(searchDirectory, ec) || !std::filesystem::is_directory(searchDirectory, ec))
        return results;

    // Collect all files in search directory
    std::vector<std::filesystem::path> candidateFiles;
    if (recursive)
    {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(searchDirectory, ec))
        {
            if (entry.is_regular_file(ec))
                candidateFiles.push_back(entry.path());
        }
    }
    else
    {
        for (const auto& entry : std::filesystem::directory_iterator(searchDirectory, ec))
        {
            if (entry.is_regular_file(ec))
                candidateFiles.push_back(entry.path());
        }
    }

    for (auto& item : m_items)
    {
        if (!item.isMissing)
            continue;

        const std::filesystem::path origFilename = std::filesystem::path(item.originalPath).filename();

        // Search for matching filename
        auto it = std::find_if(candidateFiles.begin(), candidateFiles.end(),
            [&origFilename](const std::filesystem::path& p) {
                return p.filename() == origFilename;
            });

        if (it != candidateFiles.end())
        {
            RelinkResult res {};
            res.assetId = item.assetId;
            res.oldPath = item.resolvedPath;
            res.newPath = it->lexically_normal().string();
            res.relinked = true;

            item.resolvedPath = res.newPath;
            item.isMissing = false;
            item.fileSizeBytes = std::filesystem::file_size(*it, ec);
            if (ec) item.fileSizeBytes = 0;

            results.push_back(std::move(res));
        }
    }

    return results;
}

std::vector<AudioPoolItem> AudioPoolManager::findUnusedAssets() const
{
    std::vector<AudioPoolItem> unused;
    for (const auto& item : m_items)
    {
        if (item.totalUsageCount == 0)
        {
            unused.push_back(item);
        }
    }
    return unused;
}

std::vector<project::MediaAssetId> AudioPoolManager::purgeUnusedAssets(bool deleteFromDisk)
{
    std::vector<project::MediaAssetId> purgedIds;
    std::error_code ec;

    auto it = std::remove_if(m_items.begin(), m_items.end(),
        [deleteFromDisk, &purgedIds, &ec](const AudioPoolItem& item) {
            if (item.totalUsageCount == 0)
            {
                purgedIds.push_back(item.assetId);
                if (deleteFromDisk && !item.isMissing && !item.resolvedPath.empty())
                {
                    std::filesystem::remove(item.resolvedPath, ec);
                }
                return true;
            }
            return false;
        });

    m_items.erase(it, m_items.end());
    return purgedIds;
}

bool AudioPoolManager::consolidatePool(
    const std::filesystem::path& targetAudioDirectory,
    project::ProjectSnapshot& inOutSnapshot)
{
    std::error_code ec;
    std::filesystem::create_directories(targetAudioDirectory, ec);
    if (ec) return false;

    for (auto& item : m_items)
    {
        if (item.isMissing)
            continue;

        const std::filesystem::path currentPath(item.resolvedPath);
        const std::filesystem::path destPath = targetAudioDirectory / currentPath.filename();

        // If not already in destination folder, copy it
        if (currentPath != destPath)
        {
            std::filesystem::copy_file(currentPath, destPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec)
            {
                item.resolvedPath = destPath.lexically_normal().string();
                item.originalPath = (std::filesystem::path("audio") / currentPath.filename()).string();

                // Update snapshot asset path to relative
                for (auto& asset : inOutSnapshot.assets)
                {
                    if (asset.id == item.assetId)
                    {
                        asset.path = item.originalPath;
                        asset.pathKind = project::AudioAssetPathKind::projectRelative;
                        break;
                    }
                }
            }
        }
    }

    return true;
}

} // namespace dawg::persistence
