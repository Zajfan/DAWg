#include <dawg/commands/TrackMacroManager.h>

namespace dawg::commands {

void TrackMacroManager::setMacroValue(project::TrackId trackId, std::size_t macroIndex, float value)
{
    if (macroIndex >= NumMacrosPerTrack)
        return;

    m_trackBanks[trackId].macros[macroIndex].normalizedValue = std::clamp(value, 0.0f, 1.0f);
}

float TrackMacroManager::getMacroValue(project::TrackId trackId, std::size_t macroIndex) const
{
    if (macroIndex >= NumMacrosPerTrack)
        return 0.5f;

    auto it = m_trackBanks.find(trackId);
    if (it == m_trackBanks.end())
        return 0.5f;

    return it->second.macros[macroIndex].normalizedValue;
}

void TrackMacroManager::setMacroName(project::TrackId trackId, std::size_t macroIndex, std::string name)
{
    if (macroIndex >= NumMacrosPerTrack)
        return;

    m_trackBanks[trackId].macros[macroIndex].name = std::move(name);
}

std::string TrackMacroManager::getMacroName(project::TrackId trackId, std::size_t macroIndex) const
{
    if (macroIndex >= NumMacrosPerTrack)
        return "Macro";

    auto it = m_trackBanks.find(trackId);
    if (it == m_trackBanks.end())
        return "Macro " + std::to_string(macroIndex + 1);

    return it->second.macros[macroIndex].name;
}

void TrackMacroManager::addTargetAssignment(project::TrackId trackId, std::size_t macroIndex, MacroTargetAssignment assignment)
{
    if (macroIndex >= NumMacrosPerTrack)
        return;

    auto& assignments = m_trackBanks[trackId].macros[macroIndex].assignments;
    // Check if target already assigned, update if so
    for (auto& a : assignments)
    {
        if (a.trackId == assignment.trackId && a.slotIndex == assignment.slotIndex && a.parameterIndex == assignment.parameterIndex)
        {
            a = std::move(assignment);
            return;
        }
    }
    assignments.push_back(std::move(assignment));
}

void TrackMacroManager::clearTargetAssignments(project::TrackId trackId, std::size_t macroIndex)
{
    if (macroIndex >= NumMacrosPerTrack)
        return;

    auto it = m_trackBanks.find(trackId);
    if (it != m_trackBanks.end())
        it->second.macros[macroIndex].assignments.clear();
}

const std::vector<MacroTargetAssignment>& TrackMacroManager::getAssignments(project::TrackId trackId, std::size_t macroIndex) const
{
    static const std::vector<MacroTargetAssignment> empty;
    if (macroIndex >= NumMacrosPerTrack)
        return empty;

    auto it = m_trackBanks.find(trackId);
    if (it == m_trackBanks.end())
        return empty;

    return it->second.macros[macroIndex].assignments;
}

void TrackMacroManager::addModulationRoute(PluginModulationRoute route)
{
    for (auto& r : m_modRoutes)
    {
        if (r.source == route.source && r.trackId == route.trackId &&
            r.slotIndex == route.slotIndex && r.parameterIndex == route.parameterIndex)
        {
            r.depth = route.depth;
            return;
        }
    }
    m_modRoutes.push_back(std::move(route));
}

void TrackMacroManager::removeModulationRoutes(project::TrackId trackId, std::uint32_t slotIndex, std::uint32_t parameterIndex)
{
    m_modRoutes.erase(
        std::remove_if(m_modRoutes.begin(), m_modRoutes.end(), [&](const PluginModulationRoute& r) {
            return r.trackId == trackId && r.slotIndex == slotIndex && r.parameterIndex == parameterIndex;
        }),
        m_modRoutes.end()
    );
}

std::vector<PluginModulationRoute> TrackMacroManager::getRoutesForParameter(
    project::TrackId trackId, std::uint32_t slotIndex, std::uint32_t parameterIndex) const
{
    std::vector<PluginModulationRoute> result;
    for (const auto& r : m_modRoutes)
    {
        if (r.trackId == trackId && r.slotIndex == slotIndex && r.parameterIndex == parameterIndex)
            result.push_back(r);
    }
    return result;
}

float TrackMacroManager::calculateModulatedValue(
    project::TrackId trackId,
    std::uint32_t slotIndex,
    std::uint32_t parameterIndex,
    float baseValue,
    const engine::dsp::MacroModulationMatrix& modMatrix) const
{
    float val = baseValue;

    // 1. Apply Macro assignments targeting this parameter
    auto bankIt = m_trackBanks.find(trackId);
    if (bankIt != m_trackBanks.end())
    {
        for (const auto& macro : bankIt->second.macros)
        {
            for (const auto& assign : macro.assignments)
            {
                if (assign.slotIndex == slotIndex && assign.parameterIndex == parameterIndex)
                {
                    const float norm = std::pow(macro.normalizedValue, std::max(0.01f, assign.curve));
                    const float mapped = assign.minVal + norm * (assign.maxVal - assign.minVal);
                    val = mapped;
                }
            }
        }
    }

    // 2. Apply modulation routes (LFO, MSEG, follower, chaos)
    for (const auto& route : m_modRoutes)
    {
        if (route.trackId == trackId && route.slotIndex == slotIndex && route.parameterIndex == parameterIndex)
        {
            const float srcVal = modMatrix.getSourceValue(route.source);
            val += route.depth * srcVal;
        }
    }

    return val;
}

engine::dsp::ModulationRingState TrackMacroManager::calculateModulationRing(
    project::TrackId trackId,
    std::uint32_t slotIndex,
    std::uint32_t parameterIndex,
    float baseValue,
    const engine::dsp::MacroModulationMatrix& modMatrix) const
{
    engine::dsp::ModulationRingState ring;
    ring.baseValue = baseValue;
    ring.currentValue = calculateModulatedValue(trackId, slotIndex, parameterIndex, baseValue, modMatrix);

    float totalPositiveMod = 0.0f;
    float totalNegativeMod = 0.0f;

    for (const auto& route : m_modRoutes)
    {
        if (route.trackId == trackId && route.slotIndex == slotIndex && route.parameterIndex == parameterIndex)
        {
            if (route.depth >= 0.0f)
                totalPositiveMod += route.depth;
            else
                totalNegativeMod += std::abs(route.depth);
        }
    }

    ring.arcMin = std::max(0.0f, baseValue - totalNegativeMod);
    ring.arcMax = std::min(1.0f, baseValue + totalPositiveMod);
    return ring;
}

} // namespace dawg::commands
