#pragma once

#include "EditorViewModel.h"

#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace dawg::ui {

class MidiLearnableSlider final : public juce::Slider
{
public:
    std::function<void(const juce::MouseEvent&)> onContextMenu;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu)
        {
            onContextMenu(e);
            return;
        }
        juce::Slider::mouseDown(e);
    }
};

// Collapsible Left Track Inspector (Logic Pro / Cubase / Studio One style)
// Displays full channel strip parameters, I/O routing, monitoring, record arm,
// peak/RMS meter fader, pan, and vertical DSP insert slots for the selected track.
class TrackInspectorView final : public juce::Component
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using OpenDspEditorCallback = std::function<void(project::TrackId trackId, std::size_t slotIdx)>;

    TrackInspectorView(CommandHandler commandHandler, OpenDspEditorCallback openDspEditorCb = nullptr);
    ~TrackInspectorView() override = default;

    void setViewModel(const EditorViewModel& model);
    void setSelectedTrackId(std::optional<project::TrackId> trackId);
    [[nodiscard]] std::optional<project::TrackId> selectedTrackId() const noexcept { return m_selectedTrackId; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void updateFromSelectedTrack();
    void showAddDspMenu();

    CommandHandler m_commandHandler;
    OpenDspEditorCallback m_openDspEditorCb;
    EditorViewModel m_model;
    std::optional<project::TrackId> m_selectedTrackId;

    // Header & Info
    juce::Label m_lblTrackName;
    juce::Label m_lblTrackKind;
    juce::Colour m_trackColor {juce::Colour::fromRGB(45, 140, 240)};

    // Input & Monitoring
    juce::ComboBox m_inputSelector;
    juce::TextButton m_btnMonitor {"Mon: Auto"};
    std::uint8_t m_monitorMode {1}; // 0 = Off, 1 = Auto, 2 = In

    // Track Controls
    juce::TextButton m_btnArm {"REC"};
    juce::TextButton m_btnSolo {"S"};
    juce::TextButton m_btnMute {"M"};
    juce::TextButton m_btnPolarity {"Ø"};

    // Volume & Meter
    MidiLearnableSlider m_faderVolume;
    juce::Label m_lblVolumeDb;
    float m_meterLevelDb {-60.0f};

    // Pan
    MidiLearnableSlider m_knobPan;
    juce::Label m_lblPan;

    // Output Destination
    juce::ComboBox m_outputSelector;

    // DSP Inserts Section
    juce::Label m_lblDspHeader {"INSERTS / FX"};
    juce::TextButton m_btnAddDsp {"+ FX"};
    struct DspSlotRow
    {
        std::unique_ptr<juce::TextButton> btnName;
        std::unique_ptr<juce::TextButton> btnBypass;
    };
    std::vector<DspSlotRow> m_dspRows;
    juce::Component m_dspContainer;

    bool m_updatingControls {false};
};

} // namespace dawg::ui
