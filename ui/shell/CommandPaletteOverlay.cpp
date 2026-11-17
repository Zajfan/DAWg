#include "CommandPaletteOverlay.h"

#include <algorithm>

namespace dawg::ui {

CommandPaletteOverlay::CommandPaletteOverlay(ActionCallback actionCb)
    : m_actionCb(std::move(actionCb))
{
    setAlwaysOnTop(true);
    setVisible(false);

    m_searchEditor.setFont(juce::FontOptions {15.0f});
    m_searchEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(28, 30, 38));
    m_searchEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    m_searchEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(80, 140, 240));
    m_searchEditor.setTextToShowWhenEmpty("Type to search panels, workspaces, and actions...", juce::Colour::fromRGB(130, 135, 150));
    m_searchEditor.onTextChange = [this] { filterItems(); };
    m_searchEditor.onReturnKey = [this] { executeSelected(); };
    m_searchEditor.onEscapeKey = [this] { close(); };
    addAndMakeVisible(m_searchEditor);

    rebuildCatalog();
}

void CommandPaletteOverlay::rebuildCatalog()
{
    m_allCatalog = {
        // Panels
        { "panel:mixer", "Mixer Console", "Panels", "M" },
        { "panel:pianoRoll", "Piano Roll Editor (MPE)", "Panels", "P" },
        { "panel:channelRack", "Channel Rack & Step Sequencer", "Panels", "R" },
        { "panel:drumSampler", "Drum Pad Sampler (MPC-Style)", "Panels", "D" },
        { "panel:wireGrid", "Modular DSP Wire Grid", "Panels", "G" },
        { "panel:routingMatrix", "Console Routing Matrix / Patchbay", "Panels", "X" },
        { "panel:spatialRadar", "3D Spatial Radar & Binaural Panner", "Panels", "Z" },
        { "panel:modulators", "Modulation Rack (LFO/Env/Step)", "Panels", "O" },
        { "panel:mastering", "Mastering Suite & Loudness Radar", "Panels", "U" },
        { "panel:pitchBlob", "Pitch Contour & Vocal Tuner", "Panels", "H" },
        { "panel:sampleZone", "Multi-Sample SFZ Zone Editor", "Panels", "K" },
        { "panel:browser", "Plugin & Sample Browser", "Panels", "B" },

        // Workspaces
        { "workspace:arrangement", "Workspace: Song Arrangement", "Workspaces", "1" },
        { "workspace:beatmaker", "Workspace: Pattern Lab / Beatmaker", "Workspaces", "2" },
        { "workspace:recording", "Workspace: Recording & Comping", "Workspaces", "3" },
        { "workspace:mixing", "Workspace: Mixing Console", "Workspaces", "4" },
        { "workspace:modular", "Workspace: Sound Design & Modular", "Workspaces", "5" },
        { "workspace:mastering", "Workspace: Mastering Suite", "Workspaces", "6" },
        { "workspace:liveRig", "Workspace: Live Performance Rig", "Workspaces", "7" },

        // Templates
        { "template:modernTrap", "Template: Modern Trap / Beatmaker", "Templates", "" },
        { "template:synthwave", "Template: Synthwave / Cyberpunk", "Templates", "" },
        { "template:acousticRock", "Template: Multi-Mic Acoustic Rock", "Templates", "" },
        { "template:mastering", "Template: Mastering Session", "Templates", "" },

        // Actions
        { "action:toggleInspector", "Toggle Left Track Inspector", "Actions", "I" },
        { "action:toggleCenterView", "Toggle Arrangement / Clip Matrix", "Actions", "Tab" },
        { "action:quantize", "Quantize Selected Notes", "Actions", "Q" },
        { "action:stepRecord", "Toggle Piano Roll Step Recording", "Actions", "" },
        { "action:addAudioTrack", "Add Audio Track", "Actions", "" },
        { "action:addMidiTrack", "Add Instrument / MIDI Track", "Actions", "" },
        { "action:importWav", "Import Audio / WAV", "Actions", "" },
        { "action:bounceStems", "Export Stems & Master Audio", "Actions", "" },
        { "action:audioPool", "Open Project Audio Pool", "Actions", "" },
        { "action:consolidate", "Consolidate & Archive Project", "Actions", "" },
        { "action:rescanPlugins", "Rescan VST3 & CLAP System Plugins", "Actions", "" },
        { "action:videoMonitor", "Open Video Picture Monitor & SMPTE Sync", "Actions", "" },
        { "action:spectralRepair", "Open Spectral Audio Repair & De-Hum", "Actions", "" },
        { "action:cueSheet", "Export Production Music Cue Sheet (CSV)", "Actions", "" },
        { "action:calibrateLatency", "Calibrate Hardware Loopback Latency", "Actions", "" },
        { "action:maximizeDock", "Maximize Active Dock Pane", "Actions", "F11" },
        { "action:detachActive", "Detach Active Panel to Window", "Actions", "" },
        { "action:reDockAll", "Re-Dock All Floating Windows", "Actions", "" }
    };
    filterItems();
}

void CommandPaletteOverlay::open()
{
    setVisible(true);
    m_searchEditor.setText("");
    m_selectedIndex = 0;
    filterItems();
    m_searchEditor.grabKeyboardFocus();
    repaint();
}

void CommandPaletteOverlay::close()
{
    setVisible(false);
}

void CommandPaletteOverlay::filterItems()
{
    const juce::String query = m_searchEditor.getText().trim().toLowerCase();
    m_filteredCatalog.clear();

    if (query.isEmpty())
    {
        m_filteredCatalog = m_allCatalog;
    }
    else
    {
        for (const auto& item : m_allCatalog)
        {
            const juce::String title(item.title);
            const juce::String cat(item.category);
            const juce::String key(item.shortcut);
            if (title.toLowerCase().contains(query) || cat.toLowerCase().contains(query) || key.toLowerCase().contains(query))
            {
                m_filteredCatalog.push_back(item);
            }
        }
    }

    m_selectedIndex = std::clamp(m_selectedIndex, 0, std::max(0, static_cast<int>(m_filteredCatalog.size()) - 1));
    repaint();
}

void CommandPaletteOverlay::executeSelected()
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_filteredCatalog.size()))
    {
        const auto chosen = m_filteredCatalog[static_cast<std::size_t>(m_selectedIndex)].id;
        close();
        if (m_actionCb)
        {
            m_actionCb(chosen);
        }
    }
}

bool CommandPaletteOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        if (m_selectedIndex > 0)
        {
            m_selectedIndex--;
            repaint();
        }
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        if (m_selectedIndex < static_cast<int>(m_filteredCatalog.size()) - 1)
        {
            m_selectedIndex++;
            repaint();
        }
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        executeSelected();
        return true;
    }
    return false;
}

void CommandPaletteOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (! m_dialogBounds.contains(e.getPosition()))
    {
        close();
    }
    else
    {
        // Check click on item row
        const int listY = m_dialogBounds.getY() + 48;
        const int relY = e.y - listY;
        if (relY >= 0)
        {
            const int clickedIdx = relY / 28;
            if (clickedIdx >= 0 && clickedIdx < static_cast<int>(m_filteredCatalog.size()))
            {
                m_selectedIndex = clickedIdx;
                executeSelected();
            }
        }
    }
}

void CommandPaletteOverlay::paint(juce::Graphics& g)
{
    // Dim background scrim
    g.fillAll(juce::Colours::black.withAlpha(0.65f));

    // Dialog card
    g.setColour(juce::Colour::fromRGB(24, 26, 33));
    g.fillRoundedRectangle(m_dialogBounds.toFloat(), 6.0f);

    // Border glow
    g.setColour(juce::Colour::fromRGB(65, 75, 95));
    g.drawRoundedRectangle(m_dialogBounds.toFloat(), 6.0f, 1.0f);

    // Render filtered items
    const int listY = m_dialogBounds.getY() + 48;
    const int itemHeight = 28;
    const int maxVisible = (m_dialogBounds.getHeight() - 56) / itemHeight;

    g.saveState();
    g.reduceClipRegion(m_dialogBounds.reduced(8, 48));

    for (int i = 0; i < std::min(static_cast<int>(m_filteredCatalog.size()), maxVisible); ++i)
    {
        const auto& item = m_filteredCatalog[static_cast<std::size_t>(i)];
        const int rowY = listY + i * itemHeight;
        auto rowRect = juce::Rectangle<int>(m_dialogBounds.getX() + 8, rowY, m_dialogBounds.getWidth() - 16, itemHeight);

        if (i == m_selectedIndex)
        {
            g.setColour(juce::Colour::fromRGB(45, 100, 210));
            g.fillRoundedRectangle(rowRect.toFloat(), 4.0f);
            g.setColour(juce::Colours::white);
        }
        else
        {
            g.setColour(juce::Colour::fromRGB(210, 215, 225));
        }

        // Shortcut badge on far right
        if (! item.shortcut.empty())
        {
            auto shortcutRect = rowRect.removeFromRight(50);
            g.setColour(i == m_selectedIndex ? juce::Colours::yellow : juce::Colour::fromRGB(255, 180, 50));
            g.setFont(juce::FontOptions {11.0f});
            g.drawText("[" + item.shortcut + "]", shortcutRect.reduced(4, 0), juce::Justification::centredRight, true);
        }

        // Category badge
        auto categoryRect = rowRect.removeFromRight(120);
        g.setColour(i == m_selectedIndex ? juce::Colour::fromRGB(200, 225, 255) : juce::Colour::fromRGB(120, 130, 145));
        g.setFont(juce::FontOptions {11.0f});
        g.drawText(item.category, categoryRect.reduced(8, 0), juce::Justification::centredRight, true);

        // Title
        g.setColour(i == m_selectedIndex ? juce::Colours::white : juce::Colour::fromRGB(210, 215, 225));
        g.setFont(juce::FontOptions {13.0f}.withStyle(i == m_selectedIndex ? "Bold" : "Regular"));
        g.drawText(item.title, rowRect.reduced(10, 0), juce::Justification::centredLeft, true);
    }

    g.restoreState();
}

void CommandPaletteOverlay::resized()
{
    constexpr int dialogW = 540;
    constexpr int dialogH = 380;
    m_dialogBounds = juce::Rectangle<int>(
        (getWidth() - dialogW) / 2,
        std::max(40, getHeight() / 6),
        dialogW,
        dialogH);

    m_searchEditor.setBounds(
        m_dialogBounds.getX() + 10,
        m_dialogBounds.getY() + 10,
        m_dialogBounds.getWidth() - 20,
        32);
}

} // namespace dawg::ui
