#pragma once

#include "ChannelRackView.h"
#include "CommandPaletteOverlay.h"
#include "DeviceRackView.h"
#include "DragSplitterBar.h"
#include "DrumPadSamplerView.h"
#include "DspWireGridView.h"
#include "EditorViewModel.h"
#include "FloatingPanelWindow.h"
#include "MasteringSuiteView.h"
#include "MixerPanel.h"
#include "ModulationRackView.h"
#include "PianoRollView.h"
#include "PitchBlobCompingView.h"
#include "PluginBrowserPanel.h"
#include "RoutingMatrixView.h"
#include "SampleBrowserPanel.h"
#include "SampleZoneEditorView.h"
#include "SessionMatrixView.h"
#include "SpatialRadarView.h"
#include "TimelineView.h"
#include "TrackInspectorView.h"
#include "TransportBar.h"

#include <dawg/media/WaveformCache.h>
#include <dawg/project/ProjectCommand.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawg::ui {

class MainEditorView final : public juce::Component, public juce::MenuBarModel
{
public:
    using CommandHandler = std::function<void(project::ProjectCommand)>;
    using ActionHandler = std::function<void()>;
    using SeekCallback = std::function<void(std::int64_t samplePosition)>;
    using LimiterCallback = std::function<void(bool enabled)>;
    using AuditionCallback = std::function<void(std::uint8_t pitch, std::uint8_t velocity, bool isNoteOn)>;
    using SampleImportCallback = std::function<void(const std::filesystem::path& path)>;

    struct TransportCallbacks
    {
        ActionHandler importWav;
        ActionHandler play;
        ActionHandler stop;
        ActionHandler returnToStart;
        ActionHandler record;
        ActionHandler undo;
        ActionHandler redo;
        SeekCallback seek;
        LimiterCallback setMasterLimiter;
        AuditionCallback audition;
        SampleImportCallback importSample;
    };

    struct MenuCallbacks
    {
        ActionHandler newProject;
        std::function<void(const std::string& templateName)> newFromTemplate;
        ActionHandler openProject;
        ActionHandler saveProject;
        ActionHandler saveProjectAs;
        ActionHandler exportBounce;
        ActionHandler exportStems;
        ActionHandler showAudioPool;
        ActionHandler consolidateProject;
        ActionHandler rescanPlugins;
        ActionHandler audioPreferences;
        ActionHandler showShortcuts;
        ActionHandler showAbout;
    };

    enum class BottomDockMode : std::uint8_t
    {
        mixer,
        deviceChain,
        pianoRoll,
        channelRack,
        drumSampler,
        routingMatrix,
        modulationRack,
        spatialRadar,
        wireGrid,
        masteringSuite,
        pitchComping,
        sampleZones
    };

    enum class DetachablePanel : std::uint8_t
    {
        mixer,
        deviceChain,
        pianoRoll,
        channelRack,
        sessionMatrix,
        drumSampler,
        routingMatrix,
        modulationRack,
        spatialRadar,
        wireGrid,
        masteringSuite,
        pitchComping,
        sampleZones,
        browser
    };

    enum class WorkspacePreset : std::uint8_t
    {
        songArrangement,
        patternLabBeatmaker,
        recordingComping,
        mixingConsole,
        soundDesignModular,
        masteringSuite,
        livePerformanceRig,
        dualScreenStudio,

        // Backward compatibility aliases
        defaultArrangement = songArrangement,
        beatmakerFocus = patternLabBeatmaker
    };

    enum class CenterViewMode : std::uint8_t
    {
        arrangement,
        sessionView
    };

    enum class SidebarTab : std::uint8_t
    {
        plugins,
        samples
    };

    MainEditorView(EditorViewModel model,
                   CommandHandler commandHandler,
                   TransportCallbacks transportCallbacks = {},
                   MenuCallbacks menuCallbacks = {},
                   media::WaveformCache* waveformCache = nullptr);
    ~MainEditorView() override;

    void setViewModel(EditorViewModel model);
    void setSnapshot(project::ProjectSnapshot snapshot);
    void setWaveformCache(media::WaveformCache* cache);
    void setDockMode(BottomDockMode mode);
    [[nodiscard]] BottomDockMode dockMode() const noexcept { return m_dockMode; }
    [[nodiscard]] PianoRollView& pianoRoll() noexcept { return m_pianoRollView; }
    [[nodiscard]] const PianoRollView& pianoRoll() const noexcept { return m_pianoRollView; }
    [[nodiscard]] TimelineView& timeline() noexcept { return m_timelineView; }
    [[nodiscard]] const TimelineView& timeline() const noexcept { return m_timelineView; }
    void loadVocalStem(const float* samples, std::size_t sampleCount, double sampleRate);
    void toggleCenterViewMode();
    void setCenterViewMode(CenterViewMode mode);
    [[nodiscard]] CenterViewMode centerViewMode() const noexcept { return m_centerViewMode; }
    void togglePluginBrowser();
    [[nodiscard]] bool isPluginBrowserVisible() const noexcept { return m_browserVisible; }
    void setSidebarTab(SidebarTab tab);
    [[nodiscard]] SidebarTab sidebarTab() const noexcept { return m_sidebarTab; }

    // Multi-Window Detach / Re-Dock (Frontier 1)
    void detachPanel(DetachablePanel panel);
    void reDockPanel(DetachablePanel panel);
    void reDockAllPanels();
    [[nodiscard]] bool isPanelDetached(DetachablePanel panel) const noexcept;

    // Fluid Resizing & Splitters (Frontier 2)
    void toggleMaximizeDock();
    [[nodiscard]] bool isDockMaximized() const noexcept { return m_dockMaximized; }

    // Left Track Inspector
    void toggleTrackInspector();
    [[nodiscard]] bool isTrackInspectorVisible() const noexcept { return m_inspectorVisible; }

    // Command Palette (Ctrl+K / Cmd+K)
    void openCommandPalette();

    // Dual-Pane Dock Support
    void toggleDualDock();
    [[nodiscard]] bool isDualDockEnabled() const noexcept { return m_dualDockEnabled; }
    void setSecondaryDockMode(BottomDockMode mode);
    [[nodiscard]] BottomDockMode secondaryDockMode() const noexcept { return m_secondaryDockMode; }

    // Workspace Presets (Frontier 3)
    void applyWorkspacePreset(WorkspacePreset preset);
    void saveCurrentWorkspace();

    // HiDPI Scaling & Zoom (Frontier 4)
    void setCanvasScale(float scaleFactor);
    void zoomIn();
    void zoomOut();
    void resetZoom();
    [[nodiscard]] float canvasScale() const noexcept { return m_canvasScale; }

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void layoutDockPanel(BottomDockMode mode, juce::Rectangle<int> bounds);
    EditorViewModel m_model;
    CommandHandler m_commandHandler;
    TransportCallbacks m_transportCallbacks;
    MenuCallbacks m_menuCallbacks;

    juce::MenuBarComponent m_menuBar;
    TransportBar m_transportBar;
    CenterViewMode m_centerViewMode {CenterViewMode::arrangement};
    TimelineView m_timelineView;
    SessionMatrixView m_sessionMatrixView;

    // Header Workspace Switcher & HUD
    juce::TextButton m_btnWsArrangement {"Arrange [1]"};
    juce::TextButton m_btnWsBeatmaker {"Beatmaker [2]"};
    juce::TextButton m_btnWsRecording {"Record [3]"};
    juce::TextButton m_btnWsMixing {"Mix [4]"};
    juce::TextButton m_btnWsModular {"Modular [5]"};
    juce::TextButton m_btnWsMastering {"Master [6]"};
    juce::TextButton m_btnWsLive {"Live [7]"};
    juce::TextButton m_btnWsSave {"Save As..."};
    juce::TextButton m_btnToggleInspector {"Inspector [I]"};
    juce::TextButton m_btnCommandPalette {"Ctrl+K Search"};

    // Left Track Inspector
    TrackInspectorView m_trackInspector;
    DragSplitterBar m_inspectorSplitter;
    int m_inspectorWidth {210};
    bool m_inspectorVisible {true};

    // Command Palette Overlay (Ctrl+K)
    CommandPaletteOverlay m_commandPalette;

    // Bottom Dock (Categorized Smart Dock)
    BottomDockMode m_dockMode {BottomDockMode::mixer};
    juce::TextButton m_btnCatMix {"Mix"};
    juce::TextButton m_btnCatEdit {"Edit"};
    juce::TextButton m_btnCatModular {"Synth"};
    juce::TextButton m_btnCatMaster {"Master"};
    juce::Label m_lblActiveDock;
    juce::TextButton m_tabBrowserButton {"Browser [B]"};
    juce::TextButton m_btnDualDock {"Dual"};
    juce::TextButton m_detachDockButton {"Detach"};
    juce::TextButton m_maximizeDockButton {"Max"};

    bool m_dualDockEnabled {false};
    BottomDockMode m_secondaryDockMode {BottomDockMode::pianoRoll};
    float m_dockSplitRatio {0.5f};

    // Legacy tab buttons preserved for backward compatibility
    juce::TextButton m_tabMixerButton {"Mixer Console [M]"};
    juce::TextButton m_tabPianoRollButton {"Piano Roll [P]"};
    juce::TextButton m_tabChannelRackButton {"Channel Rack [R]"};
    juce::TextButton m_tabDrumSamplerButton {"Drum Sampler [D]"};
    juce::TextButton m_tabRoutingMatrixButton {"Patchbay [X]"};
    juce::TextButton m_tabModulationButton {"Modulators [O]"};
    juce::TextButton m_tabSpatialRadarButton {"Spatial 3D [Z]"};
    juce::TextButton m_tabWireGridButton {"Modular Grid [G]"};
    juce::TextButton m_tabMasteringButton {"Mastering [U]"};
    juce::TextButton m_tabPitchBlobButton {"Pitch/Blob [H]"};
    juce::TextButton m_tabSampleZoneButton {"SFZ Zones [K]"};

    MixerPanel m_mixerPanel;
    DeviceRackView m_deviceRackView;
    PianoRollView m_pianoRollView;
    ChannelRackView m_channelRackView;
    DrumPadSamplerView m_drumSamplerView;
    RoutingMatrixView m_routingMatrixView;
    ModulationRackView m_modulationRackView;
    SpatialRadarView m_spatialRadarView;
    DspWireGridView m_wireGridView;
    MasteringSuiteView m_masteringSuiteView;
    PitchBlobCompingView m_pitchBlobView;
    SampleZoneEditorView m_sampleZoneView;
    std::unique_ptr<PluginBrowserPanel> m_pluginBrowser;
    std::unique_ptr<SampleBrowserPanel> m_sampleBrowser;
    juce::TextButton m_sidebarTabPluginsButton {"Plugins"};
    juce::TextButton m_sidebarTabSamplesButton {"Samples"};
    SidebarTab m_sidebarTab {SidebarTab::plugins};
    bool m_browserVisible {false};

    // Detached Floating Windows
    std::unique_ptr<FloatingPanelWindow> m_floatingMixer;
    std::unique_ptr<FloatingPanelWindow> m_floatingDeviceRack;
    std::unique_ptr<FloatingPanelWindow> m_floatingPianoRoll;
    std::unique_ptr<FloatingPanelWindow> m_floatingChannelRack;
    std::unique_ptr<FloatingPanelWindow> m_floatingDrumSampler;
    std::unique_ptr<FloatingPanelWindow> m_floatingRoutingMatrix;
    std::unique_ptr<FloatingPanelWindow> m_floatingModulationRack;
    std::unique_ptr<FloatingPanelWindow> m_floatingSpatialRadar;
    std::unique_ptr<FloatingPanelWindow> m_floatingWireGrid;
    std::unique_ptr<FloatingPanelWindow> m_floatingMasteringSuite;
    std::unique_ptr<FloatingPanelWindow> m_floatingPitchBlob;
    std::unique_ptr<FloatingPanelWindow> m_floatingSampleZone;
    std::unique_ptr<FloatingPanelWindow> m_floatingBrowser;
    std::unique_ptr<FloatingPanelWindow> m_floatingSessionMatrix;

    // Draggable Splitters
    DragSplitterBar m_dualDockSplitter;
    DragSplitterBar m_dockSplitter;
    DragSplitterBar m_sidebarSplitter;

    int m_dockHeight {220};
    int m_sidebarWidth {270};
    bool m_dockMaximized {false};
    float m_canvasScale {1.0f};
};

} // namespace dawg::ui
