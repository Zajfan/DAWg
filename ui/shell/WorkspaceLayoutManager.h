#pragma once

#include <cstdint>
#include <string>

namespace dawg::ui {

struct WorkspaceLayoutState
{
    std::string presetName {"Arrange"};
    std::uint8_t dockMode {0};          // 0 = mixer, 1 = pianoRoll, 7 = wireGrid, 8 = masteringSuite
    std::uint8_t secondaryDockMode {4}; // 4 = routingMatrix
    bool isDualDockEnabled {false};
    int dockHeight {240};
    bool isDockMaximized {false};
    bool isInspectorVisible {true};
    int inspectorWidth {210};
    std::uint8_t centerViewMode {0};    // 0 = arrangement, 1 = sessionView
    bool detachedMixer {false};
    bool detachedPianoRoll {false};
    bool detachedModular {false};
    bool detachedMastering {false};
};

class WorkspaceLayoutManager final
{
public:
    WorkspaceLayoutManager() = delete;

    [[nodiscard]] static std::string serializeToJson(const WorkspaceLayoutState& state);
    [[nodiscard]] static WorkspaceLayoutState deserializeFromJson(const std::string& json);

    static bool saveToFile(const std::string& filePath, const WorkspaceLayoutState& state);
    [[nodiscard]] static WorkspaceLayoutState loadFromFile(const std::string& filePath);

    [[nodiscard]] static WorkspaceLayoutState getPresetArrange() noexcept;
    [[nodiscard]] static WorkspaceLayoutState getPresetMix() noexcept;
    [[nodiscard]] static WorkspaceLayoutState getPresetEdit() noexcept;
    [[nodiscard]] static WorkspaceLayoutState getPresetModular() noexcept;
    [[nodiscard]] static WorkspaceLayoutState getPresetMaster() noexcept;
};

} // namespace dawg::ui
