#include "DspEditorDialog.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

// ============================================================================
// DspEditorComponent
// ============================================================================

DspEditorComponent::DspEditorComponent(DspEditorKind kind, std::string title)
    : m_kind(kind)
    , m_title(std::move(title))
{
    if (m_title.empty())
    {
        switch (m_kind)
        {
        case DspEditorKind::parametricEq: m_title = "Parametric EQ"; break;
        case DspEditorKind::compressor: m_title = "Studio Dynamics Compressor"; break;
        case DspEditorKind::synth: m_title = "TNHC Polyphonic Subtractive Synth"; break;
        case DspEditorKind::reverb: m_title = "Studio Algorithmic Reverb"; break;
        case DspEditorKind::delay: m_title = "Tempo-Synced Stereo Delay"; break;
        }
    }

    m_titleLabel.setText(m_title, juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 235, 245));
    addAndMakeVisible(m_titleLabel);

    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(180, 185, 195));
    addAndMakeVisible(m_bypassButton);

    startTimerHz(60); // 60 FPS visualizer animation
}

DspEditorComponent::~DspEditorComponent()
{
    stopTimer();
}

void DspEditorComponent::timerCallback()
{
    m_simPhase += 0.05f;
    if (m_simPhase > 2.0f * static_cast<float>(std::numbers::pi))
        m_simPhase -= 2.0f * static_cast<float>(std::numbers::pi);

    // Update simulated spectrum for analyzer
    for (std::size_t i = 0; i < m_spectrumMagnitudes.size(); ++i)
    {
        const float noise = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 0.15f;
        const float wave = 0.3f * std::sin(m_simPhase + static_cast<float>(i) * 0.2f);
        const float base = std::exp(-static_cast<float>(i) * 0.04f);
        m_spectrumMagnitudes[i] = std::clamp(base * 0.6f + wave * 0.2f + noise, 0.0f, 1.0f);
    }

    // Update simulated compressor gain reduction
    if (m_kind == DspEditorKind::compressor)
    {
        const float cycle = std::sin(m_simPhase * 2.0f);
        m_compGainReductionDb = (cycle > 0.3f) ? (cycle * 8.0f) : 0.0f;
    }

    repaint();
}

void DspEditorComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto header = area.removeFromTop(28);
    m_titleLabel.setBounds(header.removeFromLeft(header.getWidth() - 90));
    m_bypassButton.setBounds(header);
}

void DspEditorComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 21, 26));

    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(32); // Header area

    // Draw frame border
    g.setColour(juce::Colour::fromRGB(40, 44, 56));
    g.drawRoundedRectangle(area.toFloat(), 4.0f, 1.0f);

    switch (m_kind)
    {
    case DspEditorKind::parametricEq: paintEq(g, area.reduced(4)); break;
    case DspEditorKind::compressor: paintCompressor(g, area.reduced(4)); break;
    case DspEditorKind::synth: paintSynth(g, area.reduced(4)); break;
    case DspEditorKind::reverb: paintReverb(g, area.reduced(4)); break;
    case DspEditorKind::delay: paintDelay(g, area.reduced(4)); break;
    }
}

// ----------------------------------------------------------------------------
// Parametric EQ Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintEq(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 15, 18));
    g.fillRect(bounds);

    const float w = static_cast<float>(bounds.getWidth());
    const float h = static_cast<float>(bounds.getHeight());
    const float midY = bounds.getY() + h * 0.5f;

    // 1. Log frequency grid lines (20 Hz, 100 Hz, 1 kHz, 10 kHz, 20 kHz)
    constexpr std::array<float, 5> gridFreqs = {20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f};
    g.setColour(juce::Colour::fromRGB(35, 38, 48));
    for (const float f : gridFreqs)
    {
        const float normX = (std::log10(f) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    }

    // 2. Decibel gain grid lines (-12 dB, -6 dB, 0 dB, +6 dB, +12 dB)
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = midY - (static_cast<float>(db) / 18.0f) * (h * 0.45f);
        g.setColour((db == 0) ? juce::Colour::fromRGB(70, 75, 95) : juce::Colour::fromRGB(32, 35, 44));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    // 3. FFT Real-Time Spectrum Analyzer Background Glow
    juce::Path spectrumPath;
    spectrumPath.startNewSubPath(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()));
    for (std::size_t i = 0; i < m_spectrumMagnitudes.size(); ++i)
    {
        const float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(m_spectrumMagnitudes.size() - 1)) * w;
        const float magY = bounds.getBottom() - m_spectrumMagnitudes[i] * h * 0.75f;
        spectrumPath.lineTo(x, magY);
    }
    spectrumPath.lineTo(static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getBottom()));
    spectrumPath.closeSubPath();

    g.setColour(juce::Colour::fromRGBA(30, 180, 220, 35));
    g.fillPath(spectrumPath);

    // 4. Combined EQ Filter Curve
    juce::Path curvePath;
    for (int px = 0; px < bounds.getWidth(); ++px)
    {
        const float normX = static_cast<float>(px) / w;
        const float freq = 20.0f * std::pow(1000.0f, normX); // 20 to 20000 Hz

        float totalGainDb = 0.0f;
        for (const auto& band : m_eqBands)
        {
            const float octDiff = std::log2(freq / band.freqHz);
            const float width = 1.0f / std::max(0.1f, band.q);
            const float bell = std::exp(-0.5f * (octDiff * octDiff) / (width * width));
            totalGainDb += band.gainDb * bell;
        }

        const float y = midY - (totalGainDb / 18.0f) * (h * 0.45f);
        if (px == 0)
            curvePath.startNewSubPath(bounds.getX() + static_cast<float>(px), y);
        else
            curvePath.lineTo(bounds.getX() + static_cast<float>(px), y);
    }

    g.setColour(juce::Colour::fromRGB(60, 200, 240));
    g.strokePath(curvePath, juce::PathStrokeType(2.5f));

    // 5. Band handles
    for (std::size_t i = 0; i < m_eqBands.size(); ++i)
    {
        const auto& band = m_eqBands[i];
        const float normX = (std::log10(band.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        const float y = midY - (band.gainDb / 18.0f) * (h * 0.45f);

        const bool isActive = (m_activeEqBand == static_cast<int>(i));
        g.setColour(isActive ? juce::Colours::white : juce::Colour::fromRGB(80, 220, 255));
        g.fillEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f);
        g.setColour(juce::Colour::fromRGB(20, 20, 25));
        g.drawEllipse(x - 6.0f, y - 6.0f, 12.0f, 12.0f, 1.5f);

        g.setColour(juce::Colour::fromRGB(180, 210, 230));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(juce::String(static_cast<int>(band.freqHz)) + "Hz",
                   static_cast<int>(x - 25.0f), static_cast<int>(y - 20.0f), 50, 14, juce::Justification::centred);
    }
}

// ----------------------------------------------------------------------------
// Compressor Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintCompressor(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 15, 18));
    g.fillRect(bounds);

    auto curveBounds = bounds.removeFromLeft(bounds.getWidth() - 100).reduced(8);
    auto sideControls = bounds.reduced(4);

    const float w = static_cast<float>(curveBounds.getWidth());
    const float h = static_cast<float>(curveBounds.getHeight());

    // Grid lines -60 dB to 0 dB
    g.setColour(juce::Colour::fromRGB(35, 38, 48));
    for (int db = -60; db <= 0; db += 12)
    {
        const float norm = static_cast<float>(db + 60) / 60.0f;
        const float x = curveBounds.getX() + norm * w;
        const float y = curveBounds.getBottom() - norm * h;
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(curveBounds.getY()), static_cast<float>(curveBounds.getBottom()));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(curveBounds.getX()), static_cast<float>(curveBounds.getRight()));
    }

    // 1:1 Reference Diagonal (dry linear line)
    g.setColour(juce::Colour::fromRGB(55, 60, 75));
    g.drawLine(static_cast<float>(curveBounds.getX()), static_cast<float>(curveBounds.getBottom()),
               static_cast<float>(curveBounds.getRight()), static_cast<float>(curveBounds.getY()), 1.0f);

    // Compression Transfer Characteristic Curve
    juce::Path transferPath;
    transferPath.startNewSubPath(static_cast<float>(curveBounds.getX()), static_cast<float>(curveBounds.getBottom()));

    for (int px = 0; px <= curveBounds.getWidth(); ++px)
    {
        const float normIn = static_cast<float>(px) / w;
        const float inDb = -60.0f + normIn * 60.0f;

        float outDb = inDb;
        if (inDb > m_compThresholdDb)
        {
            outDb = m_compThresholdDb + (inDb - m_compThresholdDb) / m_compRatio;
        }

        const float normOut = std::clamp((outDb + 60.0f) / 60.0f, 0.0f, 1.0f);
        const float y = curveBounds.getBottom() - normOut * h;
        transferPath.lineTo(curveBounds.getX() + static_cast<float>(px), y);
    }

    g.setColour(juce::Colour::fromRGB(240, 160, 40));
    g.strokePath(transferPath, juce::PathStrokeType(2.5f));

    // Live Gain Reduction meter
    auto grMeter = sideControls.removeFromRight(24);
    g.setColour(juce::Colour::fromRGB(25, 27, 34));
    g.fillRect(grMeter);
    g.setColour(juce::Colour::fromRGB(50, 54, 68));
    g.drawRect(grMeter, 1);

    const float grNorm = std::clamp(m_compGainReductionDb / 18.0f, 0.0f, 1.0f);
    const int grHeight = static_cast<int>(std::round(grNorm * static_cast<float>(grMeter.getHeight())));
    if (grHeight > 0)
    {
        auto grFill = grMeter.removeFromTop(grHeight);
        g.setColour(juce::Colour::fromRGB(230, 60, 50));
        g.fillRect(grFill);
    }

    // Sidechain & Parameter Labels
    g.setColour(juce::Colour::fromRGB(200, 205, 220));
    g.setFont(juce::FontOptions(11.0f));
    sideControls.removeFromTop(8);
    g.drawText("Thresh: " + juce::String(m_compThresholdDb, 1) + " dB", sideControls.removeFromTop(18), juce::Justification::left);
    g.drawText("Ratio: " + juce::String(m_compRatio, 1) + ":1", sideControls.removeFromTop(18), juce::Justification::left);
    g.drawText("Attack: " + juce::String(m_compAttackMs, 0) + " ms", sideControls.removeFromTop(18), juce::Justification::left);
    g.drawText("Release: " + juce::String(m_compReleaseMs, 0) + " ms", sideControls.removeFromTop(18), juce::Justification::left);

    sideControls.removeFromTop(10);
    g.setColour(m_compSidechainEnabled ? juce::Colour::fromRGB(80, 210, 120) : juce::Colour::fromRGB(120, 125, 140));
    g.drawText(m_compSidechainEnabled ? "[SC: ON]" : "[SC: OFF]", sideControls.removeFromTop(20), juce::Justification::centred);
}

// ----------------------------------------------------------------------------
// Synth Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintSynth(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 15, 18));
    g.fillRect(bounds);

    auto envArea = bounds.removeFromLeft(bounds.getWidth() - 140).reduced(8);
    auto controls = bounds.reduced(4);

    // Graphical ADSR Envelope Curve
    g.setColour(juce::Colour::fromRGB(25, 28, 36));
    g.fillRect(envArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 65));
    g.drawRect(envArea, 1);

    const float ew = static_cast<float>(envArea.getWidth());
    const float eh = static_cast<float>(envArea.getHeight());

    const float ax = envArea.getX() + m_synthAttack * ew * 0.25f;
    const float ay = envArea.getY();
    const float dx = ax + m_synthDecay * ew * 0.25f;
    const float dy = envArea.getBottom() - m_synthSustain * eh;
    const float sx = dx + ew * 0.25f;
    const float sy = dy;
    const float rx = sx + m_synthRelease * ew * 0.25f;
    const float ry = static_cast<float>(envArea.getBottom());

    juce::Path adsrPath;
    adsrPath.startNewSubPath(static_cast<float>(envArea.getX()), static_cast<float>(envArea.getBottom()));
    adsrPath.lineTo(ax, ay);
    adsrPath.lineTo(dx, dy);
    adsrPath.lineTo(sx, sy);
    adsrPath.lineTo(rx, ry);

    g.setColour(juce::Colour::fromRGBA(160, 80, 240, 40));
    juce::Path fillPath = adsrPath;
    fillPath.lineTo(static_cast<float>(envArea.getX()), static_cast<float>(envArea.getBottom()));
    fillPath.closeSubPath();
    g.fillPath(fillPath);

    g.setColour(juce::Colour::fromRGB(180, 100, 255));
    g.strokePath(adsrPath, juce::PathStrokeType(2.5f));

    // ADSR Drag points
    g.setColour(juce::Colours::white);
    g.fillEllipse(ax - 4.0f, ay - 4.0f, 8.0f, 8.0f);
    g.fillEllipse(dx - 4.0f, dy - 4.0f, 8.0f, 8.0f);
    g.fillEllipse(sx - 4.0f, sy - 4.0f, 8.0f, 8.0f);
    g.fillEllipse(rx - 4.0f, ry - 4.0f, 8.0f, 8.0f);

    // Preset & Osc info
    controls.removeFromTop(6);
    g.setColour(juce::Colour::fromRGB(220, 180, 255));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    constexpr std::array<const char*, 8> kPresets = {
        "Sub Bass", "Punchy Acid", "Supersaw Lead", "Warm Vintage Pad",
        "Ambient Brass", "Crystal Pluck", "80s Synthwave", "Sci-Fi Laser"
    };
    g.drawText("Preset: " + juce::String(kPresets[static_cast<std::size_t>(m_synthSelectedPreset % 8)]), controls.removeFromTop(20), juce::Justification::left);

    g.setColour(juce::Colour::fromRGB(190, 195, 210));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Osc 1: Saw", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Osc 2: Pulse (PWM)", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Filter: 24dB Lowpass", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Voices: 16 Poly", controls.removeFromTop(18), juce::Justification::left);
}

// ----------------------------------------------------------------------------
// Reverb Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintReverb(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 15, 18));
    g.fillRect(bounds);

    auto roomArea = bounds.removeFromLeft(bounds.getWidth() - 130).reduced(8);
    auto controls = bounds.reduced(4);

    // Room Geometry & Wave Reflection Animation
    g.setColour(juce::Colour::fromRGB(22, 24, 32));
    g.fillRect(roomArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 68));
    g.drawRect(roomArea, 1);

    const float cx = static_cast<float>(roomArea.getCentreX());
    const float cy = static_cast<float>(roomArea.getCentreY());

    for (int ring = 1; ring <= 4; ++ring)
    {
        const float radius = (static_cast<float>(ring) * 25.0f * m_revRoomSize) + std::sin(m_simPhase * 2.0f + static_cast<float>(ring)) * 4.0f;
        g.setColour(juce::Colour::fromRGBA(60, 150, 220, static_cast<int>(180.0f / static_cast<float>(ring))));
        g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.5f);
    }

    // Parameters
    controls.removeFromTop(8);
    g.setColour(juce::Colour::fromRGB(200, 220, 245));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Room Size: " + juce::String(static_cast<int>(m_revRoomSize * 100.0f)) + "%", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Decay (RT60): " + juce::String(m_revDecaySeconds, 1) + " s", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Damping: " + juce::String(static_cast<int>(m_revDamping * 100.0f)) + "%", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Width: " + juce::String(static_cast<int>(m_revStereoWidth * 100.0f)) + "%", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Mix: " + juce::String(static_cast<int>(m_revWetDry * 100.0f)) + "% Wet", controls.removeFromTop(18), juce::Justification::left);
}

// ----------------------------------------------------------------------------
// Delay Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintDelay(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 15, 18));
    g.fillRect(bounds);

    auto bounceArea = bounds.removeFromLeft(bounds.getWidth() - 130).reduced(8);
    auto controls = bounds.reduced(4);

    // Ping-Pong bouncing tap visualizer
    g.setColour(juce::Colour::fromRGB(22, 24, 32));
    g.fillRect(bounceArea);
    g.setColour(juce::Colour::fromRGB(45, 50, 68));
    g.drawRect(bounceArea, 1);

    const float bw = static_cast<float>(bounceArea.getWidth());
    const float bh = static_cast<float>(bounceArea.getHeight());
    const float pingX = bounceArea.getX() + (0.5f + 0.45f * std::sin(m_simPhase * 3.0f)) * bw;
    const float pingY = bounceArea.getY() + bh * 0.5f;

    g.setColour(juce::Colour::fromRGB(240, 180, 50));
    g.fillEllipse(pingX - 8.0f, pingY - 8.0f, 16.0f, 16.0f);
    g.setColour(juce::Colour::fromRGBA(240, 180, 50, 60));
    g.fillEllipse(pingX - 16.0f, pingY - 16.0f, 32.0f, 32.0f);

    // Parameters
    controls.removeFromTop(8);
    g.setColour(juce::Colour::fromRGB(235, 215, 180));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText(m_delayMode == 1 ? "Mode: Ping-Pong" : "Mode: Dual Mono", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Sync: 1/8 Note", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Feedback: " + juce::String(static_cast<int>(m_delayFeedback * 100.0f)) + "% (tanh)", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Low Cut: " + juce::String(static_cast<int>(m_delayLowCutHz)) + " Hz", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("High Cut: " + juce::String(static_cast<int>(m_delayHighCutHz)) + " Hz", controls.removeFromTop(18), juce::Justification::left);
    g.drawText("Mix: " + juce::String(static_cast<int>(m_delayWetDry * 100.0f)) + "% Wet", controls.removeFromTop(18), juce::Justification::left);
}

// ----------------------------------------------------------------------------
// Mouse Interaction
// ----------------------------------------------------------------------------
void DspEditorComponent::mouseDown(const juce::MouseEvent& e)
{
    if (m_kind == DspEditorKind::parametricEq)
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(32);
        const float w = static_cast<float>(area.getWidth());
        const float h = static_cast<float>(area.getHeight());
        const float midY = area.getY() + h * 0.5f;

        m_activeEqBand = -1;
        for (std::size_t i = 0; i < m_eqBands.size(); ++i)
        {
            const auto& band = m_eqBands[i];
            const float normX = (std::log10(band.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
            const float x = area.getX() + normX * w;
            const float y = midY - (band.gainDb / 18.0f) * (h * 0.45f);

            if (std::hypot(e.position.x - x, e.position.y - y) <= 15.0f)
            {
                m_activeEqBand = static_cast<int>(i);
                break;
            }
        }
    }
    else if (m_kind == DspEditorKind::synth)
    {
        // Click to cycle factory presets
        m_synthSelectedPreset = (m_synthSelectedPreset + 1) % 8;
        repaint();
    }
    else if (m_kind == DspEditorKind::compressor)
    {
        // Click on right controls toggles external sidechain
        if (e.position.x > getWidth() - 110)
        {
            m_compSidechainEnabled = ! m_compSidechainEnabled;
            repaint();
        }
    }
}

void DspEditorComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (m_kind == DspEditorKind::parametricEq && m_activeEqBand >= 0 && m_activeEqBand < static_cast<int>(m_eqBands.size()))
    {
        const auto bandIdx = static_cast<std::size_t>(m_activeEqBand);
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(32);
        const float w = static_cast<float>(area.getWidth());
        const float h = static_cast<float>(area.getHeight());
        const float midY = area.getY() + h * 0.5f;

        const float normX = std::clamp((e.position.x - area.getX()) / w, 0.0f, 1.0f);
        m_eqBands[bandIdx].freqHz = 20.0f * std::pow(1000.0f, normX);

        const float normY = (midY - e.position.y) / (h * 0.45f);
        m_eqBands[bandIdx].gainDb = std::clamp(normY * 18.0f, -18.0f, 18.0f);
        repaint();
    }
}

void DspEditorComponent::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (m_kind == DspEditorKind::parametricEq && m_activeEqBand >= 0 && m_activeEqBand < static_cast<int>(m_eqBands.size()))
    {
        const auto bandIdx = static_cast<std::size_t>(m_activeEqBand);
        m_eqBands[bandIdx].q = std::clamp(m_eqBands[bandIdx].q + wheel.deltaY * 0.5f, 0.1f, 10.0f);
        repaint();
    }
}

// ============================================================================
// DspEditorWindow
// ============================================================================

DspEditorWindow::DspEditorWindow(const juce::String& name, DspEditorKind kind, std::function<void()> onClose)
    : DocumentWindow(name, juce::Colour::fromRGB(24, 26, 32), DocumentWindow::closeButton)
    , m_onClose(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setContentOwned(new DspEditorComponent(kind, name.toStdString()), true);
    setResizable(true, false);
    centreWithSize(520, 320);
    setVisible(true);
}

void DspEditorWindow::closeButtonPressed()
{
    if (m_onClose)
        m_onClose();
    delete this;
}

void DspEditorWindow::open(DspEditorKind kind, const std::string& trackName, std::function<void()> onClose)
{
    juce::String title;
    switch (kind)
    {
    case DspEditorKind::parametricEq: title = "Parametric EQ — " + juce::String(trackName); break;
    case DspEditorKind::compressor: title = "Dynamics Compressor — " + juce::String(trackName); break;
    case DspEditorKind::synth: title = "TNHC Poly Synth — " + juce::String(trackName); break;
    case DspEditorKind::reverb: title = "Algorithmic Reverb — " + juce::String(trackName); break;
    case DspEditorKind::delay: title = "Stereo Delay — " + juce::String(trackName); break;
    }

    new DspEditorWindow(title, kind, std::move(onClose));
}

} // namespace dawg::ui
