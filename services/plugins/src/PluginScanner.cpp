#include <dawg/plugins/PluginScanner.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace dawg::plugins {
namespace {

std::string toLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

PluginCategory categorizeFromName(const std::string& name, bool& outIsInstrument)
{
    const auto lower = toLower(name);
    if (lower.find("synth") != std::string::npos || lower.find("instrument") != std::string::npos
        || lower.find("piano") != std::string::npos || lower.find("organ") != std::string::npos
        || lower.find("drum") != std::string::npos || lower.find("sampler") != std::string::npos)
    {
        outIsInstrument = true;
        return PluginCategory::instrument;
    }
    outIsInstrument = false;
    if (lower.find("eq") != std::string::npos || lower.find("filter") != std::string::npos
        || lower.find("equalizer") != std::string::npos)
    {
        return PluginCategory::eq;
    }
    if (lower.find("comp") != std::string::npos || lower.find("limit") != std::string::npos
        || lower.find("dynamics") != std::string::npos || lower.find("gate") != std::string::npos)
    {
        return PluginCategory::dynamics;
    }
    if (lower.find("verb") != std::string::npos || lower.find("reverb") != std::string::npos
        || lower.find("delay") != std::string::npos || lower.find("echo") != std::string::npos
        || lower.find("space") != std::string::npos)
    {
        return PluginCategory::spatial;
    }
    if (lower.find("meter") != std::string::npos || lower.find("analyzer") != std::string::npos
        || lower.find("scope") != std::string::npos)
    {
        return PluginCategory::analysis;
    }
    return PluginCategory::utility;
}

} // namespace

std::vector<PluginDescriptor> PluginScanner::getBuiltInPlugins()
{
    return {
        {
            .id = "tnhc.parametric_eq",
            .name = "Tnhc 4-Band Parametric EQ",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::eq,
            .isInstrument = false,
            .isSandboxed = false
        },
        {
            .id = "tnhc.studio_compressor",
            .name = "Tnhc Studio Dynamics Compressor",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::dynamics,
            .isInstrument = false,
            .isSandboxed = false
        },
        {
            .id = "tnhc.master_limiter",
            .name = "Tnhc Brickwall Master Limiter",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::dynamics,
            .isInstrument = false,
            .isSandboxed = false
        },
        {
            .id = "tnhc.synth",
            .name = "Tnhc Polyphonic Subtractive Synth",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::instrument,
            .isInstrument = true,
            .isSandboxed = false
        },
        {
            .id = "tnhc.plate_reverb",
            .name = "Tnhc Algorithmic Plate Reverb",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::spatial,
            .isInstrument = false,
            .isSandboxed = false
        },
        {
            .id = "tnhc.stereo_delay",
            .name = "Tnhc Stereo Delay",
            .vendor = "The No-hands Company",
            .version = "1.0.0",
            .path = "",
            .format = PluginFormat::native,
            .category = PluginCategory::spatial,
            .isInstrument = false,
            .isSandboxed = false
        }
    };
}

std::vector<std::filesystem::path> PluginScanner::parseEnvPath(const char* envVar)
{
    std::vector<std::filesystem::path> result;
    const char* val = std::getenv(envVar);
    if (val == nullptr || std::strlen(val) == 0)
        return result;

    std::stringstream ss(val);
    std::string item;
    while (std::getline(ss, item, ':'))
    {
        if (! item.empty())
            result.emplace_back(item);
    }
    return result;
}

std::vector<std::filesystem::path> PluginScanner::defaultClapPaths()
{
    std::vector<std::filesystem::path> paths;
    const auto envPaths = parseEnvPath("CLAP_PATH");
    paths.insert(paths.end(), envPaths.begin(), envPaths.end());

    const char* home = std::getenv("HOME");
    if (home != nullptr)
    {
        paths.emplace_back(std::filesystem::path(home) / ".clap");
    }
    paths.emplace_back("/usr/lib/clap");
    paths.emplace_back("/usr/local/lib/clap");
    return paths;
}

std::vector<std::filesystem::path> PluginScanner::defaultVst3Paths()
{
    std::vector<std::filesystem::path> paths;
    const auto envPaths = parseEnvPath("VST3_PATH");
    paths.insert(paths.end(), envPaths.begin(), envPaths.end());

    const char* home = std::getenv("HOME");
    if (home != nullptr)
    {
        paths.emplace_back(std::filesystem::path(home) / ".vst3");
    }
    paths.emplace_back("/usr/lib/vst3");
    paths.emplace_back("/usr/local/lib/vst3");
    return paths;
}

std::vector<PluginDescriptor> PluginScanner::scanClapDirectories(
    const std::vector<std::filesystem::path>& directories)
{
    std::vector<PluginDescriptor> found;

    for (const auto& dir : directories)
    {
        std::error_code ec;
        if (! std::filesystem::exists(dir, ec) || ! std::filesystem::is_directory(dir, ec))
            continue;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (ec)
                break;

            const auto& p = entry.path();
            if (p.extension() == ".clap")
            {
                const auto stem = p.stem().string();
                bool isInst = false;
                const auto category = categorizeFromName(stem, isInst);

                found.push_back(PluginDescriptor {
                    .id = "clap." + stem,
                    .name = stem,
                    .vendor = "Third-Party CLAP",
                    .version = "1.0.0",
                    .path = p.string(),
                    .format = PluginFormat::clap,
                    .category = category,
                    .isInstrument = isInst,
                    .isSandboxed = true
                });
            }
        }
    }

    return found;
}

std::vector<PluginDescriptor> PluginScanner::scanVst3Directories(
    const std::vector<std::filesystem::path>& directories)
{
    std::vector<PluginDescriptor> found;

    for (const auto& dir : directories)
    {
        std::error_code ec;
        if (! std::filesystem::exists(dir, ec) || ! std::filesystem::is_directory(dir, ec))
            continue;

        for (const auto& entry : std::filesystem::directory_iterator(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (ec)
                break;

            const auto& p = entry.path();
            if (p.extension() == ".vst3")
            {
                const auto stem = p.stem().string();
                bool isInst = false;
                const auto category = categorizeFromName(stem, isInst);

                found.push_back(PluginDescriptor {
                    .id = "vst3." + stem,
                    .name = stem,
                    .vendor = "Third-Party VST3",
                    .version = "1.0.0",
                    .path = p.string(),
                    .format = PluginFormat::vst3,
                    .category = category,
                    .isInstrument = isInst,
                    .isSandboxed = true
                });
            }
        }
    }

    return found;
}

std::vector<PluginDescriptor> PluginScanner::scanAll(
    const std::vector<std::filesystem::path>& customClapDirs,
    const std::vector<std::filesystem::path>& customVst3Dirs)
{
    auto all = getBuiltInPlugins();

    auto clapDirs = defaultClapPaths();
    clapDirs.insert(clapDirs.end(), customClapDirs.begin(), customClapDirs.end());
    const auto claps = scanClapDirectories(clapDirs);
    all.insert(all.end(), claps.begin(), claps.end());

    auto vst3Dirs = defaultVst3Paths();
    vst3Dirs.insert(vst3Dirs.end(), customVst3Dirs.begin(), customVst3Dirs.end());
    const auto vst3s = scanVst3Directories(vst3Dirs);
    all.insert(all.end(), vst3s.begin(), vst3s.end());

    return all;
}

} // namespace dawg::plugins
