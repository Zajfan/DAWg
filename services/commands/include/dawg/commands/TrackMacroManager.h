#pragma once

#include <dawg/engine/dsp/MacroModulationMatrix.h>
#include <dawg/project/ProjectTypes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dawg::commands {

struct MacroTargetAssignment
{
    project::TrackId trackId {0};
    std::uint32_t slotIndex {0};
    std::uint32_t parameterIndex {0};
    std::string targetParamName;
    float minVal {0.0f};
    float maxVal {1.0f};
    float curve {1.0f}; // 1.0 = linear, 2.0 = exponential, 0.5 = logarithmic
};

struct TrackMacroDef
{
    std::string name {"Macro"};
    float normalizedValue {0.0f}; // 0.0 to 1.0
    std::vector<MacroTargetAssignment> assignments;
};

struct PluginModulationRoute
{
    engine::dsp::ModSource source {engine::dsp::ModSource::macro1};
    project::TrackId trackId {0};
    std::uint32_t slotIndex {0};
    std::uint32_t parameterIndex {0};
    float depth {0.0f}; // -1.0 to +1.0
};

class TrackMacroManager
{
public:
    static constexpr std::size_t NumMacrosPerTrack = 8;

    TrackMacroManager() = default;
    ~TrackMacroManager() = default;

    // Track Macro Controls (Macros 1-8)
    void setMacroValue(project::TrackId trackId, std::size_t macroIndex, float value);
    [[nodiscard]] float getMacroValue(project::TrackId trackId, std::size_t macroIndex) const;

    void setMacroName(project::TrackId trackId, std::size_t macroIndex, std::string name);
    [[nodiscard]] std::string getMacroName(project::TrackId trackId, std::size_t macroIndex) const;

    // Assignment of Macro -> Plugin Parameter
    void addTargetAssignment(project::TrackId trackId, std::size_t macroIndex, MacroTargetAssignment assignment);
    void clearTargetAssignments(project::TrackId trackId, std::size_t macroIndex);
    [[nodiscard]] const std::vector<MacroTargetAssignment>& getAssignments(project::TrackId trackId, std::size_t macroIndex) const;

    // Modulation Routing (LFOs, MSEG, Envelope Follower, Lorenz Chaos -> Plugin Parameters)
    void addModulationRoute(PluginModulationRoute route);
    void removeModulationRoutes(project::TrackId trackId, std::uint32_t slotIndex, std::uint32_t parameterIndex);
    [[nodiscard]] std::vector<PluginModulationRoute> getRoutesForParameter(
        project::TrackId trackId, std::uint32_t slotIndex, std::uint32_t parameterIndex) const;

    // Calculate parameter modulation values and visual rings
    [[nodiscard]] float calculateModulatedValue(
        project::TrackId trackId,
        std::uint32_t slotIndex,
        std::uint32_t parameterIndex,
        float baseValue,
        const engine::dsp::MacroModulationMatrix& modMatrix) const;

    [[nodiscard]] engine::dsp::ModulationRingState calculateModulationRing(
        project::TrackId trackId,
        std::uint32_t slotIndex,
        std::uint32_t parameterIndex,
        float baseValue,
        const engine::dsp::MacroModulationMatrix& modMatrix) const;

private:
    struct TrackMacroBank
    {
        std::array<TrackMacroDef, NumMacrosPerTrack> macros;
        TrackMacroBank()
        {
            for (std::size_t i = 0; i < NumMacrosPerTrack; ++i)
            {
                macros[i].name = "Macro " + std::to_string(i + 1);
                macros[i].normalizedValue = 0.5f;
            }
        }
    };

    std::unordered_map<project::TrackId, TrackMacroBank> m_trackBanks;
    std::vector<PluginModulationRoute> m_modRoutes;
};

} // namespace dawg::commands
