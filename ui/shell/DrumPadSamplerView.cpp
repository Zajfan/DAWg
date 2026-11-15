#include "DrumPadSamplerView.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace dawg::ui {

DrumPadSamplerView::DrumPadSamplerView(CommandHandler handler, AuditionCallback auditionCb)
    : m_commandHandler(std::move(handler))
    , m_auditionCallback(std::move(auditionCb))
{
    m_sampler.prepare(48000.0, 512, 2);

    m_titleLabel.setText("Drum Pad Sampler & Transient Slicer", juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(220, 225, 235));
    addAndMakeVisible(m_titleLabel);

    m_sliceButton.onClick = [this] {
        // Create synthetic loop or open file chooser to auto-slice
        std::vector<float> syntheticBreak(48000 * 2, 0.0f);
        for (int t = 0; t < 16; ++t)
        {
            const std::size_t onset = static_cast<std::size_t>(t * 6000);
            for (std::size_t j = 0; j < 300 && (onset + j) < syntheticBreak.size(); ++j)
                syntheticBreak[onset + j] = 0.9f * std::sin(static_cast<float>(j) * 0.15f);
        }
        m_sampler.sliceSampleAcrossPads(syntheticBreak.data(), syntheticBreak.size(), 48000.0, 16);
        updateControlsForSelectedPad();
        repaint();
    };
    m_sliceButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 95, 145));
    addAndMakeVisible(m_sliceButton);

    m_loadSampleButton.onClick = [this] {
        // Populate standard sample
    };
    m_loadSampleButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(38, 41, 48));
    addAndMakeVisible(m_loadSampleButton);

    // Inspector
    m_padTitleLabel.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    m_padTitleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(240, 180, 70));
    addAndMakeVisible(m_padTitleLabel);

    // Pitch
    m_pitchSlider.setRange(-24.0, 24.0, 0.5);
    m_pitchSlider.setValue(0.0, juce::dontSendNotification);
    m_pitchSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_pitchSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    m_pitchSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).pitchSemi = static_cast<float>(m_pitchSlider.getValue());
    };
    addAndMakeVisible(m_pitchSlider);
    m_pitchLabel.setText("Pitch (st)", juce::dontSendNotification);
    m_pitchLabel.setFont(juce::FontOptions(10.0f));
    m_pitchLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_pitchLabel);

    // Reverse
    m_reverseToggle.onClick = [this] {
        m_sampler.pad(m_selectedPad).reverse = m_reverseToggle.getToggleState();
        repaint();
    };
    addAndMakeVisible(m_reverseToggle);

    // Choke
    m_chokeCombo.addItem("Choke: None", 1);
    for (int c = 1; c <= 8; ++c)
        m_chokeCombo.addItem("Choke Group " + std::to_string(c), c + 1);
    m_chokeCombo.setSelectedId(1, juce::dontSendNotification);
    m_chokeCombo.onChange = [this] {
        m_sampler.pad(m_selectedPad).chokeGroup = m_chokeCombo.getSelectedId() - 1;
    };
    addAndMakeVisible(m_chokeCombo);

    // ADSR
    auto setupSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& text, double min, double max, double val) {
        s.setRange(min, max, (max - min) / 100.0);
        s.setValue(val, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearBarVertical);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(s);
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(9.0f));
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };

    setupSlider(m_attackSlider, m_attackLabel, "A", 0.001, 0.5, 0.002);
    m_attackSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).attackSec = static_cast<float>(m_attackSlider.getValue());
    };

    setupSlider(m_decaySlider, m_decayLabel, "D", 0.01, 1.5, 0.18);
    m_decaySlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).decaySec = static_cast<float>(m_decaySlider.getValue());
    };

    setupSlider(m_sustainSlider, m_sustainLabel, "S", 0.0, 1.0, 1.0);
    m_sustainSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).sustainLevel = static_cast<float>(m_sustainSlider.getValue());
    };

    setupSlider(m_releaseSlider, m_releaseLabel, "R", 0.01, 2.0, 0.25);
    m_releaseSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).releaseSec = static_cast<float>(m_releaseSlider.getValue());
    };

    // Filter
    m_filterCutoffSlider.setRange(20.0, 20000.0, 1.0);
    m_filterCutoffSlider.setSkewFactorFromMidPoint(1000.0);
    m_filterCutoffSlider.setValue(20000.0, juce::dontSendNotification);
    m_filterCutoffSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_filterCutoffSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_filterCutoffSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).filterCutoffHz = static_cast<float>(m_filterCutoffSlider.getValue());
    };
    addAndMakeVisible(m_filterCutoffSlider);
    m_filterCutoffLabel.setText("Cutoff", juce::dontSendNotification);
    m_filterCutoffLabel.setFont(juce::FontOptions(9.0f));
    m_filterCutoffLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_filterCutoffLabel);

    m_filterResoSlider.setRange(0.1, 8.0, 0.1);
    m_filterResoSlider.setValue(0.707, juce::dontSendNotification);
    m_filterResoSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_filterResoSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_filterResoSlider.onValueChange = [this] {
        m_sampler.pad(m_selectedPad).filterResonance = static_cast<float>(m_filterResoSlider.getValue());
    };
    addAndMakeVisible(m_filterResoSlider);
    m_filterResoLabel.setText("Reso", juce::dontSendNotification);
    m_filterResoLabel.setFont(juce::FontOptions(9.0f));
    m_filterResoLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(m_filterResoLabel);

    updateControlsForSelectedPad();
    startTimerHz(30);
}

DrumPadSamplerView::~DrumPadSamplerView()
{
    stopTimer();
}

void DrumPadSamplerView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

void DrumPadSamplerView::updateControlsForSelectedPad()
{
    const auto& p = m_sampler.pad(m_selectedPad);
    m_padTitleLabel.setText("Pad " + std::to_string(m_selectedPad + 1) + ": " + p.name, juce::dontSendNotification);
    m_pitchSlider.setValue(static_cast<double>(p.pitchSemi), juce::dontSendNotification);
    m_reverseToggle.setToggleState(p.reverse, juce::dontSendNotification);
    m_chokeCombo.setSelectedId(p.chokeGroup + 1, juce::dontSendNotification);
    m_attackSlider.setValue(static_cast<double>(p.attackSec), juce::dontSendNotification);
    m_decaySlider.setValue(static_cast<double>(p.decaySec), juce::dontSendNotification);
    m_sustainSlider.setValue(static_cast<double>(p.sustainLevel), juce::dontSendNotification);
    m_releaseSlider.setValue(static_cast<double>(p.releaseSec), juce::dontSendNotification);
    m_filterCutoffSlider.setValue(static_cast<double>(p.filterCutoffHz), juce::dontSendNotification);
    m_filterResoSlider.setValue(static_cast<double>(p.filterResonance), juce::dontSendNotification);
}

void DrumPadSamplerView::timerCallback()
{
    bool needRepaint = false;
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (m_padLitGlow[i] > 0.01f)
        {
            m_padLitGlow[i] = std::max(0.0f, m_padLitGlow[i] - 0.12f);
            needRepaint = true;
        }
    }
    if (needRepaint)
        repaint();
}

void DrumPadSamplerView::triggerPad(std::size_t padIdx, float velocity)
{
    if (padIdx >= 16)
        return;

    m_sampler.triggerPad(padIdx, velocity);
    m_padLitGlow[padIdx] = 1.0f;
    m_selectedPad = padIdx;
    updateControlsForSelectedPad();

    if (m_auditionCallback)
    {
        m_auditionCallback(static_cast<std::uint8_t>(36 + padIdx), static_cast<std::uint8_t>(velocity * 127.0f), true);
    }
    repaint();
}

bool DrumPadSamplerView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".wav") || file.endsWithIgnoreCase(".mp3") ||
            file.endsWithIgnoreCase(".flac") || file.endsWithIgnoreCase(".aif"))
            return true;
    }
    return false;
}

void DrumPadSamplerView::filesDropped(const juce::StringArray& files, int x, int y)
{
    if (files.isEmpty())
        return;

    const int pad = getPadAt({x, y});
    const auto targetPad = (pad >= 0) ? static_cast<std::size_t>(pad) : m_selectedPad;

    const juce::File f(files[0]);
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));

    if (reader)
    {
        const auto numSamples = static_cast<std::size_t>(reader->lengthInSamples);
        std::vector<float> l(numSamples);
        std::vector<float> r(numSamples);
        float* channels[2] = { l.data(), r.data() };
        reader->read(channels, 2, 0, static_cast<int>(numSamples));
        m_sampler.loadSampleIntoPad(targetPad, std::move(l), std::move(r), f.getFileNameWithoutExtension().toStdString());
        m_selectedPad = targetPad;
        updateControlsForSelectedPad();
        repaint();
    }
}

int DrumPadSamplerView::getPadAt(juce::Point<int> pos) const noexcept
{
    const int padGridWidth = 320;
    const int startX = 12;
    const int startY = 46;
    const int gridHeight = getHeight() - startY - 12;

    if (pos.getX() < startX || pos.getX() >= startX + padGridWidth ||
        pos.getY() < startY || pos.getY() >= startY + gridHeight)
        return -1;

    const int col = (pos.getX() - startX) / (padGridWidth / 4);
    const int row = (pos.getY() - startY) / (gridHeight / 4);

    if (col < 0 || col >= 4 || row < 0 || row >= 4)
        return -1;

    // MPC grid mapping: row 3 (bottom) is pads 0..3, row 0 (top) is pads 12..15
    const int padIdx = (3 - row) * 4 + col;
    return padIdx;
}

void DrumPadSamplerView::mouseDown(const juce::MouseEvent& e)
{
    const int pad = getPadAt(e.getPosition());
    if (pad >= 0)
    {
        // Calculate velocity based on vertical position within pad (top = 1.0, bottom = 0.4)
        const int startY = 46;
        const int gridHeight = getHeight() - startY - 12;
        const int padH = gridHeight / 4;
        const int localY = (e.getPosition().getY() - startY) % padH;
        const float velocity = std::clamp(1.0f - (static_cast<float>(localY) / static_cast<float>(padH)) * 0.6f, 0.4f, 1.0f);

        triggerPad(static_cast<std::size_t>(pad), velocity);
        return;
    }

    // Check waveform start/end handle drag
    if (m_waveformBounds.contains(e.getPosition()))
    {
        const float normX = static_cast<float>(e.getPosition().getX() - m_waveformBounds.getX()) / static_cast<float>(m_waveformBounds.getWidth());
        auto& p = m_sampler.pad(m_selectedPad);

        if (std::abs(normX - p.startNorm) < 0.08f)
            m_draggingStartHandle = true;
        else if (std::abs(normX - p.endNorm) < 0.08f)
            m_draggingEndHandle = true;
    }
}

void DrumPadSamplerView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_waveformBounds.getWidth() <= 0)
        return;

    const float normX = std::clamp(static_cast<float>(e.getPosition().getX() - m_waveformBounds.getX()) / static_cast<float>(m_waveformBounds.getWidth()), 0.0f, 1.0f);
    auto& p = m_sampler.pad(m_selectedPad);

    if (m_draggingStartHandle)
    {
        p.startNorm = std::min(normX, p.endNorm - 0.02f);
        repaint();
    }
    else if (m_draggingEndHandle)
    {
        p.endNorm = std::max(normX, p.startNorm + 0.02f);
        repaint();
    }
}

void DrumPadSamplerView::mouseUp(const juce::MouseEvent& /*e*/)
{
    m_draggingStartHandle = false;
    m_draggingEndHandle = false;
    if (m_auditionCallback)
    {
        m_auditionCallback(static_cast<std::uint8_t>(36 + m_selectedPad), 0, false);
    }
}

void DrumPadSamplerView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(32, 35, 41));

    // Top Header
    g.setColour(juce::Colour::fromRGB(26, 28, 33));
    g.fillRect(0, 0, getWidth(), 38);
    g.setColour(juce::Colour::fromRGB(50, 54, 62));
    g.drawHorizontalLine(37, 0.0f, static_cast<float>(getWidth()));

    // Draw Pad Grid
    const int padGridWidth = 320;
    const auto gridBounds = juce::Rectangle<int>(12, 46, padGridWidth, getHeight() - 58);
    drawPadGrid(g, gridBounds);

    // Draw Sound Design Inspector on Right
    const int inspectorX = padGridWidth + 24;
    const int inspectorW = getWidth() - inspectorX - 12;
    if (inspectorW > 100)
    {
        const auto inspectorBounds = juce::Rectangle<int>(inspectorX, 46, inspectorW, getHeight() - 58);
        g.setColour(juce::Colour::fromRGB(25, 27, 32));
        g.fillRoundedRectangle(inspectorBounds.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(48, 52, 60));
        g.drawRoundedRectangle(inspectorBounds.toFloat(), 4.0f, 1.0f);

        // Draw Waveform Box
        m_waveformBounds = juce::Rectangle<int>(inspectorX + 12, 74, inspectorW - 24, 80);
        drawWaveformEditor(g, m_waveformBounds);
    }
}

void DrumPadSamplerView::drawPadGrid(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const int padW = bounds.getWidth() / 4;
    const int padH = bounds.getHeight() / 4;

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            // MPC grid numbering (row 3 is bottom = 0..3)
            const std::size_t padIdx = static_cast<std::size_t>((3 - row) * 4 + col);
            const auto padBounds = juce::Rectangle<int>(
                bounds.getX() + col * padW,
                bounds.getY() + row * padH,
                padW, padH).reduced(3);

            drawPad(g, padIdx, padBounds);
        }
    }
}

void DrumPadSamplerView::drawPad(juce::Graphics& g, std::size_t padIdx, juce::Rectangle<int> bounds)
{
    const auto fBounds = bounds.toFloat();
    const bool isSelected = (padIdx == m_selectedPad);
    const float lit = m_padLitGlow[padIdx];
    const auto& p = m_sampler.pad(padIdx);

    // Rubber pad background
    juce::Colour baseColour = isSelected ? juce::Colour::fromRGB(55, 62, 75) : juce::Colour::fromRGB(38, 42, 50);
    if (lit > 0.01f)
    {
        baseColour = baseColour.interpolatedWith(juce::Colour::fromRGB(255, 140, 40), lit * 0.85f);
    }

    g.setColour(baseColour);
    g.fillRoundedRectangle(fBounds, 5.0f);

    // Bevel / Rim
    g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 50) : juce::Colour::fromRGB(60, 66, 78));
    g.drawRoundedRectangle(fBounds, 5.0f, isSelected ? 2.0f : 1.0f);

    // Pad number & Pitch name
    static const char* kNotes[] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1", "C2", "C#2", "D2", "D#2" };
    g.setColour(juce::Colour::fromRGB(150, 155, 168));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText(std::to_string(padIdx + 1) + " [" + kNotes[padIdx % 16] + "]",
               bounds.removeFromTop(16).reduced(6, 2), juce::Justification::topLeft);

    // Choke badge
    if (p.chokeGroup > 0)
    {
        g.setColour(juce::Colour::fromRGB(220, 90, 90));
        g.drawText("CH" + std::to_string(p.chokeGroup), bounds.removeFromTop(14).reduced(6, 0), juce::Justification::topRight);
    }

    // Pad Sound Name
    g.setColour(lit > 0.01f ? juce::Colour::fromRGB(255, 255, 255) : juce::Colour::fromRGB(220, 225, 235));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText(p.name, bounds.reduced(4, 2), juce::Justification::centred);
}

void DrumPadSamplerView::drawWaveformEditor(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(18, 20, 24));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

    const auto& p = m_sampler.pad(m_selectedPad);
    if (p.sampleL.empty())
    {
        g.setColour(juce::Colour::fromRGB(120, 125, 135));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("No Sample Data", bounds, juce::Justification::centred);
        return;
    }

    // Draw waveform peaks
    const auto w = bounds.getWidth();
    const auto midY = bounds.getCentreY();
    const auto amp = static_cast<float>(bounds.getHeight()) * 0.42f;
    const std::size_t sampleCount = p.sampleL.size();

    juce::Path wavePath;
    wavePath.preallocateSpace(w * 2);

    for (int x = 0; x < w; ++x)
    {
        const auto idx0 = static_cast<std::size_t>(static_cast<double>(x) / static_cast<double>(w) * static_cast<double>(sampleCount));
        const auto idx1 = static_cast<std::size_t>(static_cast<double>(x + 1) / static_cast<double>(w) * static_cast<double>(sampleCount));

        float minS = 0.0f;
        float maxS = 0.0f;
        for (std::size_t s = idx0; s < idx1 && s < sampleCount; ++s)
        {
            minS = std::min(minS, p.sampleL[s]);
            maxS = std::max(maxS, p.sampleL[s]);
        }

        const float topY = static_cast<float>(midY) - maxS * amp;
        const float btmY = static_cast<float>(midY) - minS * amp;
        g.setColour(juce::Colour::fromRGB(70, 180, 230));
        g.drawVerticalLine(bounds.getX() + x, topY, btmY);
    }

    // Draggable Start Handle (Yellow)
    const float startX = static_cast<float>(bounds.getX()) + p.startNorm * static_cast<float>(w);
    g.setColour(juce::Colour::fromRGB(250, 210, 50));
    g.drawVerticalLine(static_cast<int>(startX), static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    g.fillEllipse(startX - 4.0f, static_cast<float>(bounds.getY()), 8.0f, 8.0f);

    // Draggable End Handle (Orange)
    const float endX = static_cast<float>(bounds.getX()) + p.endNorm * static_cast<float>(w);
    g.setColour(juce::Colour::fromRGB(250, 110, 40));
    g.drawVerticalLine(static_cast<int>(endX), static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    g.fillEllipse(endX - 4.0f, static_cast<float>(bounds.getBottom() - 8), 8.0f, 8.0f);
}

void DrumPadSamplerView::resized()
{
    m_titleLabel.setBounds(12, 6, 280, 26);
    m_sliceButton.setBounds(getWidth() - 260, 6, 150, 26);
    m_loadSampleButton.setBounds(getWidth() - 100, 6, 88, 26);

    const int padGridWidth = 320;
    const int inspectorX = padGridWidth + 36;
    const int inspectorW = getWidth() - inspectorX - 24;

    m_padTitleLabel.setBounds(inspectorX, 48, inspectorW, 22);

    // Below Waveform: sound design controls
    const int ctrlY = 162;
    m_pitchSlider.setBounds(inspectorX, ctrlY, 52, 52);
    m_pitchLabel.setBounds(inspectorX, ctrlY + 54, 52, 14);

    m_reverseToggle.setBounds(inspectorX + 64, ctrlY + 12, 80, 22);
    m_chokeCombo.setBounds(inspectorX + 64, ctrlY + 38, 120, 22);

    // ADSR sliders
    const int adsrX = inspectorX + 196;
    m_attackSlider.setBounds(adsrX, ctrlY, 22, 50);
    m_attackLabel.setBounds(adsrX, ctrlY + 52, 22, 12);

    m_decaySlider.setBounds(adsrX + 26, ctrlY, 22, 50);
    m_decayLabel.setBounds(adsrX + 26, ctrlY + 52, 22, 12);

    m_sustainSlider.setBounds(adsrX + 52, ctrlY, 22, 50);
    m_sustainLabel.setBounds(adsrX + 52, ctrlY + 52, 22, 12);

    m_releaseSlider.setBounds(adsrX + 78, ctrlY, 22, 50);
    m_releaseLabel.setBounds(adsrX + 78, ctrlY + 52, 22, 12);

    // Filter knobs
    const int fltX = adsrX + 112;
    m_filterCutoffSlider.setBounds(fltX, ctrlY, 44, 44);
    m_filterCutoffLabel.setBounds(fltX, ctrlY + 46, 44, 12);

    m_filterResoSlider.setBounds(fltX + 48, ctrlY, 44, 44);
    m_filterResoLabel.setBounds(fltX + 48, ctrlY + 46, 44, 12);
}

} // namespace dawg::ui
