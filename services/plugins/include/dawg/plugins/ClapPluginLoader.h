#pragma once

#include <dawg/engine/clap/clap.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dawg::plugins {

struct ClapParamDescriptor
{
    std::uint32_t id {0};
    std::string name;
    std::string module;
    double minValue {0.0};
    double maxValue {1.0};
    double defaultValue {0.0};
    double currentValue {0.0};
};

class ClapInstance
{
public:
    ClapInstance(void* sharedLibraryHandle,
                 const clap_plugin_entry* entry,
                 const clap_plugin* plugin) noexcept;
    ~ClapInstance();

    ClapInstance(const ClapInstance&) = delete;
    ClapInstance& operator=(const ClapInstance&) = delete;

    ClapInstance(ClapInstance&& other) noexcept;
    ClapInstance& operator=(ClapInstance&& other) noexcept;

    [[nodiscard]] const clap_plugin* plugin() const noexcept { return m_plugin; }
    [[nodiscard]] bool isValid() const noexcept { return m_plugin != nullptr; }

    [[nodiscard]] std::string pluginName() const;
    [[nodiscard]] std::string pluginVendor() const;
    [[nodiscard]] std::string pluginVersion() const;
    [[nodiscard]] std::string pluginId() const;

    [[nodiscard]] std::vector<ClapParamDescriptor> getParameters() const;
    [[nodiscard]] bool getParameterValue(std::uint32_t paramId, double& outValue) const;
    bool setParameterValue(std::uint32_t paramId, double value);

    [[nodiscard]] bool isCrashed() const noexcept { return m_crashed; }
    void markCrashed() noexcept { m_crashed = true; }
    bool restart(double sampleRate = 48000.0, std::size_t maxBlockSize = 512);

private:
    void* m_libHandle {nullptr};
    const clap_plugin_entry* m_entry {nullptr};
    const clap_plugin* m_plugin {nullptr};
    const clap_plugin_params* m_paramsExt {nullptr};
    bool m_crashed {false};
};

struct ClapLoadResult
{
    std::unique_ptr<ClapInstance> instance;
    std::string errorMessage;
    [[nodiscard]] bool ok() const noexcept { return instance != nullptr; }
};

class ClapPluginLoader
{
public:
    [[nodiscard]] static ClapLoadResult
    loadPlugin(const std::string& libraryPath, const std::string& pluginId = "");
};

} // namespace dawg::plugins
