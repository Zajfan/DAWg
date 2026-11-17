#include "AudioPoolDialog.h"

#include <dawg/media/StemSeparatorService.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace dawg::ui {

AudioPoolComponent::AudioPoolComponent(persistence::AudioPoolManager& poolManager,
                                       std::function<void()> onProjectChanged,
                                       VocalStemCallback vocalStemCb)
    : m_poolManager(poolManager)
    , m_onProjectChanged(std::move(onProjectChanged))
    , m_vocalStemCallback(std::move(vocalStemCb))
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

    m_stemSplitButton.onClick = [this] {
        splitSelectedAssetIntoStems();
    };
    m_stemSplitButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff238636));
    addAndMakeVisible(m_stemSplitButton);

    m_relinkButton.onClick = [this] {
        const auto results = m_poolManager.relinkMissingAssets(std::filesystem::current_path(), true);
        (void)results;
        refreshPoolView();
        if (m_onProjectChanged) m_onProjectChanged();
    };
    addAndMakeVisible(m_relinkButton);

    m_purgeButton.onClick = [this] {
        const auto purged = m_poolManager.purgeUnusedAssets(true);
        (void)purged;
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

void AudioPoolComponent::splitSelectedAssetIntoStems()
{
    const int selRow = m_listBox.getSelectedRow();
    const auto& items = m_poolManager.poolItems();
    if (items.empty()) return;

    const std::size_t idx = (selRow >= 0 && static_cast<std::size_t>(selRow) < items.size())
        ? static_cast<std::size_t>(selRow)
        : 0;

    const auto& item = items[idx];
    const auto fsPath = std::filesystem::path(item.resolvedPath.empty() ? item.originalPath : item.resolvedPath);
    if (item.isMissing || !std::filesystem::exists(fsPath)) return;

    juce::AudioFormatManager formatMgr;
    formatMgr.registerBasicFormats();
    const juce::File srcFile(juce::String(fsPath.string()));
    std::unique_ptr<juce::AudioFormatReader> reader(formatMgr.createReaderFor(srcFile));
    if (reader == nullptr || reader->lengthInSamples == 0) return;

    const auto numSamples = static_cast<std::size_t>(reader->lengthInSamples);
    std::vector<float> left(numSamples, 0.0f);
    std::vector<float> right(numSamples, 0.0f);
    juce::AudioBuffer<float> tempBuf(static_cast<int>(reader->numChannels), static_cast<int>(numSamples));
    reader->read(&tempBuf, 0, static_cast<int>(numSamples), 0, true, true);

    std::copy(tempBuf.getReadPointer(0), tempBuf.getReadPointer(0) + numSamples, left.begin());
    if (reader->numChannels > 1) {
        std::copy(tempBuf.getReadPointer(1), tempBuf.getReadPointer(1) + numSamples, right.begin());
    } else {
        right = left;
    }

    const auto stems = media::StemSeparatorService::separate4Stems(
        left.data(), right.data(), numSamples, reader->sampleRate);

    const auto parentDir = fsPath.parent_path();
    const auto baseName = fsPath.stem().string();

    auto writeStem = [&](const std::string& label, const std::vector<float>& l, const std::vector<float>& r) {
        const auto outPath = parentDir / (baseName + "_" + label + ".wav");
        juce::File outFile(juce::String(outPath.string()));
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> outStream(outFile.createOutputStream());
        if (outStream != nullptr) {
            juce::WavAudioFormat wavFormat;
            juce::AudioFormatWriterOptions writerOptions;
            writerOptions = writerOptions
                .withSampleRate(reader->sampleRate)
                .withNumChannels(2)
                .withBitsPerSample(24);
            auto writer = wavFormat.createWriterFor(outStream, writerOptions);
            if (writer != nullptr) {
                juce::AudioBuffer<float> b(2, static_cast<int>(numSamples));
                std::copy(l.begin(), l.end(), b.getWritePointer(0));
                std::copy(r.begin(), r.end(), b.getWritePointer(1));
                writer->writeFromAudioSampleBuffer(b, 0, static_cast<int>(numSamples));
            }
        }
    };

    writeStem("Drums", stems.drumsL, stems.drumsR);
    writeStem("Bass", stems.bassL, stems.bassR);
    writeStem("Vocals", stems.vocalsL, stems.vocalsR);
    writeStem("Other", stems.otherL, stems.otherR);

    // Send extracted vocal stem audio to PitchBlobCompingView
    if (m_vocalStemCallback) {
        m_vocalStemCallback(stems.vocalsL.data(), stems.vocalsL.size(), reader->sampleRate);
    }

    refreshPoolView();
    if (m_onProjectChanged) m_onProjectChanged();
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

    auto btnRow2 = area.removeFromBottom(28);
    m_stemSplitButton.setBounds(btnRow2);
    area.removeFromBottom(8);

    auto btnRow = area.removeFromBottom(28);
    m_relinkButton.setBounds(btnRow.removeFromLeft(150));
    btnRow.removeFromLeft(8);
    m_purgeButton.setBounds(btnRow.removeFromLeft(150));
    btnRow.removeFromLeft(8);
    m_consolidateButton.setBounds(btnRow.removeFromLeft(200));

    area.removeFromBottom(10);
    m_listBox.setBounds(area);
}

AudioPoolWindow::AudioPoolWindow(persistence::AudioPoolManager& poolManager,
                                 std::function<void()> onProjectChanged,
                                 AudioPoolComponent::VocalStemCallback vocalStemCb)
    : juce::DocumentWindow("Project Audio Pool",
                           juce::Colour(0xff161b22),
                           juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new AudioPoolComponent(poolManager, std::move(onProjectChanged), std::move(vocalStemCb)), true);
    setResizable(true, true);
    centreWithSize(720, 520);
    setVisible(true);
}

void AudioPoolWindow::closeButtonPressed()
{
    delete this;
}

void AudioPoolWindow::open(persistence::AudioPoolManager& poolManager,
                           std::function<void()> onProjectChanged,
                           AudioPoolComponent::VocalStemCallback vocalStemCb)
{
    new AudioPoolWindow(poolManager, std::move(onProjectChanged), std::move(vocalStemCb));
}

} // namespace dawg::ui
