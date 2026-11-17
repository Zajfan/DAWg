#include "AudioPoolDialog.h"

namespace dawg::ui {

AudioPoolComponent::AudioPoolComponent(persistence::AudioPoolManager& poolManager,
                                       std::function<void()> onProjectChanged)
    : m_poolManager(poolManager), m_onProjectChanged(std::move(onProjectChanged))
{
    m_titleLabel.setText("Project Audio Pool & Media Manager", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff58a6ff));
    addAndMakeVisible(m_titleLabel);

    m_statsLabel.setFont(juce::FontOptions(13.0f));
    m_statsLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8b949e));
    addAndMakeVisible(m_statsLabel);

    m_listBox.setModel(this);
    m_listBox.setRowHeight(28);
    m_listBox.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff0d1117));
    addAndMakeVisible(m_listBox);

    m_relinkButton.onClick = [this] {
        const auto results = m_poolManager.relinkMissingAssets(std::filesystem::current_path(), true);
        refreshPoolView();
        if (m_onProjectChanged) m_onProjectChanged();
    };
    addAndMakeVisible(m_relinkButton);

    m_purgeButton.onClick = [this] {
        const auto purged = m_poolManager.purgeUnusedAssets(true);
        refreshPoolView();
        if (m_onProjectChanged) m_onProjectChanged();
    };
    addAndMakeVisible(m_purgeButton);

    m_consolidateButton.onClick = [this] {
        refreshPoolView();
    };
    addAndMakeVisible(m_consolidateButton);

    refreshPoolView();
}

void AudioPoolComponent::refreshPoolView()
{
    const auto totalAssets = m_poolManager.totalAssetCount();
    const auto missingAssets = m_poolManager.missingAssetCount();
    const double mb = static_cast<double>(m_poolManager.totalPoolBytes()) / (1024.0 * 1024.0);

    m_statsLabel.setText(juce::String::formatted(
        "Total Assets: %d  |  Missing: %d  |  Pool Storage: %.1f MB",
        static_cast<int>(totalAssets),
        static_cast<int>(missingAssets),
        mb), juce::dontSendNotification);

    m_listBox.updateContent();
    repaint();
}

int AudioPoolComponent::getNumRows()
{
    return static_cast<int>(m_poolManager.poolItems().size());
}

void AudioPoolComponent::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(m_poolManager.poolItems().size()))
        return;

    const auto& item = m_poolManager.poolItems()[static_cast<std::size_t>(rowNumber)];

    if (rowIsSelected)
    {
        g.fillAll(juce::Colour(0xff1f242c));
    }
    else
    {
        g.fillAll((rowNumber % 2 == 0) ? juce::Colour(0xff161b22) : juce::Colour(0xff0d1117));
    }

    // Status pill
    const juce::Rectangle<int> statusPill {8, 5, 60, height - 10};
    if (item.isMissing)
    {
        g.setColour(juce::Colour(0xffff453a).withAlpha(0.2f));
        g.fillRoundedRectangle(statusPill.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xffff453a));
        g.drawRoundedRectangle(statusPill.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("MISSING", statusPill, juce::Justification::centred);
    }
    else
    {
        g.setColour(juce::Colour(0xff3fb950).withAlpha(0.2f));
        g.fillRoundedRectangle(statusPill.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff3fb950));
        g.drawRoundedRectangle(statusPill.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("ONLINE", statusPill, juce::Justification::centred);
    }

    // Asset filename & path
    g.setColour(item.isMissing ? juce::Colour(0xffffa657) : juce::Colour(0xffc9d1d9));
    g.setFont(juce::FontOptions(12.0f));
    const std::string filename = std::filesystem::path(item.originalPath).filename().string();
    g.drawText(filename, 76, 0, 200, height, juce::Justification::centredLeft);

    // Audio properties
    g.setColour(juce::Colour(0xff8b949e));
    g.setFont(juce::FontOptions(11.0f));
    const auto propText = juce::String::formatted(
        "%d Hz | %s | %.1fs",
        item.sampleRate,
        (item.channelCount == 2 ? "Stereo" : "Mono"),
        item.durationSeconds);
    g.drawText(propText, 280, 0, 160, height, juce::Justification::centredLeft);

    // File size
    const double kb = static_cast<double>(item.fileSizeBytes) / 1024.0;
    const juce::String sizeText = (kb > 1024.0)
        ? juce::String::formatted("%.1f MB", kb / 1024.0)
        : juce::String::formatted("%.0f KB", kb);
    g.drawText(sizeText, 440, 0, 80, height, juce::Justification::centredLeft);

    // Usages
    const auto usageText = juce::String::formatted("Usages: %d", item.totalUsageCount);
    g.setColour(item.totalUsageCount == 0 ? juce::Colour(0xff8b949e) : juce::Colour(0xff58a6ff));
    g.drawText(usageText, width - 100, 0, 90, height, juce::Justification::centredLeft);
}

void AudioPoolComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161b22));
    g.setColour(juce::Colour(0xff30363d));
    g.drawRect(getLocalBounds(), 1);
}

void AudioPoolComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    m_titleLabel.setBounds(area.removeFromTop(26));
    m_statsLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(10);

    auto btnRow = area.removeFromBottom(32);
    m_relinkButton.setBounds(btnRow.removeFromLeft(160));
    btnRow.removeFromLeft(10);
    m_purgeButton.setBounds(btnRow.removeFromLeft(160));
    btnRow.removeFromLeft(10);
    m_consolidateButton.setBounds(btnRow.removeFromLeft(220));

    area.removeFromBottom(12);
    m_listBox.setBounds(area);
}

AudioPoolWindow::AudioPoolWindow(persistence::AudioPoolManager& poolManager,
                                 std::function<void()> onProjectChanged)
    : juce::DocumentWindow("Project Audio Pool",
                           juce::Colour(0xff161b22),
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AudioPoolComponent(poolManager, std::move(onProjectChanged)), true);
    setResizable(true, true);
    centreWithSize(680, 480);
    setVisible(true);
}

void AudioPoolWindow::closeButtonPressed()
{
    delete this;
}

void AudioPoolWindow::open(persistence::AudioPoolManager& poolManager,
                           std::function<void()> onProjectChanged)
{
    new AudioPoolWindow(poolManager, std::move(onProjectChanged));
}

} // namespace dawg::ui
