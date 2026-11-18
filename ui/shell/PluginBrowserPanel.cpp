#include "PluginBrowserPanel.h"

#include <algorithm>

namespace dawg::ui {

PluginBrowserPanel::PluginBrowserPanel(InsertPluginCallback onInsert)
    : m_onInsert(std::move(onInsert))
{
    m_titleLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 225, 235));
    addAndMakeVisible(m_titleLabel);

    m_rescanButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 38, 48));
    m_rescanButton.onClick = [this] { rescan(); };
    addAndMakeVisible(m_rescanButton);

    m_searchBox.setTextToShowWhenEmpty("Search plugins...", juce::Colour::fromRGB(120, 125, 140));
    m_searchBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(28, 30, 38));
    m_searchBox.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(45, 48, 60));
    m_searchBox.onTextChange = [this] { filterPlugins(); };
    addAndMakeVisible(m_searchBox);

    auto setupFilterBtn = [this](juce::TextButton& btn, int catIdx) {
        btn.setColour(juce::TextButton::buttonColourId,
                      (m_activeFilterCategory == catIdx) ? juce::Colour::fromRGB(45, 110, 160)
                                                         : juce::Colour::fromRGB(32, 34, 42));
        btn.onClick = [this, catIdx] {
            m_activeFilterCategory = catIdx;
            filterPlugins();
        };
        addAndMakeVisible(btn);
    };

    setupFilterBtn(m_filterAll, 0);
    setupFilterBtn(m_filterInstruments, 1);
    setupFilterBtn(m_filterEffects, 2);
    setupFilterBtn(m_filterSpatial, 3);
    setupFilterBtn(m_filterThirdParty, 4);

    m_listBox.setRowHeight(36);
    m_listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGB(18, 19, 24));
    m_listBox.setColour(juce::ListBox::outlineColourId, juce::Colour::fromRGB(35, 38, 48));
    addAndMakeVisible(m_listBox);

    m_insertButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 120, 175));
    m_insertButton.onClick = [this] {
        const int row = m_listBox.getSelectedRow();
        if (row >= 0 && row < static_cast<int>(m_filteredPlugins.size()) && m_onInsert)
        {
            m_onInsert(m_filteredPlugins[static_cast<std::size_t>(row)], false);
        }
    };
    addAndMakeVisible(m_insertButton);

    m_addTrackButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 85, 130));
    m_addTrackButton.onClick = [this] {
        const int row = m_listBox.getSelectedRow();
        if (row >= 0 && row < static_cast<int>(m_filteredPlugins.size()) && m_onInsert)
        {
            m_onInsert(m_filteredPlugins[static_cast<std::size_t>(row)], true);
        }
    };
    addAndMakeVisible(m_addTrackButton);

    rescan();
}

void PluginBrowserPanel::rescan()
{
    m_allPlugins = plugins::PluginScanner::scanAll();
    filterPlugins();
}

void PluginBrowserPanel::filterPlugins()
{
    const auto query = m_searchBox.getText().trim().toLowerCase();
    m_filteredPlugins.clear();

    for (const auto& p : m_allPlugins)
    {
        // Category filtering
        if (m_activeFilterCategory == 1 && ! p.isInstrument)
            continue;
        if (m_activeFilterCategory == 2 && (p.isInstrument || p.category == plugins::PluginCategory::spatial))
            continue;
        if (m_activeFilterCategory == 3 && p.category != plugins::PluginCategory::spatial)
            continue;
        if (m_activeFilterCategory == 4 && p.format == plugins::PluginFormat::native)
            continue;

        // Search query
        if (query.isNotEmpty())
        {
            const auto nameLower = juce::String(p.name).toLowerCase();
            const auto vendorLower = juce::String(p.vendor).toLowerCase();
            if (! nameLower.contains(query) && ! vendorLower.contains(query))
                continue;
        }

        m_filteredPlugins.push_back(p);
    }

    // Update filter button colors
    m_filterAll.setColour(juce::TextButton::buttonColourId,
                          (m_activeFilterCategory == 0) ? juce::Colour::fromRGB(45, 110, 160) : juce::Colour::fromRGB(32, 34, 42));
    m_filterInstruments.setColour(juce::TextButton::buttonColourId,
                                  (m_activeFilterCategory == 1) ? juce::Colour::fromRGB(45, 110, 160) : juce::Colour::fromRGB(32, 34, 42));
    m_filterEffects.setColour(juce::TextButton::buttonColourId,
                              (m_activeFilterCategory == 2) ? juce::Colour::fromRGB(45, 110, 160) : juce::Colour::fromRGB(32, 34, 42));
    m_filterSpatial.setColour(juce::TextButton::buttonColourId,
                              (m_activeFilterCategory == 3) ? juce::Colour::fromRGB(45, 110, 160) : juce::Colour::fromRGB(32, 34, 42));
    m_filterThirdParty.setColour(juce::TextButton::buttonColourId,
                                 (m_activeFilterCategory == 4) ? juce::Colour::fromRGB(45, 110, 160) : juce::Colour::fromRGB(32, 34, 42));

    m_listBox.updateContent();
    repaint();
}

int PluginBrowserPanel::getNumRows()
{
    return static_cast<int>(m_filteredPlugins.size());
}

void PluginBrowserPanel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_filteredPlugins.size()))
        return;

    const auto& plugin = m_filteredPlugins[static_cast<std::size_t>(rowNumber)];

    if (rowIsSelected)
    {
        g.setColour(juce::Colour::fromRGB(45, 95, 145));
        g.fillRect(0, 0, width, height);
    }
    else if (rowNumber % 2 == 1)
    {
        g.setColour(juce::Colour::fromRGB(22, 24, 30));
        g.fillRect(0, 0, width, height);
    }

    // Format badge
    juce::Colour badgeBg;
    juce::String badgeText;
    if (plugin.format == plugins::PluginFormat::native)
    {
        badgeBg = juce::Colour::fromRGB(30, 80, 130);
        badgeText = "NATIVE";
    }
    else if (plugin.format == plugins::PluginFormat::clap)
    {
        badgeBg = juce::Colour::fromRGB(140, 75, 20);
        badgeText = "CLAP";
    }
    else
    {
        badgeBg = juce::Colour::fromRGB(95, 45, 130);
        badgeText = "VST3";
    }

    const juce::Rectangle<float> badgeRect {static_cast<float>(width - 58), static_cast<float>(height / 2 - 8), 50.0f, 16.0f};
    g.setColour(badgeBg);
    g.fillRoundedRectangle(badgeRect, 3.0f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(badgeText, badgeRect.toNearestInt(), juce::Justification::centred);

    // Name & Vendor
    g.setColour(rowIsSelected ? juce::Colours::white : juce::Colour::fromRGB(225, 230, 240));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.drawText(plugin.name, 8, 3, width - 70, 16, juce::Justification::topLeft);

    g.setColour(juce::Colour::fromRGB(140, 145, 160));
    g.setFont(juce::FontOptions(10.0f));
    const auto subText = juce::String(plugin.vendor) + (plugin.isInstrument ? " - [Instrument]" : " - [Effect]");
    g.drawText(subText, 8, 19, width - 70, 14, juce::Justification::topLeft);
}

void PluginBrowserPanel::listBoxItemDoubleClicked(int rowNumber, const juce::MouseEvent&)
{
    if (rowNumber >= 0 && rowNumber < static_cast<int>(m_filteredPlugins.size()) && m_onInsert)
    {
        m_onInsert(m_filteredPlugins[static_cast<std::size_t>(rowNumber)], false);
    }
}

void PluginBrowserPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(21, 23, 28));
    g.setColour(juce::Colour::fromRGB(35, 38, 48));
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
}

void PluginBrowserPanel::resized()
{
    auto area = getLocalBounds().reduced(8);

    // Header row
    auto headerRow = area.removeFromTop(24);
    m_rescanButton.setBounds(headerRow.removeFromRight(60));
    m_titleLabel.setBounds(headerRow);
    area.removeFromTop(6);

    // Search Box
    m_searchBox.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    // Filter pills (2 rows or horizontal wrap)
    auto filterRow1 = area.removeFromTop(20);
    const int pillW1 = filterRow1.getWidth() / 3;
    m_filterAll.setBounds(filterRow1.removeFromLeft(pillW1).reduced(1, 0));
    m_filterInstruments.setBounds(filterRow1.removeFromLeft(pillW1).reduced(1, 0));
    m_filterEffects.setBounds(filterRow1.reduced(1, 0));
    area.removeFromTop(3);

    auto filterRow2 = area.removeFromTop(20);
    const int pillW2 = filterRow2.getWidth() / 2;
    m_filterSpatial.setBounds(filterRow2.removeFromLeft(pillW2).reduced(1, 0));
    m_filterThirdParty.setBounds(filterRow2.reduced(1, 0));
    area.removeFromTop(6);

    // Bottom action buttons
    auto bottomRow = area.removeFromBottom(28);
    const int btnW = (bottomRow.getWidth() - 6) / 2;
    m_insertButton.setBounds(bottomRow.removeFromLeft(btnW));
    m_addTrackButton.setBounds(bottomRow.removeFromRight(btnW));
    area.removeFromBottom(6);

    // List box
    m_listBox.setBounds(area);
}

} // namespace dawg::ui
