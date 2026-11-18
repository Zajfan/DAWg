#include <dawg/plugins/PluginPresetManager.h>

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

namespace dawg::plugins {

using Json = nlohmann::json;

PluginPresetManager::PluginPresetManager()
    : PluginPresetManager(std::filesystem::temp_directory_path() / "dawg_presets")
{
}

PluginPresetManager::PluginPresetManager(std::filesystem::path userPresetDirectory)
    : m_userPresetDir(std::move(userPresetDirectory))
{
    initFactoryPresets();
    reloadPresets();
}

void PluginPresetManager::initFactoryPresets()
{
    // Populate factory presets from NativePluginCatalog
    for (const auto& plugin : NativePluginCatalog::allPlugins())
    {
        for (std::size_t i = 0; i < plugin.presets.size(); ++i)
        {
            const auto& p = plugin.presets[i];
            PluginPreset preset;
            preset.presetId = "factory_" + std::to_string(i + 1);
            preset.pluginId = plugin.descriptor.id;
            preset.name = p.name;
            preset.author = "The No-hands Company";
            preset.category = plugin.descriptor.isInstrument ? "Factory Instruments" : "Factory Effects";
            preset.tags = {"factory", plugin.descriptor.isInstrument ? "synth" : "effect"};
            preset.rating = 5;
            preset.parameters = p.values;
            preset.isFactory = true;
            m_presets.push_back(std::move(preset));
        }
    }
}

void PluginPresetManager::reloadPresets()
{
    // Remove user presets, retain factory
    m_presets.erase(
        std::remove_if(m_presets.begin(), m_presets.end(), [](const PluginPreset& p) {
            return ! p.isFactory;
        }),
        m_presets.end()
    );

    std::error_code ec;
    if (! std::filesystem::exists(m_userPresetDir, ec))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(m_userPresetDir, ec))
    {
        if (! entry.is_regular_file() || entry.path().extension() != ".preset")
            continue;

        std::ifstream file(entry.path());
        if (! file.is_open())
            continue;

        try
        {
            Json j;
            file >> j;
            PluginPreset p;
            p.presetId = j.value("presetId", entry.path().stem().string());
            p.pluginId = j.value("pluginId", "");
            p.name = j.value("name", "Untitled Preset");
            p.author = j.value("author", "User");
            p.category = j.value("category", "User Presets");
            if (j.contains("tags") && j["tags"].is_array())
            {
                for (const auto& t : j["tags"])
                    p.tags.push_back(t.get<std::string>());
            }
            p.rating = j.value("rating", 0);
            if (j.contains("parameters") && j["parameters"].is_array())
            {
                for (const auto& v : j["parameters"])
                    p.parameters.push_back(v.get<float>());
            }
            p.stateData = j.value("stateData", "");
            p.isFactory = false;

            if (! p.pluginId.empty())
                m_presets.push_back(std::move(p));
        }
        catch (const std::exception&)
        {
            // Skip invalid JSON preset files
        }
    }
}

void PluginPresetManager::addPreset(PluginPreset preset)
{
    // If matching preset exists, update it
    for (auto& existing : m_presets)
    {
        if (existing.pluginId == preset.pluginId && existing.presetId == preset.presetId)
        {
            existing = std::move(preset);
            return;
        }
    }
    m_presets.push_back(std::move(preset));
}

bool PluginPresetManager::saveUserPreset(const PluginPreset& preset, std::string* errorMsg)
{
    if (preset.pluginId.empty())
    {
        if (errorMsg) *errorMsg = "Cannot save preset: pluginId is empty.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(m_userPresetDir, ec);
    if (ec)
    {
        if (errorMsg) *errorMsg = "Failed to create directory: " + ec.message();
        return false;
    }

    const std::string filename = preset.pluginId + "_" + (preset.presetId.empty() ? "preset" : preset.presetId) + ".preset";
    const auto filePath = m_userPresetDir / filename;

    Json j = {
        {"presetId", preset.presetId},
        {"pluginId", preset.pluginId},
        {"name", preset.name},
        {"author", preset.author},
        {"category", preset.category},
        {"tags", preset.tags},
        {"rating", preset.rating},
        {"parameters", preset.parameters},
        {"stateData", preset.stateData}
    };

    std::ofstream file(filePath);
    if (! file.is_open())
    {
        if (errorMsg) *errorMsg = "Failed to open file for writing: " + filePath.string();
        return false;
    }

    file << j.dump(2);
    addPreset(preset);
    return true;
}

bool PluginPresetManager::deleteUserPreset(std::string_view pluginId, std::string_view presetId)
{
    auto it = std::find_if(m_presets.begin(), m_presets.end(), [&](const PluginPreset& p) {
        return p.pluginId == pluginId && p.presetId == presetId && ! p.isFactory;
    });

    if (it == m_presets.end())
        return false;

    const std::string filename = std::string(pluginId) + "_" + std::string(presetId) + ".preset";
    const auto filePath = m_userPresetDir / filename;
    std::error_code ec;
    std::filesystem::remove(filePath, ec);

    m_presets.erase(it);
    return true;
}

std::vector<PluginPreset> PluginPresetManager::getPresetsForPlugin(std::string_view pluginId) const
{
    std::vector<PluginPreset> result;
    for (const auto& p : m_presets)
    {
        if (p.pluginId == pluginId)
            result.push_back(p);
    }
    return result;
}

const PluginPreset* PluginPresetManager::findPreset(std::string_view pluginId, std::string_view presetId) const
{
    for (const auto& p : m_presets)
    {
        if (p.pluginId == pluginId && p.presetId == presetId)
            return &p;
    }
    return nullptr;
}

std::vector<PluginPreset> PluginPresetManager::searchPresets(
    std::string_view pluginId,
    std::string_view queryText,
    const std::vector<std::string>& requiredTags,
    int minRating) const
{
    std::vector<PluginPreset> result;
    std::string queryLower(queryText);
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    for (const auto& p : m_presets)
    {
        if (! pluginId.empty() && p.pluginId != pluginId)
            continue;

        if (p.rating < minRating)
            continue;

        // Check required tags
        bool hasAllTags = true;
        for (const auto& reqTag : requiredTags)
        {
            if (std::find(p.tags.begin(), p.tags.end(), reqTag) == p.tags.end())
            {
                hasAllTags = false;
                break;
            }
        }
        if (! hasAllTags)
            continue;

        // Query text matching (name or category)
        if (! queryLower.empty())
        {
            std::string nameLower = p.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            std::string catLower = p.category;
            std::transform(catLower.begin(), catLower.end(), catLower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (nameLower.find(queryLower) == std::string::npos && catLower.find(queryLower) == std::string::npos)
                continue;
        }

        result.push_back(p);
    }

    return result;
}

bool PluginPresetManager::setPresetRating(std::string_view pluginId, std::string_view presetId, int rating)
{
    for (auto& p : m_presets)
    {
        if (p.pluginId == pluginId && p.presetId == presetId)
        {
            p.rating = std::clamp(rating, 0, 5);
            return true;
        }
    }
    return false;
}

bool PluginPresetManager::addPresetTag(std::string_view pluginId, std::string_view presetId, std::string tag)
{
    for (auto& p : m_presets)
    {
        if (p.pluginId == pluginId && p.presetId == presetId)
        {
            if (std::find(p.tags.begin(), p.tags.end(), tag) == p.tags.end())
            {
                p.tags.push_back(std::move(tag));
                return true;
            }
            return false;
        }
    }
    return false;
}

bool PluginPresetManager::removePresetTag(std::string_view pluginId, std::string_view presetId, std::string_view tag)
{
    for (auto& p : m_presets)
    {
        if (p.pluginId == pluginId && p.presetId == presetId)
        {
            auto it = std::find(p.tags.begin(), p.tags.end(), tag);
            if (it != p.tags.end())
            {
                p.tags.erase(it);
                return true;
            }
            return false;
        }
    }
    return false;
}

ABStateSlot& PluginPresetManager::getOrAllocABState(std::string_view instanceKey)
{
    return m_abStates[std::string(instanceKey)];
}

DependencyBundleReport PluginPresetManager::packageProjectDependencies(
    const std::vector<ProjectDependencyItem>& dependencies,
    const std::filesystem::path& targetBundleDirectory,
    bool overwrite)
{
    DependencyBundleReport report;
    report.bundlePath = targetBundleDirectory;

    std::error_code ec;
    std::filesystem::create_directories(targetBundleDirectory, ec);
    if (ec)
    {
        report.success = false;
        report.errorMessage = "Failed to create target bundle directory: " + ec.message();
        return report;
    }

    for (const auto& item : dependencies)
    {
        const auto sourcePath = std::filesystem::path(item.originalPath);
        if (! std::filesystem::exists(sourcePath, ec))
        {
            report.missingFiles.push_back(item.originalPath);
            continue;
        }

        const auto destPath = targetBundleDirectory / item.relativeTargetPath;
        std::filesystem::create_directories(destPath.parent_path(), ec);

        auto copyOptions = std::filesystem::copy_options::none;
        if (overwrite)
            copyOptions |= std::filesystem::copy_options::overwrite_existing;
        else
            copyOptions |= std::filesystem::copy_options::skip_existing;

        std::filesystem::copy_file(sourcePath, destPath, copyOptions, ec);
        if (! ec)
        {
            report.filesCollected++;
            report.totalBytesCopied += std::filesystem::file_size(destPath, ec);
        }
    }

    report.success = report.missingFiles.empty();
    return report;
}

} // namespace dawg::plugins
