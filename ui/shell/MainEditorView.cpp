#include "MainEditorView.h"
#include "PluginWindow.h"

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
        .redo = m_transportCallbacks.redo
    }, waveformCache)
    , m_sessionMatrixView(m_commandHandler, m_transportCallbacks.audition)
    , m_mixerPanel(m_commandHandler, m_transportCallbacks.setMasterLimiter)
    , m_pianoRollView(m_commandHandler)
    , m_channelRackView(m_commandHandler, m_transportCallbacks.audition)
    , m_drumSamplerView(m_commandHandler, m_transportCallbacks.audition)
    , m_routingMatrixView(m_commandHandler, nullptr)
    , m_modulationRackView()
    , m_spatialRadarView()
    , m_wireGridView()
    , m_masteringSuiteView()
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

    // Dock switcher buttons
    m_tabMixerButton.setClickingTogglesState(false);
    m_tabMixerButton.onClick = [this] { setDockMode(BottomDockMode::mixer); };
    addAndMakeVisible(m_tabMixerButton);

    m_tabPianoRollButton.setClickingTogglesState(false);
    m_tabPianoRollButton.onClick = [this] { setDockMode(BottomDockMode::pianoRoll); };
    addAndMakeVisible(m_tabPianoRollButton);

    m_tabChannelRackButton.setClickingTogglesState(false);
    m_tabChannelRackButton.onClick = [this] { setDockMode(BottomDockMode::channelRack); };
    addAndMakeVisible(m_tabChannelRackButton);

    m_tabDrumSamplerButton.setClickingTogglesState(false);
    m_tabDrumSamplerButton.onClick = [this] { setDockMode(BottomDockMode::drumSampler); };
    addAndMakeVisible(m_tabDrumSamplerButton);

    m_tabRoutingMatrixButton.setClickingTogglesState(false);
    m_tabRoutingMatrixButton.onClick = [this] { setDockMode(BottomDockMode::routingMatrix); };
    addAndMakeVisible(m_tabRoutingMatrixButton);

    m_tabModulationButton.setClickingTogglesState(false);
    m_tabModulationButton.onClick = [this] { setDockMode(BottomDockMode::modulationRack); };
    addAndMakeVisible(m_tabModulationButton);

    m_tabSpatialRadarButton.setClickingTogglesState(false);
    m_tabSpatialRadarButton.onClick = [this] { setDockMode(BottomDockMode::spatialRadar); };
    addAndMakeVisible(m_tabSpatialRadarButton);

    m_tabWireGridButton.setClickingTogglesState(false);
    m_tabWireGridButton.onClick = [this] { setDockMode(BottomDockMode::wireGrid); };
    addAndMakeVisible(m_tabWireGridButton);

    m_tabMasteringButton.setClickingTogglesState(false);
    m_tabMasteringButton.onClick = [this] { setDockMode(BottomDockMode::masteringSuite); };
    addAndMakeVisible(m_tabMasteringButton);

    m_tabBrowserButton.setClickingTogglesState(false);
    m_tabBrowserButton.onClick = [this] { togglePluginBrowser(); };
    addAndMakeVisible(m_tabBrowserButton);

    // Detach and Maximize dock controls
    m_detachDockButton.setTooltip("Detach active panel to floating window (Multi-Screen)");
    m_detachDockButton.onClick = [this] {
        if (m_dockMode == BottomDockMode::mixer)
            detachPanel(DetachablePanel::mixer);
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
    };
    addAndMakeVisible(m_detachDockButton);

    m_maximizeDockButton.setTooltip("Maximize / Restore Bottom Dock [F11]");
    m_maximizeDockButton.onClick = [this] { toggleMaximizeDock(); };
    addAndMakeVisible(m_maximizeDockButton);

    m_dockSplitter.setOnDoubleClick([this] { toggleMaximizeDock(); });
    addAndMakeVisible(m_dockSplitter);
    addChildComponent(m_sidebarSplitter);

    addAndMakeVisible(m_mixerPanel);
    addChildComponent(m_pianoRollView);
    addChildComponent(m_channelRackView);
    addChildComponent(m_drumSamplerView);
    addChildComponent(m_routingMatrixView);
    addChildComponent(m_modulationRackView);
    addChildComponent(m_spatialRadarView);
    addChildComponent(m_wireGridView);
    addChildComponent(m_masteringSuiteView);

    m_pluginBrowser = std::make_unique<PluginBrowserPanel>(
        [this](const plugins::PluginDescriptor& plugin, bool createNewTrack) {
            PluginWindow::open(plugin.name);
            if (createNewTrack)
            {
                if (plugin.isInstrument)
                    m_commandHandler(project::AddInstrumentTrack { plugin.name });
                else
                    m_commandHandler(project::AddAudioTrack { plugin.name });
                return;
            }

            if (m_model.project.tracks.empty())
                return;

            const auto& targetTrack = m_model.project.tracks.front();
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

            m_commandHandler(project::AddTrackInsert {
                targetTrack.id,
                project::DspNodeDescriptor {
                    .slotIndex = static_cast<std::uint32_t>(targetTrack.inserts.size()),
                    .type = nodeType,
                    .bypassed = false,
                    .name = plugin.name,
                    .pluginId = plugin.id,
                    .parameters = {}
                }
            });
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

void MainEditorView::setDockMode(BottomDockMode mode)
{
    m_dockMode = mode;

    if (m_dockMode == BottomDockMode::mixer && m_floatingMixer)
    {
        m_floatingMixer->toFront(true);
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

    const bool isMixer = (m_dockMode == BottomDockMode::mixer);
    const bool isPianoRoll = (m_dockMode == BottomDockMode::pianoRoll);
    const bool isChannelRack = (m_dockMode == BottomDockMode::channelRack);
    const bool isDrumSampler = (m_dockMode == BottomDockMode::drumSampler);
    const bool isRoutingMatrix = (m_dockMode == BottomDockMode::routingMatrix);
    const bool isModulationRack = (m_dockMode == BottomDockMode::modulationRack);
    const bool isSpatialRadar = (m_dockMode == BottomDockMode::spatialRadar);
    const bool isWireGrid = (m_dockMode == BottomDockMode::wireGrid);
    const bool isMastering = (m_dockMode == BottomDockMode::masteringSuite);

    if (! m_floatingMixer) m_mixerPanel.setVisible(isMixer);
    if (! m_floatingPianoRoll) m_pianoRollView.setVisible(isPianoRoll);
    if (! m_floatingChannelRack) m_channelRackView.setVisible(isChannelRack);
    if (! m_floatingDrumSampler) m_drumSamplerView.setVisible(isDrumSampler);
    if (! m_floatingRoutingMatrix) m_routingMatrixView.setVisible(isRoutingMatrix);
    if (! m_floatingModulationRack) m_modulationRackView.setVisible(isModulationRack);
    if (! m_floatingSpatialRadar) m_spatialRadarView.setVisible(isSpatialRadar);
    if (! m_floatingWireGrid) m_wireGridView.setVisible(isWireGrid);
    if (! m_floatingMasteringSuite) m_masteringSuiteView.setVisible(isMastering);

    m_tabMixerButton.setColour(juce::TextButton::buttonColourId,
                               isMixer ? juce::Colour::fromRGB(45, 95, 145) : juce::Colour::fromRGB(28, 30, 36));
    m_tabPianoRollButton.setColour(juce::TextButton::buttonColourId,
                                   isPianoRoll ? juce::Colour::fromRGB(120, 60, 160) : juce::Colour::fromRGB(28, 30, 36));
    m_tabChannelRackButton.setColour(juce::TextButton::buttonColourId,
                                     isChannelRack ? juce::Colour::fromRGB(190, 80, 50) : juce::Colour::fromRGB(28, 30, 36));
    m_tabDrumSamplerButton.setColour(juce::TextButton::buttonColourId,
                                     isDrumSampler ? juce::Colour::fromRGB(210, 140, 40) : juce::Colour::fromRGB(28, 30, 36));
    m_tabRoutingMatrixButton.setColour(juce::TextButton::buttonColourId,
                                       isRoutingMatrix ? juce::Colour::fromRGB(110, 50, 150) : juce::Colour::fromRGB(28, 30, 36));
    m_tabModulationButton.setColour(juce::TextButton::buttonColourId,
                                    isModulationRack ? juce::Colour::fromRGB(45, 120, 95) : juce::Colour::fromRGB(28, 30, 36));
    m_tabSpatialRadarButton.setColour(juce::TextButton::buttonColourId,
                                      isSpatialRadar ? juce::Colour::fromRGB(65, 145, 225) : juce::Colour::fromRGB(28, 30, 36));
    m_tabWireGridButton.setColour(juce::TextButton::buttonColourId,
                                  isWireGrid ? juce::Colour::fromRGB(0, 180, 200) : juce::Colour::fromRGB(28, 30, 36));
    m_tabMasteringButton.setColour(juce::TextButton::buttonColourId,
                                   isMastering ? juce::Colour::fromRGB(200, 140, 40) : juce::Colour::fromRGB(28, 30, 36));
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
            m_tabMixerButton.setButtonText("Mixer [↙]");
            setDockMode(m_dockMode == BottomDockMode::mixer ? BottomDockMode::pianoRoll : m_dockMode);
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
            m_tabPianoRollButton.setButtonText("Piano Roll [↙]");
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
            m_tabChannelRackButton.setButtonText("Channel Rack [↙]");
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
            m_tabDrumSamplerButton.setButtonText("Drum Sampler [↙]");
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
            m_tabRoutingMatrixButton.setButtonText("Patchbay [↙]");
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
            m_tabModulationButton.setButtonText("Modulators [↙]");
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
            m_tabSpatialRadarButton.setButtonText("Spatial 3D [↙]");
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
            m_tabWireGridButton.setButtonText("Modular Grid [↙]");
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
            m_tabMasteringButton.setButtonText("Mastering [↙]");
            setDockMode(m_dockMode == BottomDockMode::masteringSuite ? BottomDockMode::mixer : m_dockMode);
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

    case DetachablePanel::browser:
        break;
    }

    resized();
}

void MainEditorView::reDockAllPanels()
{
    reDockPanel(DetachablePanel::mixer);
    reDockPanel(DetachablePanel::pianoRoll);
    reDockPanel(DetachablePanel::channelRack);
    reDockPanel(DetachablePanel::sessionMatrix);
    reDockPanel(DetachablePanel::drumSampler);
    reDockPanel(DetachablePanel::routingMatrix);
    reDockPanel(DetachablePanel::modulationRack);
    reDockPanel(DetachablePanel::spatialRadar);
    reDockPanel(DetachablePanel::wireGrid);
    reDockPanel(DetachablePanel::masteringSuite);
}

bool MainEditorView::isPanelDetached(DetachablePanel panel) const noexcept
{
    switch (panel)
    {
    case DetachablePanel::mixer: return m_floatingMixer != nullptr;
    case DetachablePanel::pianoRoll: return m_floatingPianoRoll != nullptr;
    case DetachablePanel::channelRack: return m_floatingChannelRack != nullptr;
    case DetachablePanel::sessionMatrix: return m_floatingSessionMatrix != nullptr;
    case DetachablePanel::drumSampler: return m_floatingDrumSampler != nullptr;
    case DetachablePanel::routingMatrix: return m_floatingRoutingMatrix != nullptr;
    case DetachablePanel::modulationRack: return m_floatingModulationRack != nullptr;
    case DetachablePanel::spatialRadar: return m_floatingSpatialRadar != nullptr;
    case DetachablePanel::wireGrid: return m_floatingWireGrid != nullptr;
    case DetachablePanel::masteringSuite: return m_floatingMasteringSuite != nullptr;
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
    switch (preset)
    {
    case WorkspacePreset::defaultArrangement:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 220;
        m_dockMaximized = false;
        if (m_browserVisible)
            togglePluginBrowser();
        break;

    case WorkspacePreset::dualScreenStudio:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        setDockMode(BottomDockMode::pianoRoll);
        detachPanel(DetachablePanel::mixer);
        m_dockHeight = 260;
        m_dockMaximized = false;
        break;

    case WorkspacePreset::beatmakerFocus:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        detachPanel(DetachablePanel::channelRack);
        setDockMode(BottomDockMode::pianoRoll);
        if (! m_browserVisible)
        {
            togglePluginBrowser();
            setSidebarTab(SidebarTab::samples);
        }
        m_sidebarWidth = 320;
        m_dockHeight = 240;
        m_dockMaximized = false;
        break;

    case WorkspacePreset::livePerformanceRig:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::sessionView);
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 240;
        m_dockMaximized = false;
        break;

    case WorkspacePreset::masteringSuite:
        reDockAllPanels();
        setCenterViewMode(CenterViewMode::arrangement);
        setDockMode(BottomDockMode::mixer);
        m_dockHeight = 360;
        m_dockMaximized = false;
        break;
    }
    resized();
}

void MainEditorView::saveCurrentWorkspace()
{
    // Saves window arrangement to project memory
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
    m_mixerPanel.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
    m_channelRackView.setViewModel(m_model);
    m_drumSamplerView.setViewModel(m_model);
    m_routingMatrixView.setViewModel(m_model);
    m_spatialRadarView.setViewModel(m_model);
    m_wireGridView.setViewModel(m_model);
    m_masteringSuiteView.setViewModel(m_model);
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model.project = std::move(snapshot);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_sessionMatrixView.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
    m_channelRackView.setViewModel(m_model);
    m_drumSamplerView.setViewModel(m_model);
    m_routingMatrixView.setViewModel(m_model);
    m_spatialRadarView.setViewModel(m_model);
    m_wireGridView.setViewModel(m_model);
    m_masteringSuiteView.setViewModel(m_model);
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
        menu.addItem(cmdOpenProject, "Open Project...  (Ctrl+O)", true, false);
        menu.addItem(cmdSaveProject, "Save Project  (Ctrl+S)", true, false);
        menu.addItem(cmdSaveProjectAs, "Save Project As...  (Ctrl+Shift+S)", true, false);
        menu.addSeparator();
        menu.addItem(cmdExportBounce, "Export Master Bounce...  (Ctrl+B)", true, false);
        menu.addItem(cmdExportStems, "Export Multi-Track Stems...", true, false);
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
        menu.addItem(cmdDetachMixer, "Detach Mixer to Window  (↗)", true, isPanelDetached(DetachablePanel::mixer));
        menu.addItem(cmdDetachPianoRoll, "Detach Piano Roll to Window  (↗)", true, isPanelDetached(DetachablePanel::pianoRoll));
        menu.addItem(cmdDetachChannelRack, "Detach Channel Rack to Window  (↗)", true, isPanelDetached(DetachablePanel::channelRack));
        menu.addItem(cmdReDockAll, "Re-Dock All Floating Panels  (↙)", true, false);
        menu.addItem(cmdToggleMaximizeDock, "Maximize Bottom Dock  (F11)", true, m_dockMaximized);
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
    case cmdDuplicateClip:
    case cmdDeleteClip:
        m_timelineView.grabKeyboardFocus();
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
    // Function key workspace presets
    if (key.getKeyCode() == juce::KeyPress::F1Key)
    {
        applyWorkspacePreset(WorkspacePreset::defaultArrangement);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F2Key)
    {
        applyWorkspacePreset(WorkspacePreset::dualScreenStudio);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F3Key)
    {
        applyWorkspacePreset(WorkspacePreset::beatmakerFocus);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F4Key)
    {
        applyWorkspacePreset(WorkspacePreset::livePerformanceRig);
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

    // 2. Transport Header Bar (44 px)
    m_transportBar.setBounds(area.removeFromTop(44));

    // 3. Right Sidebar Plugin / Sample Browser with vertical drag splitter
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

    // 4. Bottom Dock Area (Variable Height, Collapsible, or Maximized)
    const bool allDockedPanelsDetached = (m_floatingMixer && m_floatingPianoRoll && m_floatingChannelRack && m_floatingDrumSampler && m_floatingRoutingMatrix && m_floatingModulationRack && m_floatingSpatialRadar && m_floatingWireGrid && m_floatingMasteringSuite);
    if (! allDockedPanelsDetached)
    {
        const int actualDockHeight = m_dockMaximized ? (area.getHeight() - 30) : m_dockHeight;
        auto bottomDockArea = area.removeFromBottom(std::clamp(actualDockHeight, 40, std::max(50, area.getHeight() + actualDockHeight - 40)));

        m_dockSplitter.setVisible(true);
        m_dockSplitter.setBounds(area.removeFromBottom(5));

        auto tabHeader = bottomDockArea.removeFromTop(24);
        m_tabMixerButton.setBounds(tabHeader.removeFromLeft(105).reduced(2, 2));
        m_tabPianoRollButton.setBounds(tabHeader.removeFromLeft(100).reduced(2, 2));
        m_tabChannelRackButton.setBounds(tabHeader.removeFromLeft(110).reduced(2, 2));
        m_tabDrumSamplerButton.setBounds(tabHeader.removeFromLeft(110).reduced(2, 2));
        m_tabRoutingMatrixButton.setBounds(tabHeader.removeFromLeft(90).reduced(2, 2));
        m_tabModulationButton.setBounds(tabHeader.removeFromLeft(95).reduced(2, 2));
        m_tabSpatialRadarButton.setBounds(tabHeader.removeFromLeft(95).reduced(2, 2));
        m_tabWireGridButton.setBounds(tabHeader.removeFromLeft(110).reduced(2, 2));
        m_tabMasteringButton.setBounds(tabHeader.removeFromLeft(95).reduced(2, 2));
        m_tabBrowserButton.setBounds(tabHeader.removeFromLeft(80).reduced(2, 2));

        m_maximizeDockButton.setBounds(tabHeader.removeFromRight(26).reduced(2, 2));
        m_detachDockButton.setBounds(tabHeader.removeFromRight(26).reduced(2, 2));

        if (! m_floatingMixer) m_mixerPanel.setBounds(bottomDockArea);
        if (! m_floatingPianoRoll) m_pianoRollView.setBounds(bottomDockArea);
        if (! m_floatingChannelRack) m_channelRackView.setBounds(bottomDockArea);
        if (! m_floatingDrumSampler) m_drumSamplerView.setBounds(bottomDockArea);
        if (! m_floatingRoutingMatrix) m_routingMatrixView.setBounds(bottomDockArea);
        if (! m_floatingModulationRack) m_modulationRackView.setBounds(bottomDockArea);
        if (! m_floatingSpatialRadar) m_spatialRadarView.setBounds(bottomDockArea);
        if (! m_floatingWireGrid) m_wireGridView.setBounds(bottomDockArea);
        if (! m_floatingMasteringSuite) m_masteringSuiteView.setBounds(bottomDockArea);
    }
    else
    {
        m_dockSplitter.setVisible(false);
    }

    // 5. Center View: Arrangement Timeline or Session Matrix Grid
    m_timelineView.setBounds(area);
    m_sessionMatrixView.setBounds(area);
}

} // namespace dawg::ui
