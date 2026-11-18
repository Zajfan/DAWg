#pragma once

#include <dawg/plugins/NativePluginCatalog.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::plugins {

struct PluginPreset
{
    std::string presetId;
    std::string pluginId;
    std::string name;
    std::string author {"DAWg Studio"};
    std::string category {"General"};
    std::vector<std::string> tags;
    int rating {0}; // 1 to 5 stars, 0 = unrated
    std::vector<float> parameters;
    std::string stateData;
    bool isFactory {false};

    friend bool operator==(const PluginPreset&, const PluginPreset&) = default;
};

struct ABStateSlot
{
    enum class ActiveSlot : uint8_t { slotA, slotB };

    std::vector<float> paramsA;
    std::string stateDataA;

    std::vector<float> paramsB;
    std::string stateDataB;

    ActiveSlot activeSlot {ActiveSlot::slotA};

    void setSlotA(std::vector<float> params, std::string state = "")
    {
        paramsA = std::move(params);
        stateDataA = std::move(state);
    }

    void setSlotB(std::vector<float> params, std::string state = "")
    {
        paramsB = std::move(params);
        stateDataB = std::move(state);
    }

    void toggleSlot() noexcept
    {
        activeSlot = (activeSlot == ActiveSlot::slotA) ? ActiveSlot::slotB : ActiveSlot::slotA;
    }

    void copyAToB()
    {
        paramsB = paramsA;
        stateDataB = stateDataA;
    }

    void copyBToA()
    {
        paramsA = paramsB;
        stateDataA = stateDataB;
    }

    [[nodiscard]] const std::vector<float>& activeParams() const noexcept
    {
        return (activeSlot == ActiveSlot::slotA) ? paramsA : paramsB;
    }

    [[nodiscard]] const std::string& activeStateData() const noexcept
    {
        return (activeSlot == ActiveSlot::slotA) ? stateDataA : stateDataB;
    }
};

struct ProjectDependencyItem
{
    std::string originalPath;
    std::string relativeTargetPath;
    std::string category; // "sample", "hrtf", "ir"
    uint64_t sizeBytes {0};
    bool exists {false};
};

struct DependencyBundleReport
{
    bool success {false};
    std::filesystem::path bundlePath;
    std::size_t filesCollected {0};
    uint64_t totalBytesCopied {0};
    std::vector<std::string> missingFiles;
    std::string errorMessage;
};

class PluginPresetManager
{
public:
    PluginPresetManager();
    explicit PluginPresetManager(std::filesystem::path userPresetDirectory);
    ~PluginPresetManager() = default;

    // Factory & User Presets
    void reloadPresets();
    void addPreset(PluginPreset preset);
    bool saveUserPreset(const PluginPreset& preset, std::string* errorMsg = nullptr);
    bool deleteUserPreset(std::string_view pluginId, std::string_view presetId);

    [[nodiscard]] std::vector<PluginPreset> getPresetsForPlugin(std::string_view pluginId) const;
    [[nodiscard]] const PluginPreset* findPreset(std::string_view pluginId, std::string_view presetId) const;

    [[nodiscard]] std::vector<PluginPreset> searchPresets(
        std::string_view pluginId,
        std::string_view queryText,
        const std::vector<std::string>& requiredTags = {},
        int minRating = 0) const;

    bool setPresetRating(std::string_view pluginId, std::string_view presetId, int rating);
    bool addPresetTag(std::string_view pluginId, std::string_view presetId, std::string tag);
    bool removePresetTag(std::string_view pluginId, std::string_view presetId, std::string_view tag);

    // A/B Comparison States
    [[nodiscard]] ABStateSlot& getOrAllocABState(std::string_view instanceKey);

    // Sample Relocation & Dependency Packaging
    [[nodiscard]] static DependencyBundleReport packageProjectDependencies(
        const std::vector<ProjectDependencyItem>& dependencies,
        const std::filesystem::path& targetBundleDirectory,
        bool overwrite = false);

private:
    void initFactoryPresets();

    std::filesystem::path m_userPresetDir;
    std::vector<PluginPreset> m_presets;
    std::unordered_map<std::string, ABStateSlot> m_abStates;
};

} // namespace dawg::plugins
