#pragma once

#include "EditorViewModel.h"
#include "MixerPanel.h"
#include "PianoRollView.h"
#include "TimelineView.h"
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
    };

    struct MenuCallbacks
    {
        ActionHandler newProject;
        ActionHandler openProject;
        ActionHandler saveProject;
        ActionHandler saveProjectAs;
        ActionHandler exportBounce;
        ActionHandler exportStems;
        ActionHandler audioPreferences;
        ActionHandler showShortcuts;
        ActionHandler showAbout;
    };

    enum class BottomDockMode : std::uint8_t
    {
        mixer,
        pianoRoll
    };

    MainEditorView(EditorViewModel model,
                   CommandHandler commandHandler,
                   TransportCallbacks transportCallbacks = {},
                   MenuCallbacks menuCallbacks = {},
                   media::WaveformCache* waveformCache = nullptr);
    ~MainEditorView() override = default;

    void setViewModel(EditorViewModel model);
    void setSnapshot(project::ProjectSnapshot snapshot);
    void setWaveformCache(media::WaveformCache* cache);
    void setDockMode(BottomDockMode mode);
    [[nodiscard]] BottomDockMode dockMode() const noexcept { return m_dockMode; }

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int menuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    EditorViewModel m_model;
    CommandHandler m_commandHandler;
    TransportCallbacks m_transportCallbacks;
    MenuCallbacks m_menuCallbacks;

    juce::MenuBarComponent m_menuBar;
    TransportBar m_transportBar;
    TimelineView m_timelineView;

    // Bottom Dock
    BottomDockMode m_dockMode {BottomDockMode::mixer};
    juce::TextButton m_tabMixerButton {"Mixer Console [M]"};
    juce::TextButton m_tabPianoRollButton {"Piano Roll [P]"};
    MixerPanel m_mixerPanel;
    PianoRollView m_pianoRollView;
};

} // namespace dawg::ui
