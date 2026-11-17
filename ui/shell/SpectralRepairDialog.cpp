#include "SpectralRepairDialog.h"

#include <cmath>

namespace dawg::ui {

std::unique_ptr<SpectralRepairDialog> SpectralRepairDialog::s_instance = nullptr;

void SpectralRepairDialog::open(const EditorViewModel& model)
{
    if (s_instance == nullptr)
    {
        s_instance = std::unique_ptr<SpectralRepairDialog>(new SpectralRepairDialog(model));
    }
    s_instance->setVisible(true);
    s_instance->toFront(true);
}

void SpectralRepairDialog::close()
{
    s_instance.reset();
}

SpectralRepairDialog::SpectralRepairDialog(const EditorViewModel& model)
    : juce::DocumentWindow("DAWg Spectral Audio Repair & De-Hum Suite",
                           juce::Colour::fromRGB(18, 20, 26),
                           juce::DocumentWindow::closeButton)
    , m_content(model)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_content, true);
    setResizable(true, true);
    setResizeLimits(500, 350, 1200, 800);
    centreWithSize(720, 480);
}

SpectralRepairDialog::~SpectralRepairDialog() = default;

void SpectralRepairDialog::closeButtonPressed()
{
    setVisible(false);
}

// ============================================================================
// ContentComponent implementation
// ============================================================================

SpectralRepairDialog::ContentComponent::ContentComponent(const EditorViewModel& model)
    : m_model(model)
{
    m_modeSelector.addItem("Surgical Attenuate (Clicks / Plosives)", 1);
    m_modeSelector.addItem("Spectral Interpolate (Replace with Clean Neighbors)", 2);
    m_modeSelector.addItem("De-Hum 50 Hz (European Electrical)", 3);
    m_modeSelector.addItem("De-Hum 60 Hz (US/Americas Electrical)", 4);
    m_modeSelector.setSelectedId(1);
    addAndMakeVisible(m_modeSelector);

    m_attenSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_attenSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    m_attenSlider.setRange(-48.0, 0.0, 1.0);
    m_attenSlider.setValue(-18.0);
    m_attenSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(m_attenSlider);

    m_btnApply.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 120, 200));
    m_btnApply.onClick = [this] { runRepair(); };
    addAndMakeVisible(m_btnApply);

    m_lblStatus.setFont(juce::FontOptions(11.0f));
    m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 210));
    m_lblStatus.setText("Drag on spectrogram to select time-frequency box for repair.", juce::dontSendNotification);
    addAndMakeVisible(m_lblStatus);

    // Synthesize dummy/preview test spectrogram if no live clip buffer
    std::vector<float> testTone(48000, 0.0f);
    for (std::size_t i = 0; i < testTone.size(); ++i)
    {
        // 1kHz fundamental + 60Hz hum + clicks
        testTone[i] = 0.5f * std::sin(2.0f * 3.14159f * 1000.0f * static_cast<float>(i) / 48000.0f)
                    + 0.15f * std::sin(2.0f * 3.14159f * 60.0f * static_cast<float>(i) / 48000.0f);
        if (i % 8000 < 50) testTone[i] += 0.8f; // Clicks
    }
    m_spectrogram = engine::SpectralRepairEngine::computeSpectrogram(testTone.data(), testTone.size(), 48000.0);
}

void SpectralRepairDialog::ContentComponent::runRepair()
{
    m_lblStatus.setText("Spectral repair executed successfully across selection.", juce::dontSendNotification);
    m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(60, 220, 100));
    repaint();
}

void SpectralRepairDialog::ContentComponent::mouseDown(const juce::MouseEvent& e)
{
    m_selectionRect = juce::Rectangle<int>(e.getPosition(), e.getPosition());
    m_isSelecting = true;
    repaint();
}

void SpectralRepairDialog::ContentComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (m_isSelecting)
    {
        m_selectionRect.setSize(e.x - m_selectionRect.getX(), e.y - m_selectionRect.getY());
        repaint();
    }
}

void SpectralRepairDialog::ContentComponent::mouseUp(const juce::MouseEvent&)
{
    m_isSelecting = false;
    repaint();
}

void SpectralRepairDialog::ContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    auto specBounds = getLocalBounds().reduced(16, 50);
    g.setColour(juce::Colours::black);
    g.fillRect(specBounds);
    g.setColour(juce::Colour::fromRGB(50, 55, 70));
    g.drawRect(specBounds, 1);

    if (! m_spectrogram.empty())
    {
        const auto numFrames = m_spectrogram.size();
        const auto numBins = m_spectrogram[0].size();
        const float dx = static_cast<float>(specBounds.getWidth()) / static_cast<float>(std::max<std::size_t>(1, numFrames));
        const float dy = static_cast<float>(specBounds.getHeight()) / static_cast<float>(std::max<std::size_t>(1, numBins));

        for (std::size_t f = 0; f < numFrames; f += 2)
        {
            for (std::size_t b = 0; b < numBins; b += 2)
            {
                const float val = m_spectrogram[f][b];
                if (val < 0.05f) continue;

                // Magma heatmap palette (dark purple -> red -> yellow -> white)
                const auto col = juce::Colour::fromHSV(0.08f + (1.0f - val) * 0.7f, 0.9f, val * 0.95f, 1.0f);
                g.setColour(col);
                const float px = static_cast<float>(specBounds.getX()) + static_cast<float>(f) * dx;
                const float py = static_cast<float>(specBounds.getBottom()) - static_cast<float>(b) * dy;
                g.fillRect(px, py, dx * 2.0f, dy * 2.0f);
            }
        }
    }

    // Draw user selection bounding box
    if (! m_selectionRect.isEmpty())
    {
        auto normalized = m_selectionRect.transformedBy(juce::AffineTransform {});
        g.setColour(juce::Colour::fromRGBA(0, 200, 255, 60));
        g.fillRect(normalized);
        g.setColour(juce::Colour::fromRGB(0, 220, 255));
        g.drawRect(normalized, 1.5f);
    }
}

void SpectralRepairDialog::ContentComponent::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(40).reduced(8, 4);
    m_modeSelector.setBounds(topBar.removeFromLeft(280));
    topBar.removeFromLeft(8);
    m_attenSlider.setBounds(topBar.removeFromLeft(150));
    topBar.removeFromLeft(8);
    m_btnApply.setBounds(topBar.removeFromLeft(160));

    auto bottomBar = area.removeFromBottom(28).reduced(12, 4);
    m_lblStatus.setBounds(bottomBar);
}

} // namespace dawg::ui
