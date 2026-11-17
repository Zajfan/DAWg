#include "WorkspaceLayoutManager.h"

#include <fstream>
#include <sstream>

namespace dawg::ui {

namespace {

std::string extractStringValue(const std::string& json, const std::string& key)
{
    const std::string search = "\"" + key + "\": \"";
    const auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    const auto start = pos + search.length();
    const auto end = json.find('"', start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

int extractIntValue(const std::string& json, const std::string& key, int defaultVal)
{
    const std::string search = "\"" + key + "\": ";
    const auto pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;
    const auto start = pos + search.length();
    auto end = json.find_first_of(",}\n", start);
    if (end == std::string::npos) end = json.length();
    try
    {
        return std::stoi(json.substr(start, end - start));
    }
    catch (...)
    {
        return defaultVal;
    }
}

bool extractBoolValue(const std::string& json, const std::string& key, bool defaultVal)
{
    const std::string search = "\"" + key + "\": ";
    const auto pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;
    const auto start = pos + search.length();
    if (json.compare(start, 4, "true") == 0) return true;
    if (json.compare(start, 5, "false") == 0) return false;
    return defaultVal;
}

} // namespace

std::string WorkspaceLayoutManager::serializeToJson(const WorkspaceLayoutState& state)
{
    std::ostringstream ss;
    ss << "{\n"
       << "  \"presetName\": \"" << state.presetName << "\",\n"
       << "  \"dockMode\": " << static_cast<int>(state.dockMode) << ",\n"
       << "  \"secondaryDockMode\": " << static_cast<int>(state.secondaryDockMode) << ",\n"
       << "  \"isDualDockEnabled\": " << (state.isDualDockEnabled ? "true" : "false") << ",\n"
       << "  \"dockHeight\": " << state.dockHeight << ",\n"
       << "  \"isDockMaximized\": " << (state.isDockMaximized ? "true" : "false") << ",\n"
       << "  \"isInspectorVisible\": " << (state.isInspectorVisible ? "true" : "false") << ",\n"
       << "  \"inspectorWidth\": " << state.inspectorWidth << ",\n"
       << "  \"centerViewMode\": " << static_cast<int>(state.centerViewMode) << ",\n"
       << "  \"detachedMixer\": " << (state.detachedMixer ? "true" : "false") << ",\n"
       << "  \"detachedPianoRoll\": " << (state.detachedPianoRoll ? "true" : "false") << ",\n"
       << "  \"detachedModular\": " << (state.detachedModular ? "true" : "false") << ",\n"
       << "  \"detachedMastering\": " << (state.detachedMastering ? "true" : "false") << "\n"
       << "}";
    return ss.str();
}

WorkspaceLayoutState WorkspaceLayoutManager::deserializeFromJson(const std::string& json)
{
    WorkspaceLayoutState state;
    const std::string name = extractStringValue(json, "presetName");
    if (! name.empty()) state.presetName = name;

    state.dockMode = static_cast<std::uint8_t>(extractIntValue(json, "dockMode", state.dockMode));
    state.secondaryDockMode = static_cast<std::uint8_t>(extractIntValue(json, "secondaryDockMode", state.secondaryDockMode));
    state.isDualDockEnabled = extractBoolValue(json, "isDualDockEnabled", state.isDualDockEnabled);
    state.dockHeight = extractIntValue(json, "dockHeight", state.dockHeight);
    state.isDockMaximized = extractBoolValue(json, "isDockMaximized", state.isDockMaximized);
    state.isInspectorVisible = extractBoolValue(json, "isInspectorVisible", state.isInspectorVisible);
    state.inspectorWidth = extractIntValue(json, "inspectorWidth", state.inspectorWidth);
    state.centerViewMode = static_cast<std::uint8_t>(extractIntValue(json, "centerViewMode", state.centerViewMode));
    state.detachedMixer = extractBoolValue(json, "detachedMixer", state.detachedMixer);
    state.detachedPianoRoll = extractBoolValue(json, "detachedPianoRoll", state.detachedPianoRoll);
    state.detachedModular = extractBoolValue(json, "detachedModular", state.detachedModular);
    state.detachedMastering = extractBoolValue(json, "detachedMastering", state.detachedMastering);
    return state;
}

bool WorkspaceLayoutManager::saveToFile(const std::string& filePath, const WorkspaceLayoutState& state)
{
    std::ofstream out(filePath);
    if (! out.is_open()) return false;
    out << serializeToJson(state);
    return true;
}

WorkspaceLayoutState WorkspaceLayoutManager::loadFromFile(const std::string& filePath)
{
    std::ifstream in(filePath);
    if (! in.is_open()) return getPresetArrange();
    std::stringstream buffer;
    buffer << in.rdbuf();
    return deserializeFromJson(buffer.str());
}

WorkspaceLayoutState WorkspaceLayoutManager::getPresetArrange() noexcept
{
    WorkspaceLayoutState s;
    s.presetName = "Arrange";
    s.dockMode = 0; // mixer
    s.dockHeight = 160;
    s.isDockMaximized = false;
    s.isInspectorVisible = true;
    s.inspectorWidth = 220;
    s.centerViewMode = 0; // arrangement
    return s;
}

WorkspaceLayoutState WorkspaceLayoutManager::getPresetMix() noexcept
{
    WorkspaceLayoutState s;
    s.presetName = "Mix";
    s.dockMode = 0; // mixer
    s.secondaryDockMode = 4; // routingMatrix
    s.isDualDockEnabled = false;
    s.dockHeight = 420;
    s.isDockMaximized = false;
    s.isInspectorVisible = true;
    s.inspectorWidth = 220;
    s.centerViewMode = 0;
    return s;
}

WorkspaceLayoutState WorkspaceLayoutManager::getPresetEdit() noexcept
{
    WorkspaceLayoutState s;
    s.presetName = "Edit";
    s.dockMode = 1; // pianoRoll
    s.dockHeight = 380;
    s.isDockMaximized = false;
    s.isInspectorVisible = true;
    s.inspectorWidth = 210;
    s.centerViewMode = 0;
    return s;
}

WorkspaceLayoutState WorkspaceLayoutManager::getPresetModular() noexcept
{
    WorkspaceLayoutState s;
    s.presetName = "Modular";
    s.dockMode = 7; // wireGrid
    s.dockHeight = 440;
    s.isDockMaximized = true;
    s.isInspectorVisible = false;
    s.centerViewMode = 0;
    return s;
}

WorkspaceLayoutState WorkspaceLayoutManager::getPresetMaster() noexcept
{
    WorkspaceLayoutState s;
    s.presetName = "Master";
    s.dockMode = 8; // masteringSuite
    s.dockHeight = 400;
    s.isDockMaximized = false;
    s.isInspectorVisible = false;
    s.centerViewMode = 0;
    return s;
}

} // namespace dawg::ui
