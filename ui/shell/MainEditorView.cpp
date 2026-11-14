#include "MainEditorView.h"

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
    cmdExit = 7,

    // Edit
    cmdUndo = 10,
    cmdRedo = 11,
    cmdSplitClip = 12,
    cmdDuplicateClip = 13,
    cmdDeleteClip = 14,

    // Track
    cmdAddAudioTrack = 20,
    cmdAddSynthTrack = 21,
    cmdAddPlateReverb = 22,
    cmdAddStereoDelay = 23,
    cmdAddDrumBus = 24,

    // View
    cmdToggleMixer = 30,
    cmdTogglePianoRoll = 31,
    cmdToggleAutomation = 32,
    cmdToggleBrowser = 33,

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
    , m_mixerPanel(m_commandHandler, m_transportCallbacks.setMasterLimiter)
    , m_pianoRollView(m_commandHandler)
{
    addAndMakeVisible(m_menuBar);
    addAndMakeVisible(m_transportBar);
    addAndMakeVisible(m_timelineView);

    // Dock switcher buttons
    m_tabMixerButton.setClickingTogglesState(false);
    m_tabMixerButton.onClick = [this] { setDockMode(BottomDockMode::mixer); };
    addAndMakeVisible(m_tabMixerButton);

    m_tabPianoRollButton.setClickingTogglesState(false);
    m_tabPianoRollButton.onClick = [this] { setDockMode(BottomDockMode::pianoRoll); };
    addAndMakeVisible(m_tabPianoRollButton);

    m_tabBrowserButton.setClickingTogglesState(false);
    m_tabBrowserButton.onClick = [this] { togglePluginBrowser(); };
    addAndMakeVisible(m_tabBrowserButton);

    addAndMakeVisible(m_mixerPanel);
    addChildComponent(m_pianoRollView); // hidden initially

    m_pluginBrowser = std::make_unique<PluginBrowserPanel>(
        [this](const plugins::PluginDescriptor& plugin, bool createNewTrack) {
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

    setDockMode(BottomDockMode::mixer);
    setViewModel(m_model);
}

void MainEditorView::togglePluginBrowser()
{
    m_browserVisible = ! m_browserVisible;
    if (m_pluginBrowser)
        m_pluginBrowser->setVisible(m_browserVisible);
    m_tabBrowserButton.setColour(juce::TextButton::buttonColourId,
                                m_browserVisible ? juce::Colour::fromRGB(45, 95, 145) : juce::Colour::fromRGB(28, 30, 36));
    resized();
}

void MainEditorView::setDockMode(BottomDockMode mode)
{
    m_dockMode = mode;
    const bool isMixer = (m_dockMode == BottomDockMode::mixer);

    m_mixerPanel.setVisible(isMixer);
    m_pianoRollView.setVisible(! isMixer);

    m_tabMixerButton.setColour(juce::TextButton::buttonColourId,
                               isMixer ? juce::Colour::fromRGB(45, 95, 145) : juce::Colour::fromRGB(28, 30, 36));
    m_tabPianoRollButton.setColour(juce::TextButton::buttonColourId,
                                   (! isMixer) ? juce::Colour::fromRGB(120, 60, 160) : juce::Colour::fromRGB(28, 30, 36));
    resized();
}

void MainEditorView::setViewModel(EditorViewModel model)
{
    m_model = std::move(model);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
}

void MainEditorView::setSnapshot(project::ProjectSnapshot snapshot)
{
    m_model.project = std::move(snapshot);
    m_transportBar.setViewModel(m_model);
    m_timelineView.setViewModel(m_model);
    m_mixerPanel.setViewModel(m_model);
    m_pianoRollView.setViewModel(m_model);
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
    return {"File", "Edit", "Track", "View", "Options", "Help"};
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
        menu.addSeparator();
        menu.addItem(cmdAddPlateReverb, "Add Plate Reverb Aux Return", true, false);
        menu.addItem(cmdAddStereoDelay, "Add Stereo Delay Aux Return", true, false);
        menu.addItem(cmdAddDrumBus, "Add Drum Submix Bus", true, false);
        break;

    case 3: // View
        menu.addItem(cmdToggleMixer, "Show Console Mixer  (M)", true, m_dockMode == BottomDockMode::mixer);
        menu.addItem(cmdTogglePianoRoll, "Show Piano Roll  (P)", true, m_dockMode == BottomDockMode::pianoRoll);
        menu.addSeparator();
        menu.addItem(cmdToggleAutomation, "Toggle Automation Lanes  (A)", true, m_timelineView.showAutomation());
        menu.addItem(cmdToggleBrowser, "Toggle Plugin Browser  (B)", true, m_browserVisible);
        break;

    case 4: // Options
        menu.addItem(cmdAudioPreferences, "Audio Hardware & MIDI Preferences...", true, false);
        menu.addSeparator();
        menu.addItem(cmdToggleMetronome, "Metronome Click", true, m_model.project.tempo.metronomeEnabled);
        menu.addItem(cmdToggleLimiter, "Master Limiter Protection", true, m_model.masterLimiterEnabled);
        break;

    case 5: // Help
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
    case cmdToggleAutomation:
        m_timelineView.toggleAutomation();
        break;
    case cmdToggleBrowser:
        togglePluginBrowser();
        break;

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
    // Global hotkeys
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
    if (key.getKeyCode() == 'A' || key.getKeyCode() == 'a')
    {
        m_timelineView.toggleAutomation();
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

    // 3. Bottom Dock Switcher Tabs (24 px) & Bottom Content (200 px)
    constexpr int dockContentHeight = 200;
    auto bottomDockArea = area.removeFromBottom(dockContentHeight + 24);

    auto tabHeader = bottomDockArea.removeFromTop(24);
    m_tabMixerButton.setBounds(tabHeader.removeFromLeft(140).reduced(2, 2));
    m_tabPianoRollButton.setBounds(tabHeader.removeFromLeft(130).reduced(2, 2));
    m_tabBrowserButton.setBounds(tabHeader.removeFromLeft(110).reduced(2, 2));

    m_mixerPanel.setBounds(bottomDockArea);
    m_pianoRollView.setBounds(bottomDockArea);

    // 4. Right Sidebar Plugin Browser
    if (m_browserVisible && m_pluginBrowser)
    {
        m_pluginBrowser->setBounds(area.removeFromRight(260));
    }

    // 5. Center Multi-Track Timeline View
    m_timelineView.setBounds(area);
}

} // namespace dawg::ui
