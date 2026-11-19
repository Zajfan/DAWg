#include "MainEditorView.h"
#include "DspEditorDialog.h"
#include "PluginWindow.h"
#include "SpectralRepairDialog.h"
#include "VideoPlaybackWindow.h"
#include "VirusTiPolarEditorView.h"
#include "Juno106EditorView.h"
#include "OmniFmEditorView.h"
#include "WorkspaceLayoutManager.h"
#include <dawg/engine/SmpteTimecode.h>
#include <dawg/plugins/NativePluginCatalog.h>

#include <utility>

namespace dawg::ui {

namespace {
enum MenuId
{
    // File
    cmdNewProject = 1,
    cmdOpenProject = 2,
    cmdSaveProject = 3,
    cmdSaveProjectAs = 4,
    cmdExportBounce = 5,
    cmdExportStems = 6,
    cmdProjectAudioPool = 7,
    cmdExit = 8,

    // Edit
    cmdUndo = 10,
    cmdRedo = 11,
    cmdSplitClip = 12,
    cmdDuplicateClip = 13,
    cmdDeleteClip = 14,

    // Track
    cmdAddAudioTrack = 20,
    cmdAddSynthTrack = 21,
    cmdAddFolderTrack = 22,
    cmdAddPlateReverb = 23,
    cmdAddStereoDelay = 24,
    cmdAddDrumBus = 25,

    // View
    cmdToggleMixer = 30,
    cmdTogglePianoRoll = 31,
    cmdToggleChannelRack = 34,
    cmdToggleSessionView = 35,
    cmdToggleAutomation = 32,
    cmdToggleBrowser = 33,

    // Workspace
    cmdWorkspaceDefault = 60,
    cmdWorkspaceDualScreen = 61,
    cmdWorkspaceBeatmaker = 62,
    cmdWorkspaceLive = 63,
    cmdWorkspaceMastering = 64,
    cmdSaveWorkspace = 65,

    // Detach / Dock / Maximize
    cmdDetachMixer = 70,
    cmdDetachPianoRoll = 71,
    cmdDetachChannelRack = 72,
    cmdReDockAll = 73,
    cmdToggleMaximizeDock = 74,

    // Zoom / HiDPI
    cmdZoomIn = 80,
    cmdZoomOut = 81,
    cmdZoomReset = 82,
    cmdZoom75 = 83,
    cmdZoom100 = 84,
    cmdZoom125 = 85,
    cmdZoom150 = 86,
    cmdZoom200 = 87,

    // Options
    cmdAudioPreferences = 40,
    cmdToggleMetronome = 41,
    cmdToggleLimiter = 42,
    cmdRescanPlugins = 43,

    // Templates & Bundler
    cmdTemplateModernTrap = 90,
    cmdTemplateSynthwave = 91,
    cmdTemplateAcousticRock = 92,
    cmdTemplateMastering = 93,
    cmdConsolidateProject = 94,
    cmdOpenVideoMonitor = 96,
    cmdOpenSpectralRepair = 97,

    // Help
    cmdShortcuts = 50,
    cmdAbout = 51
};
} // namespace

MainEditorView::MainEditorView(EditorViewModel model,
                               CommandHandler commandHandler,
                               TransportCallbacks transportCallbacks,
                               MenuCallbacks menuCallbacks,
                               media::WaveformCache* waveformCache)
    : m_model(std::move(model))
    , m_commandHandler(commandHandler)
    , m_transportCallbacks(transportCallbacks)
    , m_menuCallbacks(std::move(menuCallbacks))
    , m_menuBar(this)
    , m_transportBar({
        .commandHandler = m_commandHandler,
        .importWav = m_transportCallbacks.importWav,
        .play = m_transportCallbacks.play,
        .stop = m_transportCallbacks.stop,
        .returnToStart = m_transportCallbacks.returnToStart,
        .record = m_transportCallbacks.record,
        .undo = m_transportCallbacks.undo,
        .redo = m_transportCallbacks.redo
    })
    , m_timelineView({
        .commandHandler = m_commandHandler,
        .seek = m_transportCallbacks.seek,
        .togglePlay = [this] {
            if (m_model.playing)
            {
                if (m_transportCallbacks.stop)
                    m_transportCallbacks.stop();
            }
            else
            {
                if (m_transportCallbacks.play)
                    m_transportCallbacks.play();
            }
        },
        .undo = m_transportCallbacks.undo,
        .redo = m_transportCallbacks.redo,
        .trackSelected = [this](project::TrackId trkId) {
            m_selectedTrackId = trkId;
            m_trackInspector.setSelectedTrackId(trkId);
            m_deviceRackView.setSelectedTrackId(trkId);
            m_channelRackView.setSelectedTrackId(trkId);
            m_pianoRollView.setSelectedTrackId(trkId);
            if (m_transportCallbacks.trackSelected)
                m_transportCallbacks.trackSelected(trkId);
        },
        .importFile = [this](const std::filesystem::path& path, project::TrackId trkId, std::uint64_t startSample) {
            if (m_transportCallbacks.importSampleAt)
                m_transportCallbacks.importSampleAt(path, trkId, startSample);
            else if (m_transportCallbacks.importSample)
                m_transportCallbacks.importSample(path);
        }
    }, waveformCache)
    , m_sessionMatrixView(m_commandHandler, m_transportCallbacks.audition)
    , m_trackInspector(m_commandHandler, [this](project::TrackId trkId, std::size_t slotIdx) {
        const project::Track* trk = nullptr;
        for (const auto& t : m_model.project.tracks)
        {
            if (t.id == trkId)
            {
                trk = &t;
                break;
            }
        }
        if (trk != nullptr && slotIdx < trk->inserts.size())
        {
            DspEditorKind kind = DspEditorKind::parametricEq;
            switch (trk->inserts[slotIdx].type)
            {
            case project::DspNodeType::parametricEq: kind = DspEditorKind::parametricEq; break;
            case project::DspNodeType::dynamicsCompressor: kind = DspEditorKind::compressor; break;
            case project::DspNodeType::tnhcSynth: kind = DspEditorKind::synth; break;
            case project::DspNodeType::tnhcReverb: kind = DspEditorKind::reverb; break;
            case project::DspNodeType::tnhcDelay: kind = DspEditorKind::delay; break;
            case project::DspNodeType::resonanceSuppressor: kind = DspEditorKind::resonanceSuppressor; break;
            case project::DspNodeType::multibandStereoImager: kind = DspEditorKind::multibandImager; break;
            case project::DspNodeType::linearPhaseMasteringEq: kind = DspEditorKind::linearPhaseEq; break;
            case project::DspNodeType::buchlaComplexSynth: kind = DspEditorKind::buchlaComplexSynth; break;
            case project::DspNodeType::virusTiPolarSynth:
                VirusTiPolarWindow::open();
                return;
            case project::DspNodeType::juno106Synth:
                Juno106Window::open();
                return;
            case project::DspNodeType::omniFmSynth:
                OmniFmWindow::open();
                return;
            case project::DspNodeType::soundFontPlayer:
                DspEditorWindow::open(DspEditorKind::synth, trk->name);
                return;
            case project::DspNodeType::brickwallLimiter:
            case project::DspNodeType::clapPlugin:
            case project::DspNodeType::drumPadSampler:
            case project::DspNodeType::vectorSequencer:
            case project::DspNodeType::physicalModelSynth:
            case project::DspNodeType::granularCloud:
            case project::DspNodeType::analogLadderSynth:
            case project::DspNodeType::tubeTransformer:
            case project::DspNodeType::analogDegrader:
            case project::DspNodeType::rayTracingReverb:
                return;
            }
            DspEditorWindow::open(kind, trk->name);
        }
    })
    , m_inspectorSplitter(DragSplitterBar::Orientation::vertical, [this](const int delta) {
        if (m_inspectorVisible)
        {
            m_inspectorWidth = std::clamp(m_inspectorWidth + delta, 150, std::max(180, getWidth() - 320));
            resized();
        }
    })
    , m_commandPalette([this](const std::string& actionId) {
        if (actionId == "panel:mixer") setDockMode(BottomDockMode::mixer);
        else if (actionId == "panel:deviceRack") setDockMode(BottomDockMode::deviceChain);
        else if (actionId == "panel:pianoRoll") setDockMode(BottomDockMode::pianoRoll);
        else if (actionId == "panel:channelRack") setDockMode(BottomDockMode::channelRack);
        else if (actionId == "panel:drumSampler") setDockMode(BottomDockMode::drumSampler);
        else if (actionId == "panel:wireGrid") setDockMode(BottomDockMode::wireGrid);
        else if (actionId == "panel:routingMatrix") setDockMode(BottomDockMode::routingMatrix);
        else if (actionId == "panel:spatialRadar") setDockMode(BottomDockMode::spatialRadar);
        else if (actionId == "panel:modulators") setDockMode(BottomDockMode::modulationRack);
        else if (actionId == "panel:mastering") setDockMode(BottomDockMode::masteringSuite);
        else if (actionId == "panel:pitchBlob") setDockMode(BottomDockMode::pitchComping);
        else if (actionId == "panel:sampleZone") setDockMode(BottomDockMode::sampleZones);
        else if (actionId == "panel:browser") togglePluginBrowser();
        else if (actionId == "workspace:arrangement") applyWorkspacePreset(WorkspacePreset::songArrangement);
        else if (actionId == "workspace:beatmaker") applyWorkspacePreset(WorkspacePreset::patternLabBeatmaker);
        else if (actionId == "workspace:recording") applyWorkspacePreset(WorkspacePreset::recordingComping);
        else if (actionId == "workspace:mixing") applyWorkspacePreset(WorkspacePreset::mixingConsole);
        else if (actionId == "workspace:modular") applyWorkspacePreset(WorkspacePreset::soundDesignModular);
        else if (actionId == "workspace:mastering") applyWorkspacePreset(WorkspacePreset::masteringSuite);
        else if (actionId == "workspace:liveRig") applyWorkspacePreset(WorkspacePreset::livePerformanceRig);
        else if (actionId == "action:toggleInspector") toggleTrackInspector();
        else if (actionId == "action:toggleCenterView") toggleCenterViewMode();
        else if (actionId == "action:quantize") m_pianoRollView.quantizeNotes();
        else if (actionId == "action:addAudioTrack") { if (m_commandHandler) m_commandHandler(project::AddAudioTrack {"Audio " + std::to_string(m_model.trackCount() + 1)}); }
        else if (actionId == "action:addMidiTrack") { if (m_commandHandler) m_commandHandler(project::AddInstrumentTrack {"Synth Lead"}); }
        else if (actionId == "action:importWav") { if (m_transportCallbacks.importWav) m_transportCallbacks.importWav(); }
        else if (actionId == "action:bounceStems") { if (m_menuCallbacks.exportStems) m_menuCallbacks.exportStems(); }
        else if (actionId == "action:audioPool") { if (m_menuCallbacks.showAudioPool) m_menuCallbacks.showAudioPool(); }
        else if (actionId == "action:maximizeDock") toggleMaximizeDock();
        else if (actionId == "action:reDockAll") reDockAllPanels();
        else if (actionId == "action:consolidate") { if (m_menuCallbacks.consolidateProject) m_menuCallbacks.consolidateProject(); }
        else if (actionId == "action:rescanPlugins") { if (m_menuCallbacks.rescanPlugins) m_menuCallbacks.rescanPlugins(); }
        else if (actionId == "action:videoMonitor") VideoPlaybackWindow::open(m_model);
        else if (actionId == "action:spectralRepair") SpectralRepairDialog::open(m_model);
        else if (actionId == "action:cueSheet") {
            std::vector<engine::CueSheetEntry> demoCues;
            demoCues.push_back({1, "Opening Theme", "01:00:00:00", "01:01:30:00", "00:01:30:00", "Dm", 110.0, "Composer", "Main Title Sequence"});
            demoCues.push_back({2, "Action Chase", "01:01:30:00", "01:03:45:12", "00:02:15:12", "Fm", 140.0, "Composer", "Rooftop Pursuit Scene"});
            const auto report = engine::SmpteTimecodeEngine::generateCueSheetReportText(m_model.project.name.empty() ? "Film Score Project" : m_model.project.name, demoCues);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "DAWg Production Cue Sheet", report);
        }
        else if (actionId == "action:stepRecord") m_pianoRollView.toggleStepRecord();
        else if (actionId == "template:modernTrap") { if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("modernTrap"); }
        else if (actionId == "template:synthwave") { if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("synthwaveCyberpunk"); }
        else if (actionId == "template:acousticRock") { if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("multiMicAcousticRock"); }
        else if (actionId == "template:mastering") { if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("masteringSession"); }
    })
    , m_mixerPanel(m_commandHandler, m_transportCallbacks.setMasterLimiter)
    , m_deviceRackView(m_commandHandler)
    , m_pianoRollView(m_commandHandler)
    , m_channelRackView(m_commandHandler, m_transportCallbacks.audition)
    , m_drumSamplerView(m_commandHandler, m_transportCallbacks.audition)
    , m_routingMatrixView(m_commandHandler, nullptr)
    , m_modulationRackView(m_commandHandler)
    , m_spatialRadarView([this](std::uint64_t trackId, float az, [[maybe_unused]] float el, [[maybe_unused]] float dist) {
        if (m_commandHandler)
        {
            float currentGain = 1.0f;
            bool currentPhase = false;
            for (const auto& t : m_model.project.tracks)
            {
                if (t.id == trackId)
                {
                    currentGain = t.gainLinear;
                    currentPhase = t.phaseInvert;
                    break;
                }
            }
            const float pan = std::clamp(az / 90.0f, -1.0f, 1.0f);
            m_commandHandler(project::SetTrackGainPan {
                .id = trackId,
                .gainLinear = currentGain,
                .pan = pan,
                .phaseInvert = currentPhase
            });
        }
    })
    , m_wireGridView()
    , m_masteringSuiteView()
    , m_pitchBlobView(m_commandHandler)
    , m_sampleZoneView(m_transportCallbacks.audition)
    , m_dualDockSplitter(DragSplitterBar::Orientation::vertical, [this](const int delta) {
        if (m_dualDockEnabled)
        {
            const float newRatio = std::clamp(m_dockSplitRatio + static_cast<float>(delta) / static_cast<float>(std::max(100, getWidth())), 0.2f, 0.8f);
            m_dockSplitRatio = newRatio;
            resized();
        }
    })
    , m_dockSplitter(DragSplitterBar::Orientation::horizontal, [this](const int delta) {
        if (! m_dockMaximized)
        {
            m_dockHeight = std::clamp(m_dockHeight - delta, 50, std::max(60, getHeight() - 110));
            resized();
        }
    })
    , m_sidebarSplitter(DragSplitterBar::Orientation::vertical, [this](const int delta) {
        if (m_browserVisible)
        {
            m_sidebarWidth = std::clamp(m_sidebarWidth - delta, 180, std::max(200, getWidth() - 250));
            resized();
        }
    })
{
    addAndMakeVisible(m_menuBar);
    addAndMakeVisible(m_transportBar);
    addAndMakeVisible(m_timelineView);
    addChildComponent(m_sessionMatrixView); // hidden initially

    // 1. Workspace Header Buttons
    auto setupWsBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 35, 44));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(190, 195, 210));
    };
    setupWsBtn(m_btnWsArrangement);
    m_btnWsArrangement.onClick = [this] { applyWorkspacePreset(WorkspacePreset::songArrangement); };
    addAndMakeVisible(m_btnWsArrangement);

    setupWsBtn(m_btnWsBeatmaker);
    m_btnWsBeatmaker.onClick = [this] { applyWorkspacePreset(WorkspacePreset::patternLabBeatmaker); };
    addAndMakeVisible(m_btnWsBeatmaker);

    setupWsBtn(m_btnWsRecording);
    m_btnWsRecording.onClick = [this] { applyWorkspacePreset(WorkspacePreset::recordingComping); };
    addAndMakeVisible(m_btnWsRecording);

    setupWsBtn(m_btnWsMixing);
    m_btnWsMixing.onClick = [this] { applyWorkspacePreset(WorkspacePreset::mixingConsole); };
    addAndMakeVisible(m_btnWsMixing);

    setupWsBtn(m_btnWsModular);
    m_btnWsModular.onClick = [this] { applyWorkspacePreset(WorkspacePreset::soundDesignModular); };
    addAndMakeVisible(m_btnWsModular);

    setupWsBtn(m_btnWsMastering);
    m_btnWsMastering.onClick = [this] { applyWorkspacePreset(WorkspacePreset::masteringSuite); };
    addAndMakeVisible(m_btnWsMastering);

    setupWsBtn(m_btnWsLive);
    m_btnWsLive.onClick = [this] { applyWorkspacePreset(WorkspacePreset::livePerformanceRig); };
    addAndMakeVisible(m_btnWsLive);

    setupWsBtn(m_btnWsSave);
    m_btnWsSave.onClick = [this] { saveCurrentWorkspace(); };
    addAndMakeVisible(m_btnWsSave);

    m_btnToggleInspector.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 95, 160));
    m_btnToggleInspector.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    m_btnToggleInspector.onClick = [this] { toggleTrackInspector(); };
    addAndMakeVisible(m_btnToggleInspector);

    m_btnCommandPalette.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 55, 85));
    m_btnCommandPalette.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(140, 200, 255));
    m_btnCommandPalette.onClick = [this] { openCommandPalette(); };
    addAndMakeVisible(m_btnCommandPalette);

    // 2. Left Track Inspector
    addAndMakeVisible(m_trackInspector);
    addAndMakeVisible(m_inspectorSplitter);

    // 3. Command Palette
    addChildComponent(m_commandPalette);

    // 4. Categorized Smart Dock
    m_btnCatMix.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 52));
    m_btnCatMix.onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, "Mixer Console [M]", true, m_dockMode == BottomDockMode::mixer);
        m.addItem(2, "Console Patchbay / Routing Matrix [X]", true, m_dockMode == BottomDockMode::routingMatrix);
        m.addItem(3, "3D Spatial Radar & Binaural [Z]", true, m_dockMode == BottomDockMode::spatialRadar);
        m.addItem(4, "Device Chain / FX Rack [C]", true, m_dockMode == BottomDockMode::deviceChain);
        m.showMenuAsync(juce::PopupMenu::Options {}, [this](int r) {
            if (r == 1) setDockMode(BottomDockMode::mixer);
            else if (r == 2) setDockMode(BottomDockMode::routingMatrix);
            else if (r == 3) setDockMode(BottomDockMode::spatialRadar);
            else if (r == 4) setDockMode(BottomDockMode::deviceChain);
        });
    };
    addAndMakeVisible(m_btnCatMix);

    m_btnCatEdit.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 52));
    m_btnCatEdit.onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, "Piano Roll Editor (MPE) [P]", true, m_dockMode == BottomDockMode::pianoRoll);
        m.addItem(2, "Channel Rack & Step Sequencer [R]", true, m_dockMode == BottomDockMode::channelRack);
        m.addItem(3, "Drum Pad Sampler (MPC) [D]", true, m_dockMode == BottomDockMode::drumSampler);
        m.addItem(4, "Multi-Sample SFZ Zones [K]", true, m_dockMode == BottomDockMode::sampleZones);
        m.showMenuAsync(juce::PopupMenu::Options {}, [this](int r) {
            if (r == 1) setDockMode(BottomDockMode::pianoRoll);
            else if (r == 2) setDockMode(BottomDockMode::channelRack);
            else if (r == 3) setDockMode(BottomDockMode::drumSampler);
            else if (r == 4) setDockMode(BottomDockMode::sampleZones);
        });
    };
    addAndMakeVisible(m_btnCatEdit);

    m_btnCatModular.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 52));
    m_btnCatModular.onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, "Modular DSP Wire Grid [G]", true, m_dockMode == BottomDockMode::wireGrid);
        m.addItem(2, "Modulation Rack (LFO/Env/Step) [O]", true, m_dockMode == BottomDockMode::modulationRack);
        m.showMenuAsync(juce::PopupMenu::Options {}, [this](int r) {
            if (r == 1) setDockMode(BottomDockMode::wireGrid);
            else if (r == 2) setDockMode(BottomDockMode::modulationRack);
        });
    };
    addAndMakeVisible(m_btnCatModular);

    m_btnCatMaster.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 52));
    m_btnCatMaster.onClick = [this] {
        juce::PopupMenu m;
        m.addItem(1, "Mastering Suite & Loudness Radar [U]", true, m_dockMode == BottomDockMode::masteringSuite);
        m.addItem(2, "Pitch Contour & Vocal Tuner [H]", true, m_dockMode == BottomDockMode::pitchComping);
        m.addItem(3, "Project Audio Pool [A]", true, false);
        m.showMenuAsync(juce::PopupMenu::Options {}, [this](int r) {
            if (r == 1) setDockMode(BottomDockMode::masteringSuite);
            else if (r == 2) setDockMode(BottomDockMode::pitchComping);
            else if (r == 3 && m_menuCallbacks.showAudioPool) m_menuCallbacks.showAudioPool();
        });
    };
    addAndMakeVisible(m_btnCatMaster);

    m_lblActiveDock.setFont(juce::FontOptions {11.0f}.withStyle("Bold"));
    m_lblActiveDock.setColour(juce::Label::textColourId, juce::Colour::fromRGB(0, 220, 255));
    m_lblActiveDock.setText("[MIX] Mixer Console", juce::dontSendNotification);
    addAndMakeVisible(m_lblActiveDock);

    m_btnDualDock.setTooltip("Split bottom dock into dual side-by-side panes");
    m_btnDualDock.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(28, 30, 36));
    m_btnDualDock.onClick = [this] { toggleDualDock(); };
    addAndMakeVisible(m_btnDualDock);

    m_tabBrowserButton.setClickingTogglesState(false);
    m_tabBrowserButton.onClick = [this] { togglePluginBrowser(); };
    addAndMakeVisible(m_tabBrowserButton);

    addChildComponent(m_dualDockSplitter);

    // Detach and Maximize dock controls
    m_detachDockButton.setTooltip("Detach active panel to floating window (Multi-Screen)");
    m_detachDockButton.onClick = [this] {
        if (m_dockMode == BottomDockMode::mixer)
            detachPanel(DetachablePanel::mixer);
        else if (m_dockMode == BottomDockMode::deviceChain)
            detachPanel(DetachablePanel::deviceChain);
        else if (m_dockMode == BottomDockMode::pianoRoll)
            detachPanel(DetachablePanel::pianoRoll);
        else if (m_dockMode == BottomDockMode::channelRack)
            detachPanel(DetachablePanel::channelRack);
        else if (m_dockMode == BottomDockMode::drumSampler)
            detachPanel(DetachablePanel::drumSampler);
        else if (m_dockMode == BottomDockMode::routingMatrix)
            detachPanel(DetachablePanel::routingMatrix);
        else if (m_dockMode == BottomDockMode::modulationRack)
            detachPanel(DetachablePanel::modulationRack);
        else if (m_dockMode == BottomDockMode::spatialRadar)
            detachPanel(DetachablePanel::spatialRadar);
        else if (m_dockMode == BottomDockMode::wireGrid)
            detachPanel(DetachablePanel::wireGrid);
        else if (m_dockMode == BottomDockMode::masteringSuite)
            detachPanel(DetachablePanel::masteringSuite);
        else if (m_dockMode == BottomDockMode::pitchComping)
            detachPanel(DetachablePanel::pitchComping);
        else if (m_dockMode == BottomDockMode::sampleZones)
            detachPanel(DetachablePanel::sampleZones);
    };
    addAndMakeVisible(m_detachDockButton);

    m_maximizeDockButton.setTooltip("Maximize / Restore Bottom Dock [F11]");
    m_maximizeDockButton.onClick = [this] { toggleMaximizeDock(); };
    addAndMakeVisible(m_maximizeDockButton);

    m_dockSplitter.setOnDoubleClick([this] { toggleMaximizeDock(); });
    addAndMakeVisible(m_dockSplitter);
    addChildComponent(m_sidebarSplitter);

    addAndMakeVisible(m_mixerPanel);
    addChildComponent(m_deviceRackView);
    addChildComponent(m_pianoRollView);
    m_channelRackView.setOpenPianoRollCallback([this] { setDockMode(BottomDockMode::pianoRoll); });
    addChildComponent(m_channelRackView);
    addChildComponent(m_drumSamplerView);
    addChildComponent(m_routingMatrixView);
    addChildComponent(m_modulationRackView);
    addChildComponent(m_spatialRadarView);
    addChildComponent(m_wireGridView);
    addChildComponent(m_masteringSuiteView);
    addChildComponent(m_pitchBlobView);
    addChildComponent(m_sampleZoneView);

    m_pluginBrowser = std::make_unique<PluginBrowserPanel>(
        [this](const plugins::PluginDescriptor& plugin, bool createNewTrack) {
            std::vector<PluginParameterDesc> params;
            if (const auto* def = plugins::NativePluginCatalog::findPlugin(plugin.id))
            {
                for (const auto& p : def->parameters)
                {
                    params.push_back(PluginParameterDesc {
                        .id = p.id,
                        .name = p.name,
                        .minVal = p.minVal,
                        .maxVal = p.maxVal,
                        .defaultVal = p.defaultVal,
                        .currentVal = p.defaultVal,
                        .unitLabel = p.unit,
                        .isStepped = p.isStepped
                    });
                }
            }
            auto openDedicatedPluginEditor = [this, &plugin](std::vector<PluginParameterDesc>&& pList) {
                const auto lower = juce::String(plugin.name).toLowerCase();
                if (plugin.id == "dawg.synth.virus_ti_polar" || lower.contains("virus") || lower.contains("polar"))
                {
                    VirusTiPolarWindow::open();
                }
                else if (plugin.id == "dawg.synth.juno_106" || lower.contains("juno"))
                {
                    Juno106Window::open();
                }
                else if (plugin.id == "dawg.synth.omni_fm" || lower.contains("omnifm") || lower.contains("omni fm"))
                {
                    OmniFmWindow::open();
                }
                else if (plugin.id == "dawg.synth.buchla_complex" || lower.contains("buchla") || lower.contains("complex 259"))
                {
                    DspEditorWindow::open(DspEditorKind::buchlaComplexSynth, plugin.name);
                }
                else if (plugin.id == "dawg.effect.dynamic_resonance" || plugin.id == "dawg.fx.resonance_suppressor" || lower.contains("resonance") || lower.contains("resotamer"))
                {
                    DspEditorWindow::open(DspEditorKind::resonanceSuppressor, plugin.name);
                }
                else if (plugin.id == "dawg.effect.multiband_imager" || plugin.id == "dawg.fx.multiband_imager" || lower.contains("stereoscope") || lower.contains("imager"))
                {
                    DspEditorWindow::open(DspEditorKind::multibandImager, plugin.name);
                }
                else if (plugin.id == "dawg.effect.linear_phase_eq" || plugin.id == "dawg.fx.linear_phase_eq" || lower.contains("masterphase") || lower.contains("linear phase"))
                {
                    DspEditorWindow::open(DspEditorKind::linearPhaseEq, plugin.name);
                }
                else if (plugin.id == "tnhc.parametric_eq" || lower.contains("parametric eq"))
                {
                    DspEditorWindow::open(DspEditorKind::parametricEq, plugin.name);
                }
                else if (plugin.id == "tnhc.studio_compressor" || lower.contains("compressor"))
                {
                    DspEditorWindow::open(DspEditorKind::compressor, plugin.name);
                }
                else if (plugin.id == "tnhc.plate_reverb" || lower.contains("reverb"))
                {
                    DspEditorWindow::open(DspEditorKind::reverb, plugin.name);
                }
                else if (plugin.id == "tnhc.stereo_delay" || lower.contains("delay"))
                {
                    DspEditorWindow::open(DspEditorKind::delay, plugin.name);
                }
                else if (plugin.id == "tnhc.synth" || plugin.id == "dawg.synth.soundfont_player" || lower.contains("soundfont") || lower.contains("poly synth"))
                {
                    DspEditorWindow::open(DspEditorKind::synth, plugin.name);
                }
                else
                {
                    PluginWindow::open(plugin.name, std::move(pList));
                }
            };

            if (createNewTrack)
            {
                if (plugin.isInstrument)
                    m_commandHandler(project::AddInstrumentTrack { plugin.name });
                else
                    m_commandHandler(project::AddAudioTrack { plugin.name });
                openDedicatedPluginEditor(std::move(params));
                return;
            }

            if (m_model.project.tracks.empty())
            {
                openDedicatedPluginEditor(std::move(params));
                return;
            }

            // Find selected track or fallback to first track
            const project::Track* targetTrack = &m_model.project.tracks.front();
            if (m_selectedTrackId.has_value())
            {
                for (const auto& t : m_model.project.tracks)
                {
                    if (t.id == *m_selectedTrackId)
                    {
                        targetTrack = &t;
                        break;
                    }
                }
            }

            project::DspNodeType nodeType = project::DspNodeType::clapPlugin;
            if (plugin.id == "tnhc.parametric_eq")
                nodeType = project::DspNodeType::parametricEq;
            else if (plugin.id == "tnhc.studio_compressor")
                nodeType = project::DspNodeType::dynamicsCompressor;
            else if (plugin.id == "tnhc.master_limiter")
                nodeType = project::DspNodeType::brickwallLimiter;
            else if (plugin.id == "tnhc.synth")
                nodeType = project::DspNodeType::tnhcSynth;
            else if (plugin.id == "tnhc.plate_reverb")
                nodeType = project::DspNodeType::tnhcReverb;
            else if (plugin.id == "tnhc.stereo_delay")
                nodeType = project::DspNodeType::tnhcDelay;
            else if (plugin.id == "dawg.effect.dynamic_resonance" || plugin.id == "dawg.fx.resonance_suppressor")
                nodeType = project::DspNodeType::resonanceSuppressor;
            else if (plugin.id == "dawg.effect.multiband_imager" || plugin.id == "dawg.fx.multiband_imager")
                nodeType = project::DspNodeType::multibandStereoImager;
            else if (plugin.id == "dawg.effect.linear_phase_eq" || plugin.id == "dawg.fx.linear_phase_eq")
                nodeType = project::DspNodeType::linearPhaseMasteringEq;
            else if (plugin.id == "dawg.synth.buchla_complex")
                nodeType = project::DspNodeType::buchlaComplexSynth;
            else if (plugin.id == "dawg.synth.vector_sequencer")
                nodeType = project::DspNodeType::vectorSequencer;
            else if (plugin.id == "dawg.synth.physical_model")
                nodeType = project::DspNodeType::physicalModelSynth;
            else if (plugin.id == "dawg.synth.granular_cloud")
                nodeType = project::DspNodeType::granularCloud;
            else if (plugin.id == "dawg.synth.analog_ladder")
                nodeType = project::DspNodeType::analogLadderSynth;
            else if (plugin.id == "dawg.effect.vintage_tube")
                nodeType = project::DspNodeType::tubeTransformer;
            else if (plugin.id == "dawg.effect.analog_degrader")
                nodeType = project::DspNodeType::analogDegrader;
            else if (plugin.id == "dawg.effect.ray_tracing_reverb")
                nodeType = project::DspNodeType::rayTracingReverb;
            else if (plugin.id == "dawg.synth.virus_ti_polar")
                nodeType = project::DspNodeType::virusTiPolarSynth;
            else if (plugin.id == "dawg.synth.juno_106")
                nodeType = project::DspNodeType::juno106Synth;
            else if (plugin.id == "dawg.synth.omni_fm")
                nodeType = project::DspNodeType::omniFmSynth;
            else if (plugin.id == "dawg.synth.soundfont_player")
                nodeType = project::DspNodeType::soundFontPlayer;

            m_commandHandler(project::AddTrackInsert {
                targetTrack->id,
                project::DspNodeDescriptor {
                    .slotIndex = static_cast<std::uint32_t>(targetTrack->inserts.size()),
                    .type = nodeType,
                    .bypassed = false,
                    .name = plugin.name,
                    .pluginId = plugin.id,
                    .parameters = {}
                }
            });

            openDedicatedPluginEditor(std::move(params));
        });
    addChildComponent(*m_pluginBrowser);

    m_sampleBrowser = std::make_unique<SampleBrowserPanel>(
        [this](const std::filesystem::path& path) {
            if (m_transportCallbacks.importSample)
                m_transportCallbacks.importSample(path);
        },
        [this](const std::filesystem::path& path, const bool /*syncTempo*/) {
            if (! path.empty())
            {
                if (m_transportCallbacks.audition)
                    m_transportCallbacks.audition(60, 100, true);
            }
            else
            {
                if (m_transportCallbacks.audition)
                    m_transportCallbacks.audition(60, 0, false);
            }
        });
    addChildComponent(*m_sampleBrowser);

    m_sidebarTabPluginsButton.onClick = [this] { setSidebarTab(SidebarTab::plugins); };
    m_sidebarTabPluginsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1f6feb));
    addChildComponent(m_sidebarTabPluginsButton);

    m_sidebarTabSamplesButton.onClick = [this] { setSidebarTab(SidebarTab::samples); };
    m_sidebarTabSamplesButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff21262d));
    addChildComponent(m_sidebarTabSamplesButton);

    m_pianoRollView.setAuditionCallback(m_transportCallbacks.audition);

    setDockMode(BottomDockMode::mixer);
    setViewModel(m_model);
}

MainEditorView::~MainEditorView()
{
    m_floatingMixer.reset();
    m_floatingPianoRoll.reset();
    m_floatingChannelRack.reset();
    m_floatingBrowser.reset();
    m_floatingSessionMatrix.reset();
}

void MainEditorView::setSidebarTab(const SidebarTab tab)
{
    m_sidebarTab = tab;
    m_sidebarTabPluginsButton.setColour(juce::TextButton::buttonColourId,
        tab == SidebarTab::plugins ? juce::Colour(0xff1f6feb) : juce::Colour(0xff21262d));
    m_sidebarTabSamplesButton.setColour(juce::TextButton::buttonColourId,
        tab == SidebarTab::samples ? juce::Colour(0xff1f6feb) : juce::Colour(0xff21262d));

    if (m_browserVisible)
    {
        if (m_pluginBrowser)
            m_pluginBrowser->setVisible(tab == SidebarTab::plugins);
        if (m_sampleBrowser)
            m_sampleBrowser->setVisible(tab == SidebarTab::samples);
    }
    resized();
}

void MainEditorView::togglePluginBrowser()
{
    m_browserVisible = ! m_browserVisible;
    if (m_pluginBrowser)
        m_pluginBrowser->setVisible(m_browserVisible && m_sidebarTab == SidebarTab::plugins);
    if (m_sampleBrowser)
        m_sampleBrowser->setVisible(m_browserVisible && m_sidebarTab == SidebarTab::samples);
    m_sidebarTabPluginsButton.setVisible(m_browserVisible);
    m_sidebarTabSamplesButton.setVisible(m_browserVisible);
    m_tabBrowserButton.setColour(juce::TextButton::buttonColourId,
                                m_browserVisible ? juce::Colour::fromRGB(45, 95, 145) : juce::Colour::fromRGB(28, 30, 36));
    resized();
}

void MainEditorView::toggleTrackInspector()
{
    m_inspectorVisible = ! m_inspectorVisible;
    m_trackInspector.setVisible(m_inspectorVisible);
    m_inspectorSplitter.setVisible(m_inspectorVisible);
    m_btnToggleInspector.setColour(juce::TextButton::buttonColourId,
        m_inspectorVisible ? juce::Colour::fromRGB(45, 95, 160) : juce::Colour::fromRGB(32, 35, 44));
    resized();
}

void MainEditorView::openCommandPalette()
{
    m_commandPalette.open();
}

void MainEditorView::toggleDualDock()
{
    m_dualDockEnabled = ! m_dualDockEnabled;
    m_dualDockSplitter.setVisible(m_dualDockEnabled);
    m_btnDualDock.setColour(juce::TextButton::buttonColourId,
        m_dualDockEnabled ? juce::Colour::fromRGB(45, 95, 160) : juce::Colour::fromRGB(28, 30, 36));
    setDockMode(m_dockMode);
    resized();
}

void MainEditorView::setSecondaryDockMode(BottomDockMode mode)
{
    m_secondaryDockMode = mode;
    setDockMode(m_dockMode);
    resized();
}

void MainEditorView::layoutDockPanel(BottomDockMode mode, juce::Rectangle<int> bounds)
{
    switch (mode)
    {
    case BottomDockMode::mixer:
        if (! m_floatingMixer) m_mixerPanel.setBounds(bounds);
        break;
    case BottomDockMode::deviceChain:
        if (! m_floatingDeviceRack) m_deviceRackView.setBounds(bounds);
        break;
    case BottomDockMode::pianoRoll:
        if (! m_floatingPianoRoll) m_pianoRollView.setBounds(bounds);
        break;
    case BottomDockMode::channelRack:
        if (! m_floatingChannelRack) m_channelRackView.setBounds(bounds);
        break;
    case BottomDockMode::drumSampler:
        if (! m_floatingDrumSampler) m_drumSamplerView.setBounds(bounds);
        break;
    case BottomDockMode::routingMatrix:
        if (! m_floatingRoutingMatrix) m_routingMatrixView.setBounds(bounds);
        break;
    case BottomDockMode::modulationRack:
        if (! m_floatingModulationRack) m_modulationRackView.setBounds(bounds);
        break;
    case BottomDockMode::spatialRadar:
        if (! m_floatingSpatialRadar) m_spatialRadarView.setBounds(bounds);
        break;
    case BottomDockMode::wireGrid:
        if (! m_floatingWireGrid) m_wireGridView.setBounds(bounds);
        break;
    case BottomDockMode::masteringSuite:
        if (! m_floatingMasteringSuite) m_masteringSuiteView.setBounds(bounds);
        break;
    case BottomDockMode::pitchComping:
        if (! m_floatingPitchBlob) m_pitchBlobView.setBounds(bounds);
        break;
    case BottomDockMode::sampleZones:
        if (! m_floatingSampleZone) m_sampleZoneView.setBounds(bounds);
        break;
    }
}

void MainEditorView::setDockMode(BottomDockMode mode)
{
    m_dockMode = mode;

    if (m_dockMode == BottomDockMode::mixer && m_floatingMixer)
    {
        m_floatingMixer->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::deviceChain && m_floatingDeviceRack)
    {
        m_floatingDeviceRack->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::pianoRoll && m_floatingPianoRoll)
    {
        m_floatingPianoRoll->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::channelRack && m_floatingChannelRack)
    {
        m_floatingChannelRack->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::drumSampler && m_floatingDrumSampler)
    {
        m_floatingDrumSampler->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::routingMatrix && m_floatingRoutingMatrix)
    {
        m_floatingRoutingMatrix->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::modulationRack && m_floatingModulationRack)
    {
        m_floatingModulationRack->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::spatialRadar && m_floatingSpatialRadar)
    {
        m_floatingSpatialRadar->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::wireGrid && m_floatingWireGrid)
    {
        m_floatingWireGrid->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::masteringSuite && m_floatingMasteringSuite)
    {
        m_floatingMasteringSuite->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::pitchComping && m_floatingPitchBlob)
    {
        m_floatingPitchBlob->toFront(true);
        return;
    }
    if (m_dockMode == BottomDockMode::sampleZones && m_floatingSampleZone)
    {
        m_floatingSampleZone->toFront(true);
        return;
    }

    auto isPanelActive = [this](BottomDockMode checkMode) {
        if (m_dockMode == checkMode) return true;
        if (m_dualDockEnabled && m_secondaryDockMode == checkMode) return true;
        return false;
    };

    if (! m_floatingMixer) m_mixerPanel.setVisible(isPanelActive(BottomDockMode::mixer));
    if (! m_floatingDeviceRack) m_deviceRackView.setVisible(isPanelActive(BottomDockMode::deviceChain));
    if (! m_floatingPianoRoll) m_pianoRollView.setVisible(isPanelActive(BottomDockMode::pianoRoll));
    if (! m_floatingChannelRack) m_channelRackView.setVisible(isPanelActive(BottomDockMode::channelRack));
    if (! m_floatingDrumSampler) m_drumSamplerView.setVisible(isPanelActive(BottomDockMode::drumSampler));
    if (! m_floatingRoutingMatrix) m_routingMatrixView.setVisible(isPanelActive(BottomDockMode::routingMatrix));
    if (! m_floatingModulationRack) m_modulationRackView.setVisible(isPanelActive(BottomDockMode::modulationRack));
    if (! m_floatingSpatialRadar) m_spatialRadarView.setVisible(isPanelActive(BottomDockMode::spatialRadar));
    if (! m_floatingWireGrid) m_wireGridView.setVisible(isPanelActive(BottomDockMode::wireGrid));
    if (! m_floatingMasteringSuite) m_masteringSuiteView.setVisible(isPanelActive(BottomDockMode::masteringSuite));
    if (! m_floatingPitchBlob) m_pitchBlobView.setVisible(isPanelActive(BottomDockMode::pitchComping));
    if (! m_floatingSampleZone) m_sampleZoneView.setVisible(isPanelActive(BottomDockMode::sampleZones));

    // Update active label text
    juce::String dockName;
    switch (m_dockMode)
    {
    case BottomDockMode::mixer: dockName = "Mix: Console"; break;
    case BottomDockMode::deviceChain: dockName = "Mix: Device Rack"; break;
    case BottomDockMode::routingMatrix: dockName = "Mix: Patchbay"; break;
    case BottomDockMode::spatialRadar: dockName = "Mix: Spatial 3D"; break;
    case BottomDockMode::pianoRoll: dockName = "Edit: Piano Roll"; break;
    case BottomDockMode::channelRack: dockName = "Edit: Channel Rack"; break;
    case BottomDockMode::drumSampler: dockName = "Edit: Drum Sampler"; break;
    case BottomDockMode::sampleZones: dockName = "Edit: SFZ Zones"; break;
    case BottomDockMode::wireGrid: dockName = "Synth: DSP Wire Grid"; break;
    case BottomDockMode::modulationRack: dockName = "Synth: Modulators"; break;
    case BottomDockMode::masteringSuite: dockName = "Master: Suite"; break;
    case BottomDockMode::pitchComping: dockName = "Master: Pitch Blob"; break;
    }
    m_lblActiveDock.setText(dockName, juce::dontSendNotification);

    // Update category button styling
    const bool catMix = (m_dockMode == BottomDockMode::mixer || m_dockMode == BottomDockMode::deviceChain || m_dockMode == BottomDockMode::routingMatrix || m_dockMode == BottomDockMode::spatialRadar);
    const bool catEdit = (m_dockMode == BottomDockMode::pianoRoll || m_dockMode == BottomDockMode::channelRack || m_dockMode == BottomDockMode::drumSampler || m_dockMode == BottomDockMode::sampleZones);
    const bool catModular = (m_dockMode == BottomDockMode::wireGrid || m_dockMode == BottomDockMode::modulationRack);
    const bool catMaster = (m_dockMode == BottomDockMode::masteringSuite || m_dockMode == BottomDockMode::pitchComping);

    m_btnCatMix.setColour(juce::TextButton::buttonColourId, catMix ? juce::Colour::fromRGB(45, 95, 160) : juce::Colour::fromRGB(32, 38, 52));
    m_btnCatEdit.setColour(juce::TextButton::buttonColourId, catEdit ? juce::Colour::fromRGB(120, 60, 160) : juce::Colour::fromRGB(32, 38, 52));
    m_btnCatModular.setColour(juce::TextButton::buttonColourId, catModular ? juce::Colour::fromRGB(0, 150, 170) : juce::Colour::fromRGB(32, 38, 52));
    m_btnCatMaster.setColour(juce::TextButton::buttonColourId, catMaster ? juce::Colour::fromRGB(180, 110, 30) : juce::Colour::fromRGB(32, 38, 52));

    m_tabMixerButton.setColour(juce::TextButton::buttonColourId,
                               m_dockMode == BottomDockMode::mixer ? juce::Colour::fromRGB(45, 95, 145) : juce::Colour::fromRGB(28, 30, 36));
    m_tabPianoRollButton.setColour(juce::TextButton::buttonColourId,
                                   m_dockMode == BottomDockMode::pianoRoll ? juce::Colour::fromRGB(120, 60, 160) : juce::Colour::fromRGB(28, 30, 36));
    m_tabChannelRackButton.setColour(juce::TextButton::buttonColourId,
                                     m_dockMode == BottomDockMode::channelRack ? juce::Colour::fromRGB(190, 80, 50) : juce::Colour::fromRGB(28, 30, 36));
    m_tabDrumSamplerButton.setColour(juce::TextButton::buttonColourId,
                                     m_dockMode == BottomDockMode::drumSampler ? juce::Colour::fromRGB(210, 140, 40) : juce::Colour::fromRGB(28, 30, 36));
    m_tabRoutingMatrixButton.setColour(juce::TextButton::buttonColourId,
                                       m_dockMode == BottomDockMode::routingMatrix ? juce::Colour::fromRGB(110, 50, 150) : juce::Colour::fromRGB(28, 30, 36));
    m_tabModulationButton.setColour(juce::TextButton::buttonColourId,
                                    m_dockMode == BottomDockMode::modulationRack ? juce::Colour::fromRGB(45, 120, 95) : juce::Colour::fromRGB(28, 30, 36));
    m_tabSpatialRadarButton.setColour(juce::TextButton::buttonColourId,
                                      m_dockMode == BottomDockMode::spatialRadar ? juce::Colour::fromRGB(65, 145, 225) : juce::Colour::fromRGB(28, 30, 36));
    m_tabWireGridButton.setColour(juce::TextButton::buttonColourId,
                                  m_dockMode == BottomDockMode::wireGrid ? juce::Colour::fromRGB(0, 180, 200) : juce::Colour::fromRGB(28, 30, 36));
    m_tabMasteringButton.setColour(juce::TextButton::buttonColourId,
                                   m_dockMode == BottomDockMode::masteringSuite ? juce::Colour::fromRGB(200, 140, 40) : juce::Colour::fromRGB(28, 30, 36));
    m_tabPitchBlobButton.setColour(juce::TextButton::buttonColourId,
                                   m_dockMode == BottomDockMode::pitchComping ? juce::Colour::fromRGB(220, 80, 130) : juce::Colour::fromRGB(28, 30, 36));
    m_tabSampleZoneButton.setColour(juce::TextButton::buttonColourId,
                                    m_dockMode == BottomDockMode::sampleZones ? juce::Colour::fromRGB(40, 160, 100) : juce::Colour::fromRGB(28, 30, 36));
    resized();
}

void MainEditorView::detachPanel(DetachablePanel panel)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const bool hasMultiMonitor = displays.displays.size() > 1;

    switch (panel)
    {
    case DetachablePanel::mixer:
        if (! m_floatingMixer)
        {
            removeChildComponent(&m_mixerPanel);
            m_floatingMixer = std::make_unique<FloatingPanelWindow>(
                "Console Mixer - DAWg",
                &m_mixerPanel,
                [this] { reDockPanel(DetachablePanel::mixer); });

            if (hasMultiMonitor)
            {
                const auto secondScreen = displays.displays[1].userBounds.toNearestInt();
                m_floatingMixer->setBounds(secondScreen.reduced(24, 24));
            }
            else
            {
                m_floatingMixer->setBounds(80, 80, 960, 520);
            }
            m_floatingMixer->setVisible(true);
            m_tabMixerButton.setButtonText("Mixer [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::mixer ? BottomDockMode::pianoRoll : m_dockMode);
        }
        break;

    case DetachablePanel::deviceChain:
        if (! m_floatingDeviceRack)
        {
            removeChildComponent(&m_deviceRackView);
            m_floatingDeviceRack = std::make_unique<FloatingPanelWindow>(
                "Device Rack - DAWg",
                &m_deviceRackView,
                [this] { reDockPanel(DetachablePanel::deviceChain); });

            if (hasMultiMonitor)
            {
                const auto secondScreen = displays.displays[1].userBounds.toNearestInt();
                m_floatingDeviceRack->setBounds(secondScreen.reduced(24, 24));
            }
            else
            {
                m_floatingDeviceRack->setBounds(80, 80, 840, 360);
            }
            m_floatingDeviceRack->setVisible(true);
            setDockMode(m_dockMode == BottomDockMode::deviceChain ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::pianoRoll:
        if (! m_floatingPianoRoll)
        {
            removeChildComponent(&m_pianoRollView);
            m_floatingPianoRoll = std::make_unique<FloatingPanelWindow>(
                "Piano Roll - DAWg",
                &m_pianoRollView,
                [this] { reDockPanel(DetachablePanel::pianoRoll); });

            if (hasMultiMonitor)
            {
                const auto secondScreen = displays.displays[1].userBounds.toNearestInt();
                m_floatingPianoRoll->setBounds(secondScreen.reduced(24, 24));
            }
            else
            {
                m_floatingPianoRoll->setBounds(120, 120, 900, 480);
            }
            m_floatingPianoRoll->setVisible(true);
            m_tabPianoRollButton.setButtonText("Piano Roll [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::pianoRoll ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::channelRack:
        if (! m_floatingChannelRack)
        {
            removeChildComponent(&m_channelRackView);
            m_floatingChannelRack = std::make_unique<FloatingPanelWindow>(
                "Channel Rack - DAWg",
                &m_channelRackView,
                [this] { reDockPanel(DetachablePanel::channelRack); });

            m_floatingChannelRack->setBounds(160, 160, 800, 380);
            m_floatingChannelRack->setVisible(true);
            m_tabChannelRackButton.setButtonText("Channel Rack [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::channelRack ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::sessionMatrix:
        if (! m_floatingSessionMatrix)
        {
            removeChildComponent(&m_sessionMatrixView);
            m_floatingSessionMatrix = std::make_unique<FloatingPanelWindow>(
                "Session Matrix - DAWg",
                &m_sessionMatrixView,
                [this] { reDockPanel(DetachablePanel::sessionMatrix); });

            m_floatingSessionMatrix->setBounds(140, 140, 850, 480);
            m_floatingSessionMatrix->setVisible(true);
            setCenterViewMode(CenterViewMode::arrangement);
        }
        break;

    case DetachablePanel::drumSampler:
        if (! m_floatingDrumSampler)
        {
            removeChildComponent(&m_drumSamplerView);
            m_floatingDrumSampler = std::make_unique<FloatingPanelWindow>(
                "Drum Pad Sampler - DAWg",
                &m_drumSamplerView,
                [this] { reDockPanel(DetachablePanel::drumSampler); });

            m_floatingDrumSampler->setBounds(150, 150, 860, 420);
            m_floatingDrumSampler->setVisible(true);
            m_tabDrumSamplerButton.setButtonText("Drum Sampler [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::drumSampler ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::routingMatrix:
        if (! m_floatingRoutingMatrix)
        {
            removeChildComponent(&m_routingMatrixView);
            m_floatingRoutingMatrix = std::make_unique<FloatingPanelWindow>(
                "Routing Matrix & Patchbay - DAWg",
                &m_routingMatrixView,
                [this] { reDockPanel(DetachablePanel::routingMatrix); });

            m_floatingRoutingMatrix->setBounds(160, 160, 820, 400);
            m_floatingRoutingMatrix->setVisible(true);
            m_tabRoutingMatrixButton.setButtonText("Patchbay [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::routingMatrix ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::modulationRack:
        if (! m_floatingModulationRack)
        {
            removeChildComponent(&m_modulationRackView);
            m_floatingModulationRack = std::make_unique<FloatingPanelWindow>(
                "Modulation Rack & LFO System - DAWg",
                &m_modulationRackView,
                [this] { reDockPanel(DetachablePanel::modulationRack); });

            m_floatingModulationRack->setBounds(180, 180, 840, 420);
            m_floatingModulationRack->setVisible(true);
            m_tabModulationButton.setButtonText("Modulators [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::modulationRack ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::spatialRadar:
        if (! m_floatingSpatialRadar)
        {
            removeChildComponent(&m_spatialRadarView);
            m_floatingSpatialRadar = std::make_unique<FloatingPanelWindow>(
                "3D Spatial Audio & Binaural Soundstage - DAWg",
                &m_spatialRadarView,
                [this] { reDockPanel(DetachablePanel::spatialRadar); });

            m_floatingSpatialRadar->setBounds(200, 200, 720, 520);
            m_floatingSpatialRadar->setVisible(true);
            m_tabSpatialRadarButton.setButtonText("Spatial 3D [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::spatialRadar ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::wireGrid:
        if (! m_floatingWireGrid)
        {
            removeChildComponent(&m_wireGridView);
            m_floatingWireGrid = std::make_unique<FloatingPanelWindow>(
                "Modular DSP Wire Grid - DAWg",
                &m_wireGridView,
                [this] { reDockPanel(DetachablePanel::wireGrid); });

            m_floatingWireGrid->setBounds(220, 220, 860, 540);
            m_floatingWireGrid->setVisible(true);
            m_tabWireGridButton.setButtonText("Modular Grid [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::wireGrid ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::masteringSuite:
        if (! m_floatingMasteringSuite)
        {
            removeChildComponent(&m_masteringSuiteView);
            m_floatingMasteringSuite = std::make_unique<FloatingPanelWindow>(
                "Mastering Suite & Loudness Radar - DAWg",
                &m_masteringSuiteView,
                [this] { reDockPanel(DetachablePanel::masteringSuite); });

            m_floatingMasteringSuite->setBounds(240, 240, 880, 520);
            m_floatingMasteringSuite->setVisible(true);
            m_tabMasteringButton.setButtonText("Mastering [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::masteringSuite ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::pitchComping:
        if (! m_floatingPitchBlob)
        {
            removeChildComponent(&m_pitchBlobView);
            m_floatingPitchBlob = std::make_unique<FloatingPanelWindow>(
                "Pitch Blob & Swipe Comping - DAWg",
                &m_pitchBlobView,
                [this] { reDockPanel(DetachablePanel::pitchComping); });

            m_floatingPitchBlob->setBounds(250, 250, 880, 520);
            m_floatingPitchBlob->setVisible(true);
            m_tabPitchBlobButton.setButtonText("Pitch/Blob [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::pitchComping ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::sampleZones:
        if (! m_floatingSampleZone)
        {
            removeChildComponent(&m_sampleZoneView);
            m_floatingSampleZone = std::make_unique<FloatingPanelWindow>(
                "SFZ Sample Zone Mapping Editor - DAWg",
                &m_sampleZoneView,
                [this] { reDockPanel(DetachablePanel::sampleZones); });

            m_floatingSampleZone->setBounds(260, 260, 920, 480);
            m_floatingSampleZone->setVisible(true);
            m_tabSampleZoneButton.setButtonText("SFZ Zones [DOCK]");
            setDockMode(m_dockMode == BottomDockMode::sampleZones ? BottomDockMode::mixer : m_dockMode);
        }
        break;

    case DetachablePanel::browser:
        break;
    }

    resized();
}

void MainEditorView::reDockPanel(DetachablePanel panel)
{
    switch (panel)
    {
    case DetachablePanel::mixer:
        if (m_floatingMixer)
        {
            m_floatingMixer.reset();
            addChildComponent(m_mixerPanel);
            m_tabMixerButton.setButtonText("Mixer Console [M]");
            setDockMode(BottomDockMode::mixer);
        }
        break;

    case DetachablePanel::deviceChain:
        if (m_floatingDeviceRack)
        {
            m_floatingDeviceRack.reset();
            addChildComponent(m_deviceRackView);
            setDockMode(BottomDockMode::deviceChain);
        }
        break;

    case DetachablePanel::pianoRoll:
        if (m_floatingPianoRoll)
        {
            m_floatingPianoRoll.reset();
            addChildComponent(m_pianoRollView);
            m_tabPianoRollButton.setButtonText("Piano Roll [P]");
            setDockMode(BottomDockMode::pianoRoll);
        }
        break;

    case DetachablePanel::channelRack:
        if (m_floatingChannelRack)
        {
            m_floatingChannelRack.reset();
            addChildComponent(m_channelRackView);
            m_tabChannelRackButton.setButtonText("Channel Rack [R]");
            setDockMode(BottomDockMode::channelRack);
        }
        break;

    case DetachablePanel::sessionMatrix:
        if (m_floatingSessionMatrix)
        {
            m_floatingSessionMatrix.reset();
            addChildComponent(m_sessionMatrixView);
            setCenterViewMode(CenterViewMode::sessionView);
        }
        break;

    case DetachablePanel::drumSampler:
        if (m_floatingDrumSampler)
        {
            m_floatingDrumSampler.reset();
            addChildComponent(m_drumSamplerView);
            m_tabDrumSamplerButton.setButtonText("Drum Sampler [D]");
            setDockMode(BottomDockMode::drumSampler);
        }
        break;

    case DetachablePanel::routingMatrix:
        if (m_floatingRoutingMatrix)
        {
            m_floatingRoutingMatrix.reset();
            addChildComponent(m_routingMatrixView);
            m_tabRoutingMatrixButton.setButtonText("Patchbay [X]");
            setDockMode(BottomDockMode::routingMatrix);
        }
        break;

    case DetachablePanel::modulationRack:
        if (m_floatingModulationRack)
        {
            m_floatingModulationRack.reset();
            addChildComponent(m_modulationRackView);
            m_tabModulationButton.setButtonText("Modulators [O]");
            setDockMode(BottomDockMode::modulationRack);
        }
        break;

    case DetachablePanel::spatialRadar:
        if (m_floatingSpatialRadar)
        {
            m_floatingSpatialRadar.reset();
            addChildComponent(m_spatialRadarView);
            m_tabSpatialRadarButton.setButtonText("Spatial 3D [Z]");
            setDockMode(BottomDockMode::spatialRadar);
        }
        break;

    case DetachablePanel::wireGrid:
        if (m_floatingWireGrid)
        {
            m_floatingWireGrid.reset();
            addChildComponent(m_wireGridView);
            m_tabWireGridButton.setButtonText("Modular Grid [G]");
            setDockMode(BottomDockMode::wireGrid);
        }
        break;

    case DetachablePanel::masteringSuite:
        if (m_floatingMasteringSuite)
        {
            m_floatingMasteringSuite.reset();
            addChildComponent(m_masteringSuiteView);
            m_tabMasteringButton.setButtonText("Mastering [U]");
            setDockMode(BottomDockMode::masteringSuite);
        }
        break;

    case DetachablePanel::pitchComping:
        if (m_floatingPitchBlob)
        {
            m_floatingPitchBlob.reset();
            addChildComponent(m_pitchBlobView);
            m_tabPitchBlobButton.setButtonText("Pitch/Blob [H]");
            setDockMode(BottomDockMode::pitchComping);
        }
        break;

    case DetachablePanel::sampleZones:
        if (m_floatingSampleZone)
        {
            m_floatingSampleZone.reset();
            addChildComponent(m_sampleZoneView);
            m_tabSampleZoneButton.setButtonText("SFZ Zones [K]");
            setDockMode(BottomDockMode::sampleZones);
        }
        break;

    case DetachablePanel::browser:
        break;
    }

    resized();
}

void MainEditorView::reDockAllPanels()
{
    reDockPanel(DetachablePanel::mixer);
    reDockPanel(DetachablePanel::deviceChain);
    reDockPanel(DetachablePanel::pianoRoll);
    reDockPanel(DetachablePanel::channelRack);
    reDockPanel(DetachablePanel::sessionMatrix);
    reDockPanel(DetachablePanel::drumSampler);
    reDockPanel(DetachablePanel::routingMatrix);
    reDockPanel(DetachablePanel::modulationRack);
    reDockPanel(DetachablePanel::spatialRadar);
    reDockPanel(DetachablePanel::wireGrid);
    reDockPanel(DetachablePanel::masteringSuite);
    reDockPanel(DetachablePanel::pitchComping);
    reDockPanel(DetachablePanel::sampleZones);
}

bool MainEditorView::isPanelDetached(DetachablePanel panel) const noexcept
{
    switch (panel)
    {
    case DetachablePanel::mixer: return m_floatingMixer != nullptr;
    case DetachablePanel::deviceChain: return m_floatingDeviceRack != nullptr;
    case DetachablePanel::pianoRoll: return m_floatingPianoRoll != nullptr;
    case DetachablePanel::channelRack: return m_floatingChannelRack != nullptr;
    case DetachablePanel::sessionMatrix: return m_floatingSessionMatrix != nullptr;
    case DetachablePanel::drumSampler: return m_floatingDrumSampler != nullptr;
    case DetachablePanel::routingMatrix: return m_floatingRoutingMatrix != nullptr;
    case DetachablePanel::modulationRack: return m_floatingModulationRack != nullptr;
    case DetachablePanel::spatialRadar: return m_floatingSpatialRadar != nullptr;
    case DetachablePanel::wireGrid: return m_floatingWireGrid != nullptr;
    case DetachablePanel::masteringSuite: return m_floatingMasteringSuite != nullptr;
    case DetachablePanel::pitchComping: return m_floatingPitchBlob != nullptr;
    case DetachablePanel::sampleZones: return m_floatingSampleZone != nullptr;
    case DetachablePanel::browser: return false;
    }
    return false;
}

void MainEditorView::toggleMaximizeDock()
{
    m_dockMaximized = ! m_dockMaximized;
    m_maximizeDockButton.setColour(juce::TextButton::buttonColourId,
                                  m_dockMaximized ? juce::Colour::fromRGB(45, 95, 140) : juce::Colour::fromRGB(28, 30, 36));
    resized();
}

void MainEditorView::applyWorkspacePreset(WorkspacePreset preset)
{
    auto resetPresetButtons = [this] {
        const auto idleCol = juce::Colour::fromRGB(32, 35, 44);
        const auto idleTxt = juce::Colour::fromRGB(190, 195, 210);
        m_btnWsArrangement.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsArrangement.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsBeatmaker.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsBeatmaker.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsRecording.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsRecording.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsMixing.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsMixing.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsModular.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsModular.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsMastering.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsMastering.setColour(juce::TextButton::textColourOffId, idleTxt);
        m_btnWsLive.setColour(juce::TextButton::buttonColourId, idleCol);
        m_btnWsLive.setColour(juce::TextButton::textColourOffId, idleTxt);
    };
    auto highlightBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 95, 160));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    };

    resetPresetButtons();

    switch (preset)
    {
    case WorkspacePreset::songArrangement:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        m_inspectorWidth = 210;
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 220;
        m_dockMaximized = false;
        if (m_browserVisible)
            togglePluginBrowser();
        highlightBtn(m_btnWsArrangement);
        break;

    case WorkspacePreset::patternLabBeatmaker:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        setDockMode(BottomDockMode::drumSampler);
        m_dockHeight = 280;
        m_dockMaximized = false;
        if (! m_browserVisible)
        {
            togglePluginBrowser();
            setSidebarTab(SidebarTab::samples);
        }
        m_sidebarWidth = 300;
        highlightBtn(m_btnWsBeatmaker);
        break;

    case WorkspacePreset::recordingComping:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        m_inspectorWidth = 260;
        setDockMode(BottomDockMode::pitchComping);
        m_dockHeight = 280;
        m_dockMaximized = false;
        if (m_browserVisible)
            togglePluginBrowser();
        highlightBtn(m_btnWsRecording);
        break;

    case WorkspacePreset::mixingConsole:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 380;
        m_dockMaximized = false;
        if (m_browserVisible)
            togglePluginBrowser();
        highlightBtn(m_btnWsMixing);
        break;

    case WorkspacePreset::soundDesignModular:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        setDockMode(BottomDockMode::wireGrid);
        m_dockHeight = 360;
        m_dockMaximized = false;
        highlightBtn(m_btnWsModular);
        break;

    case WorkspacePreset::masteringSuite:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = false;
        m_trackInspector.setVisible(false);
        m_inspectorSplitter.setVisible(false);
        setDockMode(BottomDockMode::masteringSuite);
        m_dockHeight = 380;
        m_dockMaximized = false;
        if (m_browserVisible)
            togglePluginBrowser();
        highlightBtn(m_btnWsMastering);
        break;

    case WorkspacePreset::livePerformanceRig:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::sessionView);
        m_inspectorVisible = false;
        m_trackInspector.setVisible(false);
        m_inspectorSplitter.setVisible(false);
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 240;
        m_dockMaximized = false;
        highlightBtn(m_btnWsLive);
        break;

    case WorkspacePreset::dualScreenStudio:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        m_inspectorVisible = true;
        m_trackInspector.setVisible(true);
        m_inspectorSplitter.setVisible(true);
        setDockMode(BottomDockMode::pianoRoll);
        detachPanel(DetachablePanel::mixer);
        m_dockHeight = 260;
        m_dockMaximized = false;
        break;
    }
    resized();
}

void MainEditorView::saveCurrentWorkspace()
{
    WorkspaceLayoutState state;
    state.presetName = "Custom User Preset";
    state.dockMode = static_cast<std::uint8_t>(m_dockMode);
    state.secondaryDockMode = static_cast<std::uint8_t>(m_secondaryDockMode);
    state.isDualDockEnabled = m_dualDockEnabled;
    state.dockHeight = m_dockHeight;
    state.isDockMaximized = m_dockMaximized;
    state.isInspectorVisible = m_inspectorVisible;
    state.inspectorWidth = m_inspectorWidth;
    state.centerViewMode = (m_centerViewMode == CenterViewMode::arrangement ? 0 : 1);
    state.detachedMixer = isPanelDetached(DetachablePanel::mixer);
    state.detachedPianoRoll = isPanelDetached(DetachablePanel::pianoRoll);
    state.detachedModular = isPanelDetached(DetachablePanel::wireGrid);
    state.detachedMastering = isPanelDetached(DetachablePanel::masteringSuite);

    const auto prefDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DAWg");
    if (! prefDir.exists()) prefDir.createDirectory();
    const auto filePath = prefDir.getChildFile("custom_workspace.json").getFullPathName().toStdString();
    WorkspaceLayoutManager::saveToFile(filePath, state);
}

void MainEditorView::setCanvasScale(float scaleFactor)
{
    m_canvasScale = std::clamp(scaleFactor, 0.75f, 2.50f);
    juce::Desktop::getInstance().setGlobalScaleFactor(m_canvasScale);
}

void MainEditorView::zoomIn()
{
    setCanvasScale(m_canvasScale + 0.25f);
}

void MainEditorView::zoomOut()
{
    setCanvasScale(m_canvasScale - 0.25f);
}

void MainEditorView::resetZoom()
{
    setCanvasScale(1.0f);
}

void MainEditorView::toggleCenterViewMode()
{
    setCenterViewMode(m_centerViewMode == CenterViewMode::arrangement ? CenterViewMode::sessionView : CenterViewMode::arrangement);
}

void MainEditorView::setCenterViewMode(CenterViewMode mode)
{
    m_centerViewMode = mode;
    const bool isArrangement = (m_centerViewMode == CenterViewMode::arrangement);
    m_timelineView.setVisible(isArrangement);
    m_sessionMatrixView.setVisible(! isArrangement);
    resized();
}

void MainEditorView::setViewModel(EditorViewModel model)
{
    m_model = std::move(model);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_sessionMatrixView.setViewModel(m_model);
    m_trackInspector.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
    m_deviceRackView.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
    m_channelRackView.setViewModel(m_model);
    m_drumSamplerView.setViewModel(m_model);
    m_routingMatrixView.setViewModel(m_model);
    m_spatialRadarView.setViewModel(m_model);
    m_wireGridView.setViewModel(m_model);
    m_masteringSuiteView.setViewModel(m_model);
    VideoPlaybackWindow::updatePlayhead(m_model);
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model.project = std::move(snapshot);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_sessionMatrixView.setViewModel(m_model);
    m_trackInspector.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
    m_deviceRackView.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
    m_channelRackView.setViewModel(m_model);
    m_drumSamplerView.setViewModel(m_model);
    m_routingMatrixView.setViewModel(m_model);
    m_spatialRadarView.setViewModel(m_model);
    m_wireGridView.setViewModel(m_model);
    m_masteringSuiteView.setViewModel(m_model);
    VideoPlaybackWindow::updatePlayhead(m_model);
}

void MainEditorView::setWaveformCache(media::WaveformCache* cache)
{
    m_timelineView.setWaveformCache(cache);
}

// ============================================================================
// MenuBarModel implementation
// ============================================================================

juce::StringArray MainEditorView::getMenuBarNames()
{
    return {"File", "Edit", "Track", "View", "Workspace", "Options", "Help"};
}

juce::PopupMenu MainEditorView::getMenuForIndex(int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;
    switch (menuIndex)
    {
    case 0: // File
        menu.addItem(cmdNewProject, "New Project", true, false);
        {
            juce::PopupMenu tmplMenu;
            tmplMenu.addItem(cmdTemplateModernTrap, "Modern Trap / Beatmaker");
            tmplMenu.addItem(cmdTemplateSynthwave, "Synthwave / Cyberpunk");
            tmplMenu.addItem(cmdTemplateAcousticRock, "Multi-Mic Acoustic Rock");
            tmplMenu.addItem(cmdTemplateMastering, "Mastering Session");
            menu.addSubMenu("New from Template", tmplMenu);
        }
        menu.addItem(cmdOpenProject, "Open Project...  (Ctrl+O)", true, false);
        menu.addItem(cmdSaveProject, "Save Project  (Ctrl+S)", true, false);
        menu.addItem(cmdSaveProjectAs, "Save Project As...  (Ctrl+Shift+S)", true, false);
        menu.addSeparator();
        menu.addItem(cmdExportBounce, "Export Master Bounce...  (Ctrl+B)", true, false);
        menu.addItem(cmdExportStems, "Export Multi-Track Stems...", true, false);
        menu.addItem(cmdConsolidateProject, "Consolidate & Archive Project...", true, false);
        menu.addSeparator();
        menu.addItem(cmdProjectAudioPool, "Project Audio Pool & Relinker...", true, false);
        menu.addSeparator();
        menu.addItem(cmdExit, "Exit  (Ctrl+Q)", true, false);
        break;

    case 1: // Edit
        menu.addItem(cmdUndo, "Undo  (Ctrl+Z)", m_model.canUndo, false);
        menu.addItem(cmdRedo, "Redo  (Ctrl+Y)", m_model.canRedo, false);
        menu.addSeparator();
        menu.addItem(cmdSplitClip, "Split Clip at Playhead  (S)", true, false);
        menu.addItem(cmdDuplicateClip, "Duplicate Selected Clip  (Ctrl+D)", true, false);
        menu.addItem(cmdDeleteClip, "Delete Selected Clip  (Del)", true, false);
        break;

    case 2: // Track
        menu.addItem(cmdAddAudioTrack, "Add Audio Track  (Ctrl+T)", true, false);
        menu.addItem(cmdAddSynthTrack, "Add Synth Track  (Ctrl+Shift+T)", true, false);
        menu.addItem(cmdAddFolderTrack, "Add Folder Track", true, false);
        menu.addSeparator();
        menu.addItem(cmdAddPlateReverb, "Add Plate Reverb Aux Return", true, false);
        menu.addItem(cmdAddStereoDelay, "Add Stereo Delay Aux Return", true, false);
        menu.addItem(cmdAddDrumBus, "Add Drum Submix Bus", true, false);
        break;

    case 3: // View
        menu.addItem(cmdToggleMixer, "Show Console Mixer  (M)", true, m_dockMode == BottomDockMode::mixer && ! m_floatingMixer);
        menu.addItem(cmdTogglePianoRoll, "Show Piano Roll  (P)", true, m_dockMode == BottomDockMode::pianoRoll && ! m_floatingPianoRoll);
        menu.addItem(cmdToggleChannelRack, "Show Channel Rack  (R)", true, m_dockMode == BottomDockMode::channelRack && ! m_floatingChannelRack);
        menu.addSeparator();
        menu.addItem(cmdToggleSessionView, "Toggle Session / Arrangement  (Tab)", true, m_centerViewMode == CenterViewMode::sessionView);
        menu.addItem(cmdToggleAutomation, "Toggle Automation Lanes  (A)", true, m_timelineView.showAutomation());
        menu.addItem(cmdToggleBrowser, "Toggle Browser Panel  (B)", true, m_browserVisible);
        menu.addSeparator();
        menu.addItem(cmdDetachMixer, "Detach Mixer to Window", true, isPanelDetached(DetachablePanel::mixer));
        menu.addItem(cmdDetachPianoRoll, "Detach Piano Roll to Window", true, isPanelDetached(DetachablePanel::pianoRoll));
        menu.addItem(cmdDetachChannelRack, "Detach Channel Rack to Window", true, isPanelDetached(DetachablePanel::channelRack));
        menu.addItem(cmdReDockAll, "Re-Dock All Floating Panels  (Dock)", true, false);
        menu.addItem(cmdToggleMaximizeDock, "Maximize Bottom Dock  (F11)", true, m_dockMaximized);
        menu.addSeparator();
        menu.addItem(cmdOpenVideoMonitor, "Video Picture Monitor & SMPTE Sync...", true, VideoPlaybackWindow::isOpen());
        menu.addItem(cmdOpenSpectralRepair, "Spectral Audio Repair & De-Hum...", true, false);
        menu.addSeparator();
        menu.addItem(cmdZoomIn, "Zoom In  (Ctrl + =)", true, false);
        menu.addItem(cmdZoomOut, "Zoom Out  (Ctrl + -)", true, false);
        menu.addItem(cmdZoomReset, "Reset Zoom 100%  (Ctrl + 0)", true, false);
        {
            juce::PopupMenu zoomMenu;
            zoomMenu.addItem(cmdZoom75, "75%", true, std::abs(m_canvasScale - 0.75f) < 0.05f);
            zoomMenu.addItem(cmdZoom100, "100% (Default)", true, std::abs(m_canvasScale - 1.0f) < 0.05f);
            zoomMenu.addItem(cmdZoom125, "125%", true, std::abs(m_canvasScale - 1.25f) < 0.05f);
            zoomMenu.addItem(cmdZoom150, "150%", true, std::abs(m_canvasScale - 1.50f) < 0.05f);
            zoomMenu.addItem(cmdZoom200, "200% (HiDPI 4K)", true, std::abs(m_canvasScale - 2.0f) < 0.05f);
            menu.addSubMenu("UI Scale", zoomMenu);
        }
        break;

    case 4: // Workspace
        menu.addItem(cmdWorkspaceDefault, "Default Arrangement Layout  (F1)", true, false);
        menu.addItem(cmdWorkspaceDualScreen, "Dual-Screen Studio Layout  (F2)", true, false);
        menu.addItem(cmdWorkspaceBeatmaker, "Beatmaker Focus Layout  (F3)", true, false);
        menu.addItem(cmdWorkspaceLive, "Live Performance Rig Layout  (F4)", true, false);
        menu.addItem(cmdWorkspaceMastering, "Mastering Suite Layout", true, false);
        menu.addSeparator();
        menu.addItem(cmdSaveWorkspace, "Save Workspace Layout...  (Ctrl+Shift+L)", true, false);
        break;

    case 5: // Options
        menu.addItem(cmdAudioPreferences, "Audio Hardware & MIDI Preferences...", true, false);
        menu.addItem(cmdRescanPlugins, "Rescan VST3 & CLAP Plugins...", true, false);
        menu.addSeparator();
        menu.addItem(cmdToggleMetronome, "Metronome Click", true, m_model.project.tempo.metronomeEnabled);
        menu.addItem(cmdToggleLimiter, "Master Limiter Protection", true, m_model.masterLimiterEnabled);
        break;

    case 6: // Help
        menu.addItem(cmdShortcuts, "Keyboard Shortcuts Reference", true, false);
        menu.addItem(cmdAbout, "About DAWg", true, false);
        break;
    }
    return menu;
}

void MainEditorView::menuItemSelected(int menuItemID, int)
{
    switch (menuItemID)
    {
    case cmdNewProject:
        if (m_menuCallbacks.newProject) m_menuCallbacks.newProject();
        break;
    case cmdOpenProject:
        if (m_menuCallbacks.openProject) m_menuCallbacks.openProject();
        break;
    case cmdSaveProject:
        if (m_menuCallbacks.saveProject) m_menuCallbacks.saveProject();
        break;
    case cmdSaveProjectAs:
        if (m_menuCallbacks.saveProjectAs) m_menuCallbacks.saveProjectAs();
        break;
    case cmdExportBounce:
        if (m_menuCallbacks.exportBounce) m_menuCallbacks.exportBounce();
        break;
    case cmdExportStems:
        if (m_menuCallbacks.exportStems) m_menuCallbacks.exportStems();
        break;
    case cmdProjectAudioPool:
        if (m_menuCallbacks.showAudioPool) m_menuCallbacks.showAudioPool();
        break;
    case cmdExit:
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
        break;

    case cmdUndo:
        if (m_transportCallbacks.undo) m_transportCallbacks.undo();
        break;
    case cmdRedo:
        if (m_transportCallbacks.redo) m_transportCallbacks.redo();
        break;
    case cmdSplitClip:
        m_timelineView.splitSelectedClip();
        break;
    case cmdDuplicateClip:
        m_timelineView.duplicateSelectedClip();
        break;
    case cmdDeleteClip:
        m_timelineView.deleteSelectedClip();
        break;

    case cmdAddAudioTrack:
        if (m_commandHandler)
            m_commandHandler(project::AddAudioTrack {"Audio " + std::to_string(m_model.trackCount() + 1)});
        break;
    case cmdAddSynthTrack:
        if (m_commandHandler)
            m_commandHandler(project::AddInstrumentTrack {"Synth Lead"});
        break;
    case cmdAddFolderTrack:
        if (m_commandHandler)
            m_commandHandler(project::AddFolderTrack {"Folder Track"});
        break;
    case cmdAddPlateReverb:
        if (m_commandHandler)
            m_commandHandler(project::AddAuxReturnTrack {"Plate Reverb"});
        break;
    case cmdAddStereoDelay:
        if (m_commandHandler)
            m_commandHandler(project::AddAuxReturnTrack {"Stereo Delay"});
        break;
    case cmdAddDrumBus:
        if (m_commandHandler)
            m_commandHandler(project::AddSubmixGroupTrack {"Drum Bus"});
        break;

    case cmdToggleMixer:
        setDockMode(BottomDockMode::mixer);
        break;
    case cmdTogglePianoRoll:
        setDockMode(BottomDockMode::pianoRoll);
        break;
    case cmdToggleChannelRack:
        setDockMode(BottomDockMode::channelRack);
        break;
    case cmdToggleSessionView:
        toggleCenterViewMode();
        break;
    case cmdToggleAutomation:
        m_timelineView.toggleAutomation();
        break;
    case cmdToggleBrowser:
        togglePluginBrowser();
        break;

    case cmdDetachMixer:
        if (isPanelDetached(DetachablePanel::mixer)) reDockPanel(DetachablePanel::mixer);
        else detachPanel(DetachablePanel::mixer);
        break;
    case cmdDetachPianoRoll:
        if (isPanelDetached(DetachablePanel::pianoRoll)) reDockPanel(DetachablePanel::pianoRoll);
        else detachPanel(DetachablePanel::pianoRoll);
        break;
    case cmdDetachChannelRack:
        if (isPanelDetached(DetachablePanel::channelRack)) reDockPanel(DetachablePanel::channelRack);
        else detachPanel(DetachablePanel::channelRack);
        break;
    case cmdReDockAll:
        reDockAllPanels();
        break;
    case cmdToggleMaximizeDock:
        toggleMaximizeDock();
        break;

    case cmdZoomIn: zoomIn(); break;
    case cmdZoomOut: zoomOut(); break;
    case cmdZoomReset: resetZoom(); break;
    case cmdZoom75: setCanvasScale(0.75f); break;
    case cmdZoom100: setCanvasScale(1.00f); break;
    case cmdZoom125: setCanvasScale(1.25f); break;
    case cmdZoom150: setCanvasScale(1.50f); break;
    case cmdZoom200: setCanvasScale(2.00f); break;

    case cmdWorkspaceDefault: applyWorkspacePreset(WorkspacePreset::defaultArrangement); break;
    case cmdWorkspaceDualScreen: applyWorkspacePreset(WorkspacePreset::dualScreenStudio); break;
    case cmdWorkspaceBeatmaker: applyWorkspacePreset(WorkspacePreset::beatmakerFocus); break;
    case cmdWorkspaceLive: applyWorkspacePreset(WorkspacePreset::livePerformanceRig); break;
    case cmdWorkspaceMastering: applyWorkspacePreset(WorkspacePreset::masteringSuite); break;
    case cmdSaveWorkspace: saveCurrentWorkspace(); break;

    case cmdAudioPreferences:
        if (m_menuCallbacks.audioPreferences) m_menuCallbacks.audioPreferences();
        break;
    case cmdRescanPlugins:
        if (m_menuCallbacks.rescanPlugins) m_menuCallbacks.rescanPlugins();
        break;
    case cmdConsolidateProject:
        if (m_menuCallbacks.consolidateProject) m_menuCallbacks.consolidateProject();
        break;
    case cmdOpenVideoMonitor:
        VideoPlaybackWindow::open(m_model);
        break;
    case cmdOpenSpectralRepair:
        SpectralRepairDialog::open(m_model);
        break;
    case cmdTemplateModernTrap:
        if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("modernTrap");
        break;
    case cmdTemplateSynthwave:
        if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("synthwaveCyberpunk");
        break;
    case cmdTemplateAcousticRock:
        if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("multiMicAcousticRock");
        break;
    case cmdTemplateMastering:
        if (m_menuCallbacks.newFromTemplate) m_menuCallbacks.newFromTemplate("masteringSession");
        break;
    case cmdToggleMetronome:
        if (m_commandHandler)
        {
            m_commandHandler(project::SetProjectTempo {
                m_model.project.tempo.bpm,
                m_model.project.tempo.timeSigNumerator,
                m_model.project.tempo.timeSigDenominator,
                ! m_model.project.tempo.metronomeEnabled
            });
        }
        break;
    case cmdToggleLimiter:
        if (m_transportCallbacks.setMasterLimiter)
            m_transportCallbacks.setMasterLimiter(! m_model.masterLimiterEnabled);
        break;

    case cmdShortcuts:
        if (m_menuCallbacks.showShortcuts) m_menuCallbacks.showShortcuts();
        break;
    case cmdAbout:
        if (m_menuCallbacks.showAbout) m_menuCallbacks.showAbout();
        break;
    }
}

bool MainEditorView::keyPressed(const juce::KeyPress& key)
{
    // Command Palette (Ctrl+K or Cmd+K)
    if ((key.getModifiers().isCtrlDown() || key.getModifiers().isCommandDown()) && (key.getKeyCode() == 'K' || key.getKeyCode() == 'k'))
    {
        openCommandPalette();
        return true;
    }

    // Track Inspector Toggle [I]
    if (! key.getModifiers().isCtrlDown() && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown()
        && (key.getKeyCode() == 'I' || key.getKeyCode() == 'i'))
    {
        toggleTrackInspector();
        return true;
    }

    // Number keys 1..7 for Workspace Presets
    if (! key.getModifiers().isCtrlDown() && ! key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown())
    {
        if (key.getKeyCode() == '1') { applyWorkspacePreset(WorkspacePreset::songArrangement); return true; }
        if (key.getKeyCode() == '2') { applyWorkspacePreset(WorkspacePreset::patternLabBeatmaker); return true; }
        if (key.getKeyCode() == '3') { applyWorkspacePreset(WorkspacePreset::recordingComping); return true; }
        if (key.getKeyCode() == '4') { applyWorkspacePreset(WorkspacePreset::mixingConsole); return true; }
        if (key.getKeyCode() == '5') { applyWorkspacePreset(WorkspacePreset::soundDesignModular); return true; }
        if (key.getKeyCode() == '6') { applyWorkspacePreset(WorkspacePreset::masteringSuite); return true; }
        if (key.getKeyCode() == '7') { applyWorkspacePreset(WorkspacePreset::livePerformanceRig); return true; }
    }

    // Function key workspace presets (F1-F5 primary studio modes)
    if (key.getKeyCode() == juce::KeyPress::F1Key)
    {
        applyWorkspacePreset(WorkspacePreset::songArrangement);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F2Key)
    {
        applyWorkspacePreset(WorkspacePreset::mixingConsole);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F3Key)
    {
        applyWorkspacePreset(WorkspacePreset::patternLabBeatmaker); // Edit / Piano Roll
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F4Key)
    {
        applyWorkspacePreset(WorkspacePreset::soundDesignModular);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F5Key)
    {
        applyWorkspacePreset(WorkspacePreset::masteringSuite);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F6Key)
    {
        applyWorkspacePreset(WorkspacePreset::livePerformanceRig);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F7Key)
    {
        applyWorkspacePreset(WorkspacePreset::dualScreenStudio);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F11Key)
    {
        toggleMaximizeDock();
        return true;
    }

    // Zoom shortcuts
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == '=' || key.getKeyCode() == '+'))
    {
        zoomIn();
        return true;
    }
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == '-' || key.getKeyCode() == '_'))
    {
        zoomOut();
        return true;
    }
    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == '0')
    {
        resetZoom();
        return true;
    }

    // Global hotkeys
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        toggleCenterViewMode();
        return true;
    }
    if (key.getKeyCode() == 'M' || key.getKeyCode() == 'm')
    {
        setDockMode(BottomDockMode::mixer);
        return true;
    }
    if (key.getKeyCode() == 'C' || key.getKeyCode() == 'c')
    {
        setDockMode(BottomDockMode::deviceChain);
        return true;
    }
    if (key.getKeyCode() == 'P' || key.getKeyCode() == 'p')
    {
        setDockMode(BottomDockMode::pianoRoll);
        return true;
    }
    if (key.getKeyCode() == 'R' || key.getKeyCode() == 'r')
    {
        setDockMode(BottomDockMode::channelRack);
        return true;
    }
    if (key.getKeyCode() == 'D' || key.getKeyCode() == 'd')
    {
        setDockMode(BottomDockMode::drumSampler);
        return true;
    }
    if (key.getKeyCode() == 'X' || key.getKeyCode() == 'x')
    {
        setDockMode(BottomDockMode::routingMatrix);
        return true;
    }
    if (key.getKeyCode() == 'O' || key.getKeyCode() == 'o')
    {
        setDockMode(BottomDockMode::modulationRack);
        return true;
    }
    if (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
    {
        setDockMode(BottomDockMode::spatialRadar);
        return true;
    }
    if (key.getKeyCode() == 'G' || key.getKeyCode() == 'g')
    {
        setDockMode(BottomDockMode::wireGrid);
        return true;
    }
    if (key.getKeyCode() == 'U' || key.getKeyCode() == 'u')
    {
        setDockMode(BottomDockMode::masteringSuite);
        return true;
    }
    if (key.getKeyCode() == 'A' || key.getKeyCode() == 'a')
    {
        m_timelineView.toggleAutomation();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && (key.getKeyCode() == 'L' || key.getKeyCode() == 'l'))
    {
        saveCurrentWorkspace();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'O' || key.getKeyCode() == 'o'))
    {
        if (m_menuCallbacks.openProject) { m_menuCallbacks.openProject(); return true; }
    }
    if (key.getModifiers().isCtrlDown() && ! key.getModifiers().isShiftDown() && (key.getKeyCode() == 'S' || key.getKeyCode() == 's'))
    {
        if (m_menuCallbacks.saveProject) { m_menuCallbacks.saveProject(); return true; }
    }
    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && (key.getKeyCode() == 'S' || key.getKeyCode() == 's'))
    {
        if (m_menuCallbacks.saveProjectAs) { m_menuCallbacks.saveProjectAs(); return true; }
    }
    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'B' || key.getKeyCode() == 'b'))
    {
        if (m_menuCallbacks.exportBounce) { m_menuCallbacks.exportBounce(); return true; }
    }
    if (key.getModifiers().isCtrlDown() && ! key.getModifiers().isShiftDown() && (key.getKeyCode() == 'T' || key.getKeyCode() == 't'))
    {
        if (m_commandHandler)
        {
            m_commandHandler(project::AddAudioTrack {"Audio " + std::to_string(m_model.trackCount() + 1)});
            return true;
        }
    }
    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown() && (key.getKeyCode() == 'T' || key.getKeyCode() == 't'))
    {
        if (m_commandHandler)
        {
            m_commandHandler(project::AddInstrumentTrack {"Synth Lead"});
            return true;
        }
    }
    if (! key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'B' || key.getKeyCode() == 'b'))
    {
        togglePluginBrowser();
        return true;
    }

    if (m_dockMode == BottomDockMode::pianoRoll && ! m_floatingPianoRoll)
    {
        if (m_pianoRollView.keyPressed(key))
            return true;
    }

    return false;
}

void MainEditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 25, 29));
}

void MainEditorView::resized()
{
    auto area = getLocalBounds();

    // 1. Top Application Menu Bar (24 px)
    m_menuBar.setBounds(area.removeFromTop(24));

    // 2. Workspace Preset Bar & HUD (28 px)
    auto wsBar = area.removeFromTop(28);
    m_btnToggleInspector.setBounds(wsBar.removeFromLeft(105).reduced(2, 2));
    m_btnCommandPalette.setBounds(wsBar.removeFromRight(100).reduced(2, 2));

    m_btnWsArrangement.setBounds(wsBar.removeFromLeft(85).reduced(2, 2));
    m_btnWsBeatmaker.setBounds(wsBar.removeFromLeft(95).reduced(2, 2));
    m_btnWsRecording.setBounds(wsBar.removeFromLeft(80).reduced(2, 2));
    m_btnWsMixing.setBounds(wsBar.removeFromLeft(70).reduced(2, 2));
    m_btnWsModular.setBounds(wsBar.removeFromLeft(85).reduced(2, 2));
    m_btnWsMastering.setBounds(wsBar.removeFromLeft(80).reduced(2, 2));
    m_btnWsLive.setBounds(wsBar.removeFromLeft(65).reduced(2, 2));
    m_btnWsSave.setBounds(wsBar.removeFromLeft(75).reduced(2, 2));

    // 3. Transport Header Bar (44 px)
    m_transportBar.setBounds(area.removeFromTop(44));

    // 4. Right Sidebar Plugin / Sample Browser with vertical drag splitter
    if (m_browserVisible && ! m_floatingBrowser)
    {
        auto sidebarArea = area.removeFromRight(m_sidebarWidth);
        m_sidebarSplitter.setVisible(true);
        m_sidebarSplitter.setBounds(area.removeFromRight(5));

        auto tabArea = sidebarArea.removeFromTop(24);
        m_sidebarTabPluginsButton.setBounds(tabArea.removeFromLeft(tabArea.getWidth() / 2).reduced(2, 2));
        m_sidebarTabSamplesButton.setBounds(tabArea.reduced(2, 2));

        if (m_pluginBrowser && m_sidebarTab == SidebarTab::plugins)
            m_pluginBrowser->setBounds(sidebarArea);
        if (m_sampleBrowser && m_sidebarTab == SidebarTab::samples)
            m_sampleBrowser->setBounds(sidebarArea);
    }
    else
    {
        m_sidebarSplitter.setVisible(false);
    }

    // 5. Left Track Inspector with vertical drag splitter
    if (m_inspectorVisible)
    {
        m_trackInspector.setVisible(true);
        m_trackInspector.setBounds(area.removeFromLeft(m_inspectorWidth));
        m_inspectorSplitter.setVisible(true);
        m_inspectorSplitter.setBounds(area.removeFromLeft(5));
    }
    else
    {
        m_trackInspector.setVisible(false);
        m_inspectorSplitter.setVisible(false);
    }

    // 6. Bottom Dock Area (Variable Height, Collapsible, or Maximized)
    const bool allDockedPanelsDetached = (m_floatingMixer && m_floatingPianoRoll && m_floatingChannelRack && m_floatingDrumSampler && m_floatingRoutingMatrix && m_floatingModulationRack && m_floatingSpatialRadar && m_floatingWireGrid && m_floatingMasteringSuite && m_floatingPitchBlob && m_floatingSampleZone);
    if (! allDockedPanelsDetached)
    {
        const int actualDockHeight = m_dockMaximized ? (area.getHeight() - 20) : m_dockHeight;
        auto bottomDockArea = area.removeFromBottom(std::clamp(actualDockHeight, 40, std::max(50, area.getHeight() + actualDockHeight - 40)));

        m_dockSplitter.setVisible(true);
        m_dockSplitter.setBounds(area.removeFromBottom(5));

        auto tabHeader = bottomDockArea.removeFromTop(26);
        m_btnCatMix.setBounds(tabHeader.removeFromLeft(75).reduced(2, 2));
        m_btnCatEdit.setBounds(tabHeader.removeFromLeft(75).reduced(2, 2));
        m_btnCatModular.setBounds(tabHeader.removeFromLeft(85).reduced(2, 2));
        m_btnCatMaster.setBounds(tabHeader.removeFromLeft(85).reduced(2, 2));
        m_lblActiveDock.setBounds(tabHeader.removeFromLeft(160).reduced(2, 2));

        m_maximizeDockButton.setBounds(tabHeader.removeFromRight(26).reduced(2, 2));
        m_detachDockButton.setBounds(tabHeader.removeFromRight(26).reduced(2, 2));
        m_btnDualDock.setBounds(tabHeader.removeFromRight(65).reduced(2, 2));
        m_tabBrowserButton.setBounds(tabHeader.removeFromRight(85).reduced(2, 2));

        if (m_dualDockEnabled)
        {
            const int totalW = bottomDockArea.getWidth();
            const int leftW = std::clamp(static_cast<int>(totalW * m_dockSplitRatio), 80, std::max(100, totalW - 80));
            auto leftPane = bottomDockArea.removeFromLeft(leftW);
            m_dualDockSplitter.setVisible(true);
            m_dualDockSplitter.setBounds(bottomDockArea.removeFromLeft(5));
            auto rightPane = bottomDockArea;

            layoutDockPanel(m_dockMode, leftPane);
            layoutDockPanel(m_secondaryDockMode, rightPane);
        }
        else
        {
            m_dualDockSplitter.setVisible(false);
            layoutDockPanel(m_dockMode, bottomDockArea);
        }
    }
    else
    {
        m_dockSplitter.setVisible(false);
    }

    // 7. Center View: Arrangement Timeline or Session Matrix Grid
    m_timelineView.setBounds(area);
    m_sessionMatrixView.setBounds(area);

    // 8. Command Palette Overlay (Covers entire local bounds)
    m_commandPalette.setBounds(getLocalBounds());
}

void MainEditorView::loadVocalStem(const float* samples, const std::size_t sampleCount, const double sampleRate)
{
    setDockMode(BottomDockMode::pitchComping);
    m_pitchBlobView.loadVocalStem(samples, sampleCount, sampleRate);
}

} // namespace dawg::ui
