#pragma once

#include <dawg/plugins/PluginScanner.h>
#include <dawg/engine/ProcessorNode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::plugins {

struct NativeParameter
{
    uint32_t id {0};
    std::string name;
    float minVal {0.0f};
    float maxVal {1.0f};
    float defaultVal {0.5f};
    std::string unit {"%"};
    bool isStepped {false};
};

struct NativePreset
{
    std::string name;
    std::vector<float> values; // Parameter values in order
};

struct NativePluginDef
{
    PluginDescriptor descriptor;
    std::vector<NativeParameter> parameters;
    std::vector<NativePreset> presets;
};

class NativePluginCatalog
{
public:
    // Returns full catalog of native instruments and effects
    [[nodiscard]] static const std::vector<NativePluginDef>& allPlugins();

    // Query plugin by ID
    [[nodiscard]] static const NativePluginDef* findPlugin(std::string_view id);

    // List of PluginDescriptors for browser integration
    [[nodiscard]] static std::vector<PluginDescriptor> getDescriptors();

    // Instantiate engine ProcessorNode
    [[nodiscard]] static std::unique_ptr<engine::ProcessorNode> createNode(std::string_view id);
};

} // namespace dawg::plugins
