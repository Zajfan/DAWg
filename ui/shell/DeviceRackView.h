#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dawg::ui {

// ============================================================================
// DeviceCardComponent - Single horizontal device panel in the chain
// ============================================================================
class DeviceCardComponent final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using OpenEditorCallback = std::function<void(project::DspNodeType type, const std::string& name)>;

    DeviceCardComponent(
        project::TrackId trackId,
        std::uint32_t slotIndex,
        project::DspNodeDescriptor descriptor,
        CommandHandler cmdHandler,
        OpenEditorCallback openEditorCb);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    project::TrackId m_trackId {0};
    std::uint32_t m_slotIndex {0};
    project::DspNodeDescriptor m_descriptor;
    CommandHandler m_cmdHandler;
    OpenEditorCallback m_openEditorCb;

    juce::TextButton m_btnBypass;
    juce::Label m_lblTitle;
    juce::TextButton m_btnEdit {"Edit"};
    juce::TextButton m_btnRemove {"X"};

    struct QuickParam
    {
        juce::Label label;
        juce::Slider slider;
    };
    std::vector<std::unique_ptr<QuickParam>> m_params;
};

// ============================================================================
// DeviceRackView - Bottom-docked horizontal device chain (Ableton / Bitwig style)
// ============================================================================
class DeviceRackView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;

    explicit DeviceRackView(CommandHandler cmdHandler = {});
    ~DeviceRackView() override = default;

    void setViewModel(const EditorViewModel& model);
    void setSelectedTrackId(std::optional<project::TrackId> trackId);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildRack();
    void showAddDeviceMenu();

    CommandHandler m_cmdHandler;
    EditorViewModel m_model;
    std::optional<project::TrackId> m_selectedTrackId;

    juce::Label m_lblTrackTitle;
    juce::TextButton m_btnAddDevice {"+ Add Device"};

    juce::Viewport m_viewport;
    juce::Component m_rackContainer;
    std::vector<std::unique_ptr<DeviceCardComponent>> m_cards;
    juce::Label m_emptyLabel;
};

} // namespace dawg::ui
