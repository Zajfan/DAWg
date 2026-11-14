#pragma once

#include <dawg/plugins/PluginScanner.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace dawg::ui {

class PluginBrowserPanel final : public juce::Component, public juce::ListBoxModel
{
public:
    using InsertPluginCallback = std::function<void(const plugins::PluginDescriptor& plugin, bool createNewTrack)>;

    explicit PluginBrowserPanel(InsertPluginCallback onInsert);
    ~PluginBrowserPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent&) override;

    void rescan();

private:
    void filterPlugins();

    InsertPluginCallback m_onInsert;
    std::vector<plugins::PluginDescriptor> m_allPlugins;
    std::vector<plugins::PluginDescriptor> m_filteredPlugins;

    juce::Label m_titleLabel {"TitleLabel", "PLUGIN BROWSER"};
    juce::TextButton m_rescanButton {"Rescan"};
    juce::TextEditor m_searchBox;

    juce::TextButton m_filterAll {"All"};
    juce::TextButton m_filterInstruments {"Instruments"};
    juce::TextButton m_filterEffects {"Effects"};
    juce::TextButton m_filterSpatial {"Spatial"};
    juce::TextButton m_filterThirdParty {"CLAP / VST3"};
    int m_activeFilterCategory {0}; // 0 = all, 1 = inst, 2 = fx, 3 = spatial, 4 = 3rd party

    juce::ListBox m_listBox {"PluginList", this};
    juce::TextButton m_insertButton {"+ Insert into Track"};
    juce::TextButton m_addTrackButton {"+ New Track with Plugin"};
};

} // namespace dawg::ui
