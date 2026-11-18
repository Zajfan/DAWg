#include "SpectralRepairDialog.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

std::unique_ptr<SpectralRepairDialog> SpectralRepairDialog::s_instance = nullptr;

void SpectralRepairDialog::open(const EditorViewModel& model)
{
    if (! s_instance)
    {
        s_instance = std::unique_ptr<SpectralRepairDialog>(new SpectralRepairDialog(model));
        s_instance->centreWithSize(860, 560);
        s_instance->setVisible(true);
    }
    else
    {
        s_instance->toFront(true);
    }
}

void SpectralRepairDialog::close()
{
    s_instance.reset();
}

SpectralRepairDialog::SpectralRepairDialog(const EditorViewModel& model)
    : juce::DocumentWindow(
          "Spectral Repair & Inpainting Studio - DAWg",
          juce::Colour::fromRGB(18, 20, 26),
          juce::DocumentWindow::closeButton,
          true)
    , m_content(model)
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_content, true);
    setResizable(true, true);
    setResizeLimits(700, 440, 1400, 900);
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
    // Mode selector
    m_modeSelector.addItem("Surgical Attenuate (Clicks / Pops / Transients)", 1);
    m_modeSelector.addItem("Autoregressive Neighbor Interpolation", 2);
    m_modeSelector.addItem("De-Hum 50 Hz & Harmonics (Europe)", 3);
    m_modeSelector.addItem("De-Hum 60 Hz & Harmonics (US / Americas)", 4);
    m_modeSelector.setSelectedId(1);
    m_modeSelector.onChange = [this] {
        const int modeId = m_modeSelector.getSelectedId();
        m_attenSlider.setEnabled(modeId == 1);
    };
    addAndMakeVisible(m_modeSelector);

    // Tool selector
    m_toolSelector.addItem("Box Selection", 1);
    m_toolSelector.addItem("Spectral Brush", 2);
    m_toolSelector.setSelectedId(1);
    m_toolSelector.onChange = [this] {
        const bool isBrush = (m_toolSelector.getSelectedId() == 2);
        m_brushRadiusSlider.setEnabled(isBrush);
        m_selectionRect = {};
        m_brushPoints.clear();
        repaint();
    };
    addAndMakeVisible(m_toolSelector);

    // Attenuation slider
    m_attenSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_attenSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    m_attenSlider.setRange(-96.0, 0.0, 1.0);
    m_attenSlider.setValue(-18.0);
    m_attenSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(m_attenSlider);

    // Brush radius slider
    m_brushRadiusSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    m_brushRadiusSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    m_brushRadiusSlider.setRange(4.0, 40.0, 1.0);
    m_brushRadiusSlider.setValue(14.0);
    m_brushRadiusSlider.setTextValueSuffix(" px");
    m_brushRadiusSlider.setEnabled(false);
    addAndMakeVisible(m_brushRadiusSlider);

    // Action buttons
    m_btnApply.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 120, 200));
    m_btnApply.onClick = [this] { runRepair(); };
    addAndMakeVisible(m_btnApply);

    m_btnReset.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 58));
    m_btnReset.onClick = [this] { resetAudio(); };
    addAndMakeVisible(m_btnReset);

    m_btnPreview.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(40, 160, 100));
    m_btnPreview.onClick = [this] { toggleAudition(); };
    addAndMakeVisible(m_btnPreview);

    // Status label
    m_lblStatus.setFont(juce::FontOptions(11.0f));
    m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 210));
    m_lblStatus.setText("Drag on spectrogram to select time-frequency region for surgical repair.", juce::dontSendNotification);
    addAndMakeVisible(m_lblStatus);

    resetAudio();
}

void SpectralRepairDialog::ContentComponent::resetAudio()
{
    // Synthesize reference musical signal with 1kHz tone, 60Hz hum, and transient clicks
    m_audioBuffer.assign(48000, 0.0f);
    constexpr double sr = 48000.0;
    for (std::size_t i = 0; i < m_audioBuffer.size(); ++i)
    {
        const double t = static_cast<double>(i) / sr;
        // 1kHz fundamental tone + 60Hz electrical hum + 2.5kHz harmonics
        m_audioBuffer[i] = static_cast<float>(
            0.45 * std::sin(2.0 * std::numbers::pi * 1000.0 * t)
            + 0.18 * std::sin(2.0 * std::numbers::pi * 60.0 * t)
            + 0.12 * std::sin(2.0 * std::numbers::pi * 120.0 * t)
            + 0.15 * std::sin(2.0 * std::numbers::pi * 2500.0 * t));

        // Periodic clicks / plosives
        if (i % 9600 < 40)
        {
            m_audioBuffer[i] += 0.85f * (1.0f - static_cast<float>(i % 9600) / 40.0f);
        }
    }

    m_spectrogram = engine::SpectralRepairEngine::computeSpectrogram(
        m_audioBuffer.data(), m_audioBuffer.size(), sr);
    m_selectionRect = {};
    m_brushPoints.clear();
    m_lblStatus.setText("Audio reset to reference signal with hum and clicks.", juce::dontSendNotification);
    m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 190, 210));
    repaint();
}

void SpectralRepairDialog::ContentComponent::toggleAudition()
{
    m_isPlaying = ! m_isPlaying;
    m_btnPreview.setButtonText(m_isPlaying ? "Stop Preview" : "Audition Preview");
    m_btnPreview.setColour(
        juce::TextButton::buttonColourId,
        m_isPlaying ? juce::Colour::fromRGB(210, 60, 60) : juce::Colour::fromRGB(40, 160, 100));
}

void SpectralRepairDialog::ContentComponent::runRepair()
{
    if (m_audioBuffer.empty()) return;

    const int modeId = m_modeSelector.getSelectedId();
    engine::SpectralRepairMode repairMode = engine::SpectralRepairMode::attenuate;
    if (modeId == 2) repairMode = engine::SpectralRepairMode::interpolate;
    else if (modeId == 3) repairMode = engine::SpectralRepairMode::deHum50Hz;
    else if (modeId == 4) repairMode = engine::SpectralRepairMode::deHum60Hz;

    constexpr double sr = 48000.0;
    const double totalDurationSec = static_cast<double>(m_audioBuffer.size()) / sr;

    engine::SpectralRegion region;
    region.attenuationDb = static_cast<float>(m_attenSlider.getValue());

    if (repairMode == engine::SpectralRepairMode::deHum50Hz || repairMode == engine::SpectralRepairMode::deHum60Hz)
    {
        // Global hum filter covers all time and full spectrum
        region.startTimeSec = 0.0;
        region.endTimeSec = totalDurationSec;
        region.lowFrequencyHz = 20.0;
        region.highFrequencyHz = 20000.0;
    }
    else
    {
        juce::Rectangle<int> targetBounds = m_selectionRect;
        if (m_toolSelector.getSelectedId() == 2 && ! m_brushPoints.empty())
        {
            // Derive bounding box from brush points
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            const float rad = static_cast<float>(m_brushRadiusSlider.getValue());
            for (const auto& pt : m_brushPoints)
            {
                minX = std::min(minX, pt.x - rad);
                maxX = std::max(maxX, pt.x + rad);
                minY = std::min(minY, pt.y - rad);
                maxY = std::max(maxY, pt.y + rad);
            }
            targetBounds = juce::Rectangle<int>(
                static_cast<int>(minX), static_cast<int>(minY),
                static_cast<int>(std::max(1.0f, maxX - minX)),
                static_cast<int>(std::max(1.0f, maxY - minY)));
        }

        if (targetBounds.isEmpty())
        {
            m_lblStatus.setText("No selection! Click or drag across the spectrogram first.", juce::dontSendNotification);
            m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 160, 50));
            return;
        }

        // Map pixel coordinates inside spectrogram display area to time and frequency
        auto area = getLocalBounds().reduced(16, 50);
        area.removeFromLeft(55); // Skip frequency ruler
        area.removeFromTop(20);  // Skip time ruler

        const float normT0 = std::clamp(static_cast<float>(targetBounds.getX() - area.getX()) / static_cast<float>(area.getWidth()), 0.0f, 1.0f);
        const float normT1 = std::clamp(static_cast<float>(targetBounds.getRight() - area.getX()) / static_cast<float>(area.getWidth()), 0.0f, 1.0f);

        const float normYTop = std::clamp(static_cast<float>(targetBounds.getY() - area.getY()) / static_cast<float>(area.getHeight()), 0.0f, 1.0f);
        const float normYBottom = std::clamp(static_cast<float>(targetBounds.getBottom() - area.getY()) / static_cast<float>(area.getHeight()), 0.0f, 1.0f);

        region.startTimeSec = static_cast<double>(normT0) * totalDurationSec;
        region.endTimeSec = static_cast<double>(normT1) * totalDurationSec;

        // Logarithmic frequency conversion: Y=0 (top) is 20000 Hz, Y=1 (bottom) is 20 Hz
        region.highFrequencyHz = 20.0 * std::pow(1000.0, 1.0 - static_cast<double>(normYTop));
        region.lowFrequencyHz = 20.0 * std::pow(1000.0, 1.0 - static_cast<double>(normYBottom));
    }

    const bool ok = engine::SpectralRepairEngine::repairAudio(
        m_audioBuffer.data(), m_audioBuffer.size(), sr, region, repairMode);

    if (ok)
    {
        m_spectrogram = engine::SpectralRepairEngine::computeSpectrogram(
            m_audioBuffer.data(), m_audioBuffer.size(), sr);
        m_selectionRect = {};
        m_brushPoints.clear();

        const auto statusText = "Repaired: "
            + juce::String(region.startTimeSec, 2) + "s.."
            + juce::String(region.endTimeSec, 2) + "s, "
            + juce::String(static_cast<int>(region.lowFrequencyHz)) + "Hz.."
            + juce::String(static_cast<int>(region.highFrequencyHz)) + "Hz"
            + (repairMode == engine::SpectralRepairMode::interpolate ? " [Interpolated]" : " [Attenuated]");
        m_lblStatus.setText(statusText, juce::dontSendNotification);
        m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(80, 230, 120));
    }
    else
    {
        m_lblStatus.setText("Spectral repair failed: invalid range.", juce::dontSendNotification);
        m_lblStatus.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 80, 80));
    }

    repaint();
}

void SpectralRepairDialog::ContentComponent::mouseDown(const juce::MouseEvent& e)
{
    if (m_toolSelector.getSelectedId() == 2)
    {
        m_brushPoints.clear();
        m_brushPoints.push_back(e.position);
    }
    else
    {
        m_selectionRect = juce::Rectangle<int>(e.getPosition(), e.getPosition());
    }
    m_isSelecting = true;
    repaint();
}

void SpectralRepairDialog::ContentComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (! m_isSelecting) return;

    if (m_toolSelector.getSelectedId() == 2)
    {
        m_brushPoints.push_back(e.position);
    }
    else
    {
        m_selectionRect.setSize(e.x - m_selectionRect.getX(), e.y - m_selectionRect.getY());
    }
    repaint();
}

void SpectralRepairDialog::ContentComponent::mouseUp(const juce::MouseEvent&)
{
    m_isSelecting = false;
    repaint();
}

void SpectralRepairDialog::ContentComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    auto totalArea = getLocalBounds().reduced(16, 50);

    // Frequency ruler area (left 55px)
    auto freqRulerArea = totalArea.removeFromLeft(55);
    // Time ruler area (top 20px)
    auto timeRulerArea = totalArea.removeFromTop(20);
    auto specBounds = totalArea;

    // 1. Draw Frequency Ruler on Left
    g.setColour(juce::Colour::fromRGB(22, 25, 33));
    g.fillRect(freqRulerArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 65));
    g.drawRect(freqRulerArea, 1);

    constexpr std::array<int, 9> rulerFreqs = {20000, 10000, 5000, 2500, 1000, 500, 250, 100, 50};
    g.setFont(juce::FontOptions(10.0f));
    for (int f : rulerFreqs)
    {
        // Logarithmic norm from 20 Hz (y=1.0) to 20000 Hz (y=0.0)
        const float norm = 1.0f - static_cast<float>(std::log10(static_cast<double>(f) / 20.0) / std::log10(1000.0));
        const float y = static_cast<float>(specBounds.getY()) + norm * static_cast<float>(specBounds.getHeight());

        g.setColour(juce::Colour::fromRGB(80, 90, 110));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(freqRulerArea.getRight() - 6), static_cast<float>(freqRulerArea.getRight()));

        const juce::String label = (f >= 1000) ? (juce::String(f / 1000) + "k") : juce::String(f);
        g.setColour(juce::Colour::fromRGB(160, 175, 195));
        g.drawText(label, freqRulerArea.getX() + 2, static_cast<int>(y) - 7, 44, 14, juce::Justification::centredRight);
    }

    // 2. Draw Time Ruler on Top
    g.setColour(juce::Colour::fromRGB(22, 25, 33));
    g.fillRect(timeRulerArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 65));
    g.drawRect(timeRulerArea, 1);

    for (int s = 0; s <= 10; s += 2)
    {
        const float norm = static_cast<float>(s) / 10.0f;
        const float x = static_cast<float>(specBounds.getX()) + norm * static_cast<float>(specBounds.getWidth());

        g.setColour(juce::Colour::fromRGB(80, 90, 110));
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(timeRulerArea.getBottom() - 5), static_cast<float>(timeRulerArea.getBottom()));

        const juce::String label = juce::String(static_cast<float>(s) * 0.1f, 1) + "s";
        g.setColour(juce::Colour::fromRGB(160, 175, 195));
        g.drawText(label, static_cast<int>(x) - 16, timeRulerArea.getY() + 2, 32, 14, juce::Justification::centred);
    }

    // 3. Draw 2D Spectrogram Display Area
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
                if (val < 0.04f) continue;

                // Magma palette: dark purple -> vibrant magenta -> gold -> bright white
                const auto col = juce::Colour::fromHSV(0.08f + (1.0f - val) * 0.72f, 0.92f, val * 0.98f, 1.0f);
                g.setColour(col);
                const float px = static_cast<float>(specBounds.getX()) + static_cast<float>(f) * dx;
                const float py = static_cast<float>(specBounds.getBottom()) - static_cast<float>(b) * dy;
                g.fillRect(px, py, dx * 2.0f, dy * 2.0f);
            }
        }
    }

    // 4. Draw User Selection (Box or Brush)
    if (m_toolSelector.getSelectedId() == 1 && ! m_selectionRect.isEmpty())
    {
        auto normalized = m_selectionRect.transformedBy(juce::AffineTransform {});
        g.setColour(juce::Colour::fromRGBA(0, 200, 255, 60));
        g.fillRect(normalized);
        g.setColour(juce::Colour::fromRGB(0, 220, 255));
        g.drawRect(normalized, 1.5f);
    }
    else if (m_toolSelector.getSelectedId() == 2 && ! m_brushPoints.empty())
    {
        const float rad = static_cast<float>(m_brushRadiusSlider.getValue());
        g.setColour(juce::Colour::fromRGBA(255, 140, 0, 70));
        for (const auto& pt : m_brushPoints)
        {
            g.fillEllipse(pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f);
        }
        g.setColour(juce::Colour::fromRGB(255, 180, 50));
        for (const auto& pt : m_brushPoints)
        {
            g.drawEllipse(pt.x - rad, pt.y - rad, rad * 2.0f, rad * 2.0f, 1.0f);
        }
    }
}

void SpectralRepairDialog::ContentComponent::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(44).reduced(8, 4);

    m_modeSelector.setBounds(topBar.removeFromLeft(250));
    topBar.removeFromLeft(6);
    m_toolSelector.setBounds(topBar.removeFromLeft(120));
    topBar.removeFromLeft(6);
    m_attenSlider.setBounds(topBar.removeFromLeft(130));
    topBar.removeFromLeft(6);
    m_brushRadiusSlider.setBounds(topBar.removeFromLeft(110));
    topBar.removeFromLeft(6);
    m_btnApply.setBounds(topBar.removeFromLeft(140));

    auto bottomBar = area.removeFromBottom(28).reduced(12, 4);
    m_btnPreview.setBounds(bottomBar.removeFromRight(120));
    bottomBar.removeFromRight(8);
    m_btnReset.setBounds(bottomBar.removeFromRight(90));
    bottomBar.removeFromRight(8);
    m_lblStatus.setBounds(bottomBar);
}

} // namespace dawg::ui
