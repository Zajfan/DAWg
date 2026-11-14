#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dawg::plugins {

enum class PluginFormat : std::uint8_t
{
    native,
    clap,
    vst3
};

enum class PluginCategory : std::uint8_t
{
    instrument,
    eq,
    dynamics,
    spatial,
    analysis,
    utility
};

struct PluginDescriptor
{
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::string path;
    PluginFormat format {PluginFormat::native};
    PluginCategory category {PluginCategory::utility};
    bool isInstrument {false};
    bool isSandboxed {true};

    friend bool operator==(const PluginDescriptor&, const PluginDescriptor&) = default;
};

class PluginScanner final
{
public:
    [[nodiscard]] static std::vector<PluginDescriptor> scanAll(
        const std::vector<std::filesystem::path>& customClapDirs = {},
        const std::vector<std::filesystem::path>& customVst3Dirs = {});

    [[nodiscard]] static std::vector<PluginDescriptor> getBuiltInPlugins();
    [[nodiscard]] static std::vector<PluginDescriptor> scanClapDirectories(
        const std::vector<std::filesystem::path>& directories);
    [[nodiscard]] static std::vector<PluginDescriptor> scanVst3Directories(
        const std::vector<std::filesystem::path>& directories);

    [[nodiscard]] static std::vector<std::filesystem::path> defaultClapPaths();
    [[nodiscard]] static std::vector<std::filesystem::path> defaultVst3Paths();

private:
    [[nodiscard]] static std::vector<std::filesystem::path> parseEnvPath(const char* envVar);
};

} // namespace dawg::plugins
