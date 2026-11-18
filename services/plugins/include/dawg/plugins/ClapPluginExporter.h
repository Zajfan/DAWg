#pragma once

#include <dawg/engine/clap/clap.h>
#include <dawg/plugins/NativePluginCatalog.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::plugins {

struct ExportedClapInfo
{
    std::string exportId;
    std::string nativeCatalogId;
    std::string name;
    std::string category;
    bool isInstrument {false};
};

class ClapPluginExporter
{
public:
    // Returns C ABI clap_plugin_entry_t
    [[nodiscard]] static const clap_plugin_entry* getClapEntry() noexcept;

    // Export catalog
    [[nodiscard]] static const std::vector<ExportedClapInfo>& getExportedPlugins() noexcept;
    [[nodiscard]] static uint32_t getExportedCount() noexcept;
    [[nodiscard]] static const clap_plugin_descriptor* getExportedDescriptor(uint32_t index) noexcept;
    [[nodiscard]] static const clap_plugin_descriptor* findDescriptor(const char* pluginId) noexcept;

    // Factory instance creation
    [[nodiscard]] static const clap_plugin* createPlugin(const clap_host* host, const char* pluginId) noexcept;

    // Export filesystem bundle generator (.clap bundle layout)
    static bool exportClapBundle(
        std::string_view exportPluginId,
        const std::filesystem::path& destinationDir,
        std::string* errorMsg = nullptr);
};

} // namespace dawg::plugins
