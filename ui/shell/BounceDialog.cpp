#include "BounceDialog.h"

namespace dawg::ui {

BounceComponent::BounceComponent(project::ProjectSnapshot snapshot, StartRenderFn startRender)
    : m_snapshot(std::move(snapshot))
    , m_startRender(std::move(startRender))
{
    addAndMakeVisible(m_destLabel);
    m_destPathEdit.setText("output/master_bounce.wav");
    addAndMakeVisible(m_destPathEdit);

    m_browseButton.onClick = [this] {
        m_fileChooser = std::make_unique<juce::FileChooser>(
            "Export Master Audio File",
            juce::File::getCurrentWorkingDirectory().getChildFile("master_bounce.wav"),
            "*.wav");
        m_fileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser) {
                const auto result = chooser.getResult();
                if (result.getFullPathName().isNotEmpty())
                {
                    m_destPathEdit.setText(result.getFullPathName());
                }
            });
    };
    addAndMakeVisible(m_browseButton);

    // Range Combo
    addAndMakeVisible(m_rangeLabel);
    m_rangeCombo.addItem("Entire Arrangement", 1);
    m_rangeCombo.addItem("Loop Region", 2);
    m_rangeCombo.setSelectedId(1);
    addAndMakeVisible(m_rangeCombo);

    // Format Combo
    addAndMakeVisible(m_formatLabel);
    m_formatCombo.addItem("24-bit PCM (Industry Standard)", 1);
    m_formatCombo.addItem("16-bit PCM (TPDF Dithered)", 2);
    m_formatCombo.addItem("32-bit Floating Point", 3);
    m_formatCombo.setSelectedId(1);
    addAndMakeVisible(m_formatCombo);

    // Sample Rate Combo
    addAndMakeVisible(m_srLabel);
    m_srCombo.addItem("48,000 Hz (Project Native)", 1);
    m_srCombo.addItem("44,100 Hz (CD Standard)", 2);
    m_srCombo.addItem("96,000 Hz (High Resolution)", 3);
    m_srCombo.setSelectedId(1);
    addAndMakeVisible(m_srCombo);

    // Mode Combo
    addAndMakeVisible(m_modeLabel);
    m_modeCombo.addItem("Master Mix (Summed Stereo Master)", 1);
    m_modeCombo.addItem("Separate Multi-Track Stems", 2);
    m_modeCombo.setSelectedId(1);
    addAndMakeVisible(m_modeCombo);

    // Progress Bar & Buttons
    addAndMakeVisible(m_progressBar);

    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(170, 180, 200));
    m_statusLabel.setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(m_statusLabel);

    m_startButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 125, 200));
    m_startButton.onClick = [this] { startBounce(); };
    addAndMakeVisible(m_startButton);

    m_cancelButton.onClick = [this] { cancelBounce(); };
    m_cancelButton.setEnabled(false);
    addAndMakeVisible(m_cancelButton);

    setSize(520, 360);
    startTimerHz(20);
}

BounceComponent::~BounceComponent()
{
    stopTimer();
    m_cancelToken.store(true);
}

void BounceComponent::timerCallback()
{
    // periodic UI updates if needed
}

void BounceComponent::startBounce()
{
    if (m_isRendering.load() || ! m_startRender)
        return;

    media::RenderOptions opts;
    opts.destinationPath = m_destPathEdit.getText().toStdString();
    if (opts.destinationPath.empty())
        opts.destinationPath = "output/master_bounce.wav";

    // Range
    if (m_rangeCombo.getSelectedId() == 2 && m_snapshot.loop.enabled)
    {
        opts.startSample = m_snapshot.loop.startSample;
        opts.endSample = m_snapshot.loop.endSample;
    }
    else
    {
        opts.startSample = 0;
        opts.endSample = 0;
    }

    // Format
    const int formatId = m_formatCombo.getSelectedId();
    if (formatId == 2)
    {
        opts.bitDepth = media::ExportBitDepth::int16;
        opts.applyTpdfDither = true;
    }
    else if (formatId == 3)
    {
        opts.bitDepth = media::ExportBitDepth::float32;
        opts.applyTpdfDither = false;
    }
    else
    {
        opts.bitDepth = media::ExportBitDepth::int24;
        opts.applyTpdfDither = false;
    }

    // Sample rate
    const int srId = m_srCombo.getSelectedId();
    opts.sampleRate = (srId == 2) ? 44100 : ((srId == 3) ? 96000 : 48000);

    // Stems mode
    opts.exportStems = (m_modeCombo.getSelectedId() == 2);

    m_isRendering.store(true);
    m_cancelToken.store(false);
    m_startButton.setEnabled(false);
    m_cancelButton.setEnabled(true);
    m_statusLabel.setText("Rendering...", juce::dontSendNotification);

    m_startRender(
        opts,
        [this](float progress, const std::string& status) {
            juce::MessageManager::callAsync([this, progress, status] {
                m_progressVal = static_cast<double>(progress);
                m_statusLabel.setText(juce::String(status) + " (" + juce::String(static_cast<int>(progress * 100.0f)) + "%)",
                                      juce::dontSendNotification);
            });
        },
        &m_cancelToken,
        [this](bool success, const std::string& error) {
            juce::MessageManager::callAsync([this, success, error] {
                m_isRendering.store(false);
                m_startButton.setEnabled(true);
                m_cancelButton.setEnabled(false);
                if (success)
                {
                    m_progressVal = 1.0;
                    m_statusLabel.setText("Bounce completed successfully!", juce::dontSendNotification);
                    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(70, 220, 100));
                }
                else
                {
                    m_statusLabel.setText("Render failed: " + juce::String(error), juce::dontSendNotification);
                    m_statusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 70, 70));
                }
            });
        });
}

void BounceComponent::cancelBounce()
{
    if (m_isRendering.load())
    {
        m_cancelToken.store(true);
        m_statusLabel.setText("Cancelling render...", juce::dontSendNotification);
    }
}

void BounceComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 26, 32));
}

void BounceComponent::resized()
{
    auto area = getLocalBounds().reduced(16);

    // Row 1: File destination
    auto row1 = area.removeFromTop(28);
    m_destLabel.setBounds(row1.removeFromLeft(120));
    m_browseButton.setBounds(row1.removeFromRight(80));
    row1.removeFromRight(6);
    m_destPathEdit.setBounds(row1);
    area.removeFromTop(10);

    // Row 2: Range
    auto row2 = area.removeFromTop(28);
    m_rangeLabel.setBounds(row2.removeFromLeft(120));
    m_rangeCombo.setBounds(row2);
    area.removeFromTop(10);

    // Row 3: Format
    auto row3 = area.removeFromTop(28);
    m_formatLabel.setBounds(row3.removeFromLeft(120));
    m_formatCombo.setBounds(row3);
    area.removeFromTop(10);

    // Row 4: Sample Rate
    auto row4 = area.removeFromTop(28);
    m_srLabel.setBounds(row4.removeFromLeft(120));
    m_srCombo.setBounds(row4);
    area.removeFromTop(10);

    // Row 5: Render Mode
    auto row5 = area.removeFromTop(28);
    m_modeLabel.setBounds(row5.removeFromLeft(120));
    m_modeCombo.setBounds(row5);
    area.removeFromTop(16);

    // Progress Bar
    m_progressBar.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    m_statusLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(12);

    // Bottom buttons
    auto btnRow = area.removeFromTop(32);
    m_startButton.setBounds(btnRow.removeFromLeft(140));
    btnRow.removeFromLeft(12);
    m_cancelButton.setBounds(btnRow.removeFromLeft(100));
}

BounceWindow::BounceWindow(project::ProjectSnapshot snapshot, BounceComponent::StartRenderFn startRender)
    : DocumentWindow("Export Master Bounce & Stems",
                     juce::Colour::fromRGB(24, 26, 32),
                     DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new BounceComponent(std::move(snapshot), std::move(startRender)), true);
    setResizable(true, false);
    centreWithSize(540, 390);
    setVisible(true);
}

void BounceWindow::closeButtonPressed()
{
    delete this;
}

void BounceWindow::open(project::ProjectSnapshot snapshot, BounceComponent::StartRenderFn startRender)
{
    new BounceWindow(std::move(snapshot), std::move(startRender));
}

} // namespace dawg::ui
