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
        case DspEditorKind::resonanceSuppressor: m_title = "ResoTamer Dynamic Resonance Suppressor"; break;
        case DspEditorKind::multibandImager: m_title = "StereoScope Mastering Multiband Imager"; break;
        case DspEditorKind::linearPhaseEq: m_title = "MasterPhase Dynamic Linear-Phase EQ"; break;
        case DspEditorKind::buchlaComplexSynth: m_title = "Complex 259 West-Coast Synthesizer"; break;
        }
    }

    switch (m_kind)
    {
    case DspEditorKind::parametricEq: m_pluginId = "tnhc.parametric_eq"; break;
    case DspEditorKind::compressor: m_pluginId = "tnhc.studio_compressor"; break;
    case DspEditorKind::synth: m_pluginId = "tnhc.synth"; break;
    case DspEditorKind::reverb: m_pluginId = "tnhc.plate_reverb"; break;
    case DspEditorKind::delay: m_pluginId = "tnhc.stereo_delay"; break;
    case DspEditorKind::resonanceSuppressor: m_pluginId = "dawg.effect.dynamic_resonance"; break;
    case DspEditorKind::multibandImager: m_pluginId = "dawg.effect.multiband_imager"; break;
    case DspEditorKind::linearPhaseEq: m_pluginId = "dawg.effect.linear_phase_eq"; break;
    case DspEditorKind::buchlaComplexSynth: m_pluginId = "dawg.synth.buchla_complex"; break;
    }

    m_titleLabel.setText(m_title, juce::dontSendNotification);
    m_titleLabel.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    m_titleLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 235, 245));
    addAndMakeVisible(m_titleLabel);

    // Preset Manager & Ribbon
    m_presetManager = std::make_unique<plugins::PluginPresetManager>();

    m_searchPill.setTextToShowWhenEmpty("Filter...", juce::Colour::fromRGB(110, 120, 140));
    m_searchPill.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGB(24, 26, 33));
    m_searchPill.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_searchPill.onTextChange = [this] { populatePresets(m_searchPill.getText().toStdString()); };
    addAndMakeVisible(m_searchPill);

    m_presetCombo.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGB(24, 26, 33));
    m_presetCombo.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGB(45, 52, 65));
    m_presetCombo.onChange = [this] { selectPreset(m_presetCombo.getSelectedId()); };
    addAndMakeVisible(m_presetCombo);

    for (int i = 0; i < 5; ++i)
    {
        m_starButtons[static_cast<std::size_t>(i)].setButtonText("*");
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 205, 50));
        m_starButtons[static_cast<std::size_t>(i)].onClick = [this, star = i + 1] { updateStars(star); };
        addAndMakeVisible(m_starButtons[static_cast<std::size_t>(i)]);
    }

    m_abToggleBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(32, 38, 48));
    m_abToggleBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(140, 200, 255));
    m_abToggleBtn.onClick = [this] {
        m_isSlotA = ! m_isSlotA;
        m_abToggleBtn.setButtonText(m_isSlotA ? "[ A ]" : "[ B ]");
    };
    addAndMakeVisible(m_abToggleBtn);

    m_copyAbBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(28, 32, 42));
    m_copyAbBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(180, 190, 210));
    m_copyAbBtn.onClick = [this] {
        if (m_presetManager)
        {
            auto& ab = m_presetManager->getOrAllocABState(m_pluginId);
            if (m_isSlotA) ab.copyAToB(); else ab.copyBToA();
        }
    };
    addAndMakeVisible(m_copyAbBtn);

    m_savePresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(35, 75, 45));
    m_savePresetBtn.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(200, 245, 210));
    m_savePresetBtn.onClick = [this] {
        if (! m_presetManager) return;
        plugins::PluginPreset p;
        p.presetId = "user_" + std::to_string(std::rand() % 10000);
        p.pluginId = m_pluginId;
        p.name = "My " + m_title.substr(0, m_title.find(' ')) + " Preset";
        p.author = "User";
        p.category = "User Presets";
        p.tags = {"user", "custom"};
        p.rating = m_currentRating;
        m_presetManager->saveUserPreset(p);
        populatePresets(m_searchPill.getText().toStdString());
    };
    addAndMakeVisible(m_savePresetBtn);

    m_bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(180, 185, 195));
    addAndMakeVisible(m_bypassButton);

    populatePresets();
    updateStars(5);

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
    else if (m_kind == DspEditorKind::resonanceSuppressor)
    {
        // 128 FFT bins simulation with sharp resonances
        for (std::size_t i = 0; i < m_resoFftBins.size(); ++i)
        {
            const float norm = static_cast<float>(i) / static_cast<float>(m_resoFftBins.size());
            const float base = 0.6f * std::exp(-norm * 2.2f) + 0.05f;
            // Offending resonances at bin 25 (~500Hz), bin 60 (~2.8kHz), bin 95 (~7kHz)
            const float res1 = 0.45f * std::exp(-std::pow((norm - 0.2f) * 35.0f, 2.0f)) * (0.8f + 0.2f * std::sin(m_simPhase * 4.0f));
            const float res2 = 0.55f * std::exp(-std::pow((norm - 0.48f) * 40.0f, 2.0f)) * (0.85f + 0.15f * std::cos(m_simPhase * 5.0f));
            const float res3 = 0.35f * std::exp(-std::pow((norm - 0.75f) * 30.0f, 2.0f)) * (0.9f + 0.1f * std::sin(m_simPhase * 3.0f));
            const float noise = (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 0.04f;

            m_resoFftBins[i] = std::clamp(base + res1 + res2 + res3 + noise, 0.02f, 0.98f);
            // ERB psychoacoustic masking threshold curve (smoother, dynamic tilt)
            m_resoErbMask[i] = std::clamp(base * 1.15f + 0.08f + 0.03f * std::sin(m_simPhase + norm * 3.0f), 0.05f, 0.92f);
        }
    }
    else if (m_kind == DspEditorKind::multibandImager)
    {
        m_imagerCorrelation = std::clamp(0.75f + 0.22f * std::sin(m_simPhase * 0.8f), -1.0f, 1.0f);
        // Lissajous stereo orbital points
        for (std::size_t i = 0; i < m_lissajousPoints.size(); ++i)
        {
            const float theta = m_simPhase * 2.5f + static_cast<float>(i) * (2.0f * static_cast<float>(std::numbers::pi) / static_cast<float>(m_lissajousPoints.size()));
            const float spread = 0.65f + 0.35f * std::sin(m_simPhase * 1.2f + static_cast<float>(i));
            const float l = std::sin(theta) * 0.7f;
            const float r = std::cos(theta * 1.02f + 0.4f) * spread * 0.7f;
            // Rotate 45 deg for M/S goniometer projection
            const float x = (r - l) * 0.7071f;
            const float y = (r + l) * 0.7071f;
            m_lissajousPoints[i] = juce::Point<float>(x, y);
        }
    }
    else if (m_kind == DspEditorKind::linearPhaseEq)
    {
        // Dynamic gain reduction modulation on bands 2 and 4
        m_masterEqBands[1].dynReductionDb = -1.5f * std::max(0.0f, std::sin(m_simPhase * 3.5f));
        m_masterEqBands[3].dynReductionDb = -2.8f * std::max(0.0f, std::sin(m_simPhase * 2.8f + 0.5f));
    }
    else if (m_kind == DspEditorKind::buchlaComplexSynth)
    {
        // Vactrol decay envelope
        m_vactrolDecayEnvelope = std::exp(-std::fmod(m_simPhase * 0.7f, 1.0f) * 3.2f);
        // Wavefolder oscilloscope trace: TzFM + diode folding
        for (std::size_t i = 0; i < m_oscilloscopeTrace.size(); ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(m_oscilloscopeTrace.size() - 1);
            const float carrierPhase = t * 2.0f * static_cast<float>(std::numbers::pi);
            const float modulator = std::sin(carrierPhase * 2.0f + m_simPhase);
            const float raw = std::sin(carrierPhase + 1.8f * modulator) * m_wavefoldDrive + m_wavefoldSymmetry;
            // Cascaded diode fold function: sin(pi * x)
            const float folded = std::sin(raw * static_cast<float>(std::numbers::pi) * 0.5f);
            m_oscilloscopeTrace[i] = juce::Point<float>(t, folded);
        }
    }

    repaint();
}

void DspEditorComponent::populatePresets(const std::string& query)
{
    m_presetCombo.clear(juce::dontSendNotification);
    if (! m_presetManager)
        return;

    const auto presets = m_presetManager->searchPresets(m_pluginId, query);
    int id = 1;
    for (const auto& p : presets)
    {
        juce::String label = (p.isFactory ? "[Factory] " : "[User] ") + juce::String(p.name);
        m_presetCombo.addItem(label, id++);
    }
    if (m_presetCombo.getNumItems() > 0 && m_presetCombo.getSelectedId() == 0)
    {
        m_presetCombo.setSelectedId(1, juce::dontSendNotification);
    }
}

void DspEditorComponent::selectPreset(int index)
{
    if (index <= 0 || ! m_presetManager)
        return;

    const auto presets = m_presetManager->searchPresets(m_pluginId, m_searchPill.getText().toStdString());
    if (static_cast<std::size_t>(index - 1) < presets.size())
    {
        const auto& p = presets[static_cast<std::size_t>(index - 1)];
        updateStars(p.rating > 0 ? p.rating : 5);
        if (! p.parameters.empty())
        {
            if (m_kind == DspEditorKind::buchlaComplexSynth && p.parameters.size() >= 4)
            {
                m_wavefoldDrive = p.parameters[3] * 3.0f;
                if (p.parameters.size() >= 5)
                    m_wavefoldSymmetry = p.parameters[4];
            }
            else if (m_kind == DspEditorKind::resonanceSuppressor && p.parameters.size() >= 2)
            {
                m_resoDepth = p.parameters[0];
                m_resoSharpness = p.parameters[1];
            }
            else if (m_kind == DspEditorKind::multibandImager && p.parameters.size() >= 4)
            {
                for (std::size_t b = 0; b < 4 && b < p.parameters.size(); ++b)
                    m_imagerWidths[b] = p.parameters[b];
            }
            else if (m_kind == DspEditorKind::linearPhaseEq && p.parameters.size() >= 6)
            {
                for (std::size_t b = 0; b < 6 && b < p.parameters.size(); ++b)
                    m_masterEqBands[b].gainDb = p.parameters[b];
            }
        }
        repaint();
    }
}

void DspEditorComponent::updateStars(int rating)
{
    m_currentRating = std::clamp(rating, 1, 5);
    for (int i = 0; i < 5; ++i)
    {
        if (i < m_currentRating)
            m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(255, 205, 50));
        else
            m_starButtons[static_cast<std::size_t>(i)].setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(75, 80, 95));
    }
}

void DspEditorComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto ribbon = area.removeFromTop(26);

    m_titleLabel.setBounds(ribbon.removeFromLeft(125));
    ribbon.removeFromLeft(4);

    m_searchPill.setBounds(ribbon.removeFromLeft(75).reduced(0, 1));
    ribbon.removeFromLeft(4);

    m_presetCombo.setBounds(ribbon.removeFromLeft(145).reduced(0, 1));
    ribbon.removeFromLeft(4);

    // Star rating buttons
    for (std::size_t i = 0; i < m_starButtons.size(); ++i)
    {
        m_starButtons[i].setBounds(ribbon.removeFromLeft(16).reduced(0, 3));
    }
    ribbon.removeFromLeft(6);

    m_abToggleBtn.setBounds(ribbon.removeFromLeft(42).reduced(0, 1));
    ribbon.removeFromLeft(3);

    m_copyAbBtn.setBounds(ribbon.removeFromLeft(40).reduced(0, 1));
    ribbon.removeFromLeft(3);

    m_savePresetBtn.setBounds(ribbon.removeFromLeft(42).reduced(0, 1));

    m_bypassButton.setBounds(ribbon.removeFromRight(68));
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
    case DspEditorKind::resonanceSuppressor: paintResonanceSuppressor(g, area.reduced(4)); break;
    case DspEditorKind::multibandImager: paintMultibandImager(g, area.reduced(4)); break;
    case DspEditorKind::linearPhaseEq: paintLinearPhaseEq(g, area.reduced(4)); break;
    case DspEditorKind::buchlaComplexSynth: paintBuchlaComplexSynth(g, area.reduced(4)); break;
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
// ResoTamer Dynamic Resonance Suppressor Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintResonanceSuppressor(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(15, 17, 21));
    g.fillRect(bounds);

    const float w = static_cast<float>(bounds.getWidth());
    const float h = static_cast<float>(bounds.getHeight());
    const float scopeH = h - 30.0f;

    // Grid lines for frequency (100Hz, 1kHz, 5kHz, 10kHz)
    constexpr std::array<float, 4> freqs = {100.0f, 1000.0f, 5000.0f, 10000.0f};
    g.setColour(juce::Colour::fromRGB(32, 36, 45));
    for (float f : freqs)
    {
        const float normX = (std::log10(f) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + scopeH));
    }

    // Decibel grid
    for (int db = -36; db <= 0; db += 12)
    {
        const float y = bounds.getY() + scopeH * (static_cast<float>(-db) / 48.0f);
        g.setColour(juce::Colour::fromRGB(30, 34, 42));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    // FFT Spectrum Fill
    juce::Path spectrumPath;
    spectrumPath.startNewSubPath(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY() + scopeH));
    for (std::size_t i = 0; i < m_resoFftBins.size(); ++i)
    {
        const float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(m_resoFftBins.size() - 1)) * w;
        const float y = bounds.getY() + scopeH * (1.0f - m_resoFftBins[i] * 0.85f);
        spectrumPath.lineTo(x, y);
    }
    spectrumPath.lineTo(static_cast<float>(bounds.getRight()), static_cast<float>(bounds.getY() + scopeH));
    spectrumPath.closeSubPath();

    juce::ColourGradient grad(juce::Colour::fromRGBA(35, 210, 220, 60), static_cast<float>(bounds.getX()), bounds.getY() + scopeH * 0.3f,
                              juce::Colour::fromRGBA(15, 60, 100, 15), static_cast<float>(bounds.getX()), bounds.getY() + scopeH, false);
    g.setGradientFill(grad);
    g.fillPath(spectrumPath);

    // Spectrum Outline
    g.setColour(juce::Colour::fromRGBA(40, 220, 235, 180));
    juce::Path strokePath;
    for (std::size_t i = 0; i < m_resoFftBins.size(); ++i)
    {
        const float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(m_resoFftBins.size() - 1)) * w;
        const float y = bounds.getY() + scopeH * (1.0f - m_resoFftBins[i] * 0.85f);
        if (i == 0) strokePath.startNewSubPath(x, y); else strokePath.lineTo(x, y);
    }
    g.strokePath(strokePath, juce::PathStrokeType(1.2f));

    // Psychoacoustic ERB Masking Curve
    juce::Path erbPath;
    for (std::size_t i = 0; i < m_resoErbMask.size(); ++i)
    {
        const float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(m_resoErbMask.size() - 1)) * w;
        const float y = bounds.getY() + scopeH * (1.0f - m_resoErbMask[i] * 0.85f);
        if (i == 0) erbPath.startNewSubPath(x, y); else erbPath.lineTo(x, y);
    }
    g.setColour(juce::Colour::fromRGB(245, 190, 50));
    g.strokePath(erbPath, juce::PathStrokeType(1.5f));

    // Resonance Suppression Cut Markers
    for (std::size_t i = 2; i < m_resoFftBins.size() - 2; ++i)
    {
        if (m_resoFftBins[i] > m_resoErbMask[i] + 0.08f)
        {
            const float x = bounds.getX() + (static_cast<float>(i) / static_cast<float>(m_resoFftBins.size() - 1)) * w;
            const float peakY = bounds.getY() + scopeH * (1.0f - m_resoFftBins[i] * 0.85f);
            const float targetY = bounds.getY() + scopeH * (1.0f - m_resoErbMask[i] * 0.85f);

            // Downward attenuation arrow / notch line
            g.setColour(juce::Colour::fromRGBA(255, 70, 70, 160));
            g.drawLine(x, peakY, x, targetY, 2.0f);

            // Halo node at peak
            g.setColour(juce::Colour::fromRGB(255, 80, 80));
            g.fillEllipse(x - 3.5f, peakY - 3.5f, 7.0f, 7.0f);
        }
    }

    // Interactive Draggable Resonance Cut Nodes
    for (std::size_t i = 0; i < m_resoNodes.size(); ++i)
    {
        const auto& node = m_resoNodes[i];
        if (! node.active) continue;

        const float normX = (std::log10(node.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        const float normY = (-node.depthDb) / 24.0f; // 0 to 24 dB cut
        const float y = bounds.getY() + scopeH * std::clamp(normY, 0.1f, 0.9f);

        const bool isSelected = (m_activeResoNode == static_cast<int>(i));

        // Halo outer glow
        g.setColour(isSelected ? juce::Colour::fromRGBA(255, 90, 90, 80) : juce::Colour::fromRGBA(255, 140, 40, 50));
        g.fillEllipse(x - 10.0f, y - 10.0f, 20.0f, 20.0f);

        // Core handle
        g.setColour(isSelected ? juce::Colour::fromRGB(255, 100, 100) : juce::Colour::fromRGB(255, 160, 50));
        g.fillEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colours::white);
        g.drawEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f, 1.2f);

        // Parameter tag
        const juce::String tag = (node.freqHz >= 1000.0f ? juce::String(node.freqHz / 1000.0f, 1) + "k" : juce::String(static_cast<int>(node.freqHz)))
            + " | " + juce::String(node.depthDb, 1) + "dB";
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(tag, static_cast<int>(x) - 30, static_cast<int>(y) - 18, 60, 12, juce::Justification::centred);
    }

    // Bottom Status Ribbon
    auto ribbon = bounds.removeFromBottom(26);
    g.setColour(juce::Colour::fromRGB(22, 25, 32));
    g.fillRect(ribbon);
    g.setColour(juce::Colour::fromRGB(210, 220, 235));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("ResoTamer: 1024-Band ERB Auditory Masking  |  Dynamic Attenuation: Active  |  Zero Comb Ringing",
               ribbon.reduced(8, 2), juce::Justification::centredLeft);
}

// ----------------------------------------------------------------------------
// StereoScope Mastering Multiband Imager Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintMultibandImager(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(16, 18, 23));
    g.fillRect(bounds);

    auto workArea = bounds.reduced(6);
    auto bottomArea = workArea.removeFromBottom(30);

    // Left pane: Lissajous radar & goniometer
    auto gonioBounds = workArea.removeFromLeft(180).reduced(4);
    g.setColour(juce::Colour::fromRGB(22, 25, 32));
    g.fillRoundedRectangle(gonioBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(38, 42, 54));
    g.drawRoundedRectangle(gonioBounds.toFloat(), 4.0f, 1.0f);

    const float cx = static_cast<float>(gonioBounds.getCentreX());
    const float cy = static_cast<float>(gonioBounds.getCentreY());
    const float radius = static_cast<float>(std::min(gonioBounds.getWidth(), gonioBounds.getHeight())) * 0.42f;

    // Target circles
    g.setColour(juce::Colour::fromRGBA(50, 70, 90, 80));
    g.drawEllipse(cx - radius * 0.5f, cy - radius * 0.5f, radius, radius, 1.0f);
    g.drawEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    // Crosshairs
    g.drawLine(cx - radius, cy, cx + radius, cy, 1.0f);
    g.drawLine(cx, cy - radius, cx, cy + radius, 1.0f);
    g.drawLine(cx - radius * 0.707f, cy - radius * 0.707f, cx + radius * 0.707f, cy + radius * 0.707f, 1.0f);
    g.drawLine(cx - radius * 0.707f, cy + radius * 0.707f, cx + radius * 0.707f, cy - radius * 0.707f, 1.0f);

    // Lissajous scatter points
    g.setColour(juce::Colour::fromRGBA(60, 240, 180, 200));
    for (const auto& pt : m_lissajousPoints)
    {
        const float px = cx + pt.x * radius;
        const float py = cy - pt.y * radius;
        g.fillEllipse(px - 2.0f, py - 2.0f, 4.0f, 4.0f);
    }

    g.setColour(juce::Colour::fromRGB(150, 160, 180));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("GONIOMETER", gonioBounds.removeFromTop(18), juce::Justification::centred);

    // Right pane: 4-band stereo width controls
    auto bandsArea = workArea.reduced(6, 2);
    const int bandW = bandsArea.getWidth() / 4;
    const std::array<const char*, 4> bandNames = {"SUB (<120Hz)", "LOW-MID", "HIGH-MID", "AIR (>6kHz)"};
    const std::array<juce::Colour, 4> bandColours = {
        juce::Colour::fromRGB(80, 160, 240),
        juce::Colour::fromRGB(60, 210, 160),
        juce::Colour::fromRGB(240, 180, 60),
        juce::Colour::fromRGB(210, 110, 240)
    };

    for (int b = 0; b < 4; ++b)
    {
        auto bandRect = bandsArea.removeFromLeft(bandW).reduced(3);
        g.setColour(juce::Colour::fromRGB(24, 27, 35));
        g.fillRoundedRectangle(bandRect.toFloat(), 3.0f);

        g.setColour(juce::Colour::fromRGB(180, 190, 210));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText(bandNames[static_cast<std::size_t>(b)], bandRect.removeFromTop(16), juce::Justification::centred);

        // Solo and Mute button pills
        auto btnRow = bandRect.removeFromTop(16).reduced(6, 1);
        const auto soloRect = btnRow.removeFromLeft(btnRow.getWidth() / 2).reduced(1);
        const auto muteRect = btnRow.reduced(1);

        const bool isSolo = m_imagerSolos[static_cast<std::size_t>(b)];
        const bool isMute = m_imagerMutes[static_cast<std::size_t>(b)];

        g.setColour(isSolo ? juce::Colour::fromRGB(240, 200, 40) : juce::Colour::fromRGB(40, 45, 55));
        g.fillRoundedRectangle(soloRect.toFloat(), 2.0f);
        g.setColour(isSolo ? juce::Colours::black : juce::Colour::fromRGB(180, 190, 205));
        g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
        g.drawText("S", soloRect, juce::Justification::centred);

        g.setColour(isMute ? juce::Colour::fromRGB(220, 60, 60) : juce::Colour::fromRGB(40, 45, 55));
        g.fillRoundedRectangle(muteRect.toFloat(), 2.0f);
        g.setColour(isMute ? juce::Colours::white : juce::Colour::fromRGB(180, 190, 205));
        g.drawText("M", muteRect, juce::Justification::centred);

        // Width meter bar
        const float widthPct = m_imagerWidths[static_cast<std::size_t>(b)];
        auto meterRect = bandRect.reduced(10, 6);
        g.setColour(juce::Colour::fromRGB(35, 40, 50));
        g.fillRoundedRectangle(meterRect.toFloat(), 2.0f);

        const float filledH = meterRect.getHeight() * std::clamp(widthPct / 2.0f, 0.0f, 1.0f);
        auto fillRect = meterRect.removeFromBottom(static_cast<int>(filledH));
        g.setColour(bandColours[static_cast<std::size_t>(b)]);
        g.fillRoundedRectangle(fillRect.toFloat(), 2.0f);

        g.setColour(juce::Colour::fromRGB(230, 235, 245));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(juce::String(static_cast<int>(widthPct * 100.0f)) + "%",
                   bandRect.removeFromBottom(16), juce::Justification::centred);

        // Draggable Linkwitz-Riley crossover divider handle
        if (b < 3)
        {
            const float divX = static_cast<float>(bandRect.getRight() + 4);
            g.setColour(juce::Colour::fromRGB(70, 85, 110));
            g.drawVerticalLine(static_cast<int>(divX), static_cast<float>(bandsArea.getY() + 4), static_cast<float>(bandsArea.getBottom() - 14));

            const float xoverFreq = m_imagerCrossovers[static_cast<std::size_t>(b)];
            const juce::String freqStr = (xoverFreq >= 1000.0f)
                ? juce::String(xoverFreq / 1000.0f, 1) + "k"
                : juce::String(static_cast<int>(xoverFreq));

            const juce::Rectangle<int> tagRect {static_cast<int>(divX) - 18, bandsArea.getBottom() - 13, 36, 13};
            g.setColour(juce::Colour::fromRGB(32, 38, 50));
            g.fillRoundedRectangle(tagRect.toFloat(), 2.0f);
            g.setColour(juce::Colour::fromRGB(170, 190, 220));
            g.setFont(juce::FontOptions(8.0f));
            g.drawText(freqStr, tagRect, juce::Justification::centred);
        }
    }

    // Bottom: Phase Correlation Bar
    bottomArea.reduce(6, 4);
    g.setColour(juce::Colour::fromRGB(22, 25, 32));
    g.fillRoundedRectangle(bottomArea.toFloat(), 3.0f);

    const float corrNorm = (m_imagerCorrelation + 1.0f) * 0.5f; // [0.0, 1.0]
    const float markerX = bottomArea.getX() + corrNorm * static_cast<float>(bottomArea.getWidth());

    // Gradient bar from Red (-1) through Amber (0) to Green (+1)
    g.setColour(juce::Colour::fromRGB(220, 60, 60));
    g.drawText("-1.0", bottomArea.removeFromLeft(30), juce::Justification::centredLeft);
    g.setColour(juce::Colour::fromRGB(60, 220, 120));
    g.drawText("+1.0", bottomArea.removeFromRight(30), juce::Justification::centredRight);

    g.setColour(juce::Colour::fromRGB(220, 230, 245));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("Phase Correlation: " + juce::String(m_imagerCorrelation, 2),
               bottomArea, juce::Justification::centred);

    // Indicator needle
    g.setColour(juce::Colour::fromRGB(255, 255, 255));
    g.drawLine(markerX, static_cast<float>(bottomArea.getY()),
               markerX, static_cast<float>(bottomArea.getBottom()), 2.0f);
}

// ----------------------------------------------------------------------------
// MasterPhase Dynamic Linear-Phase EQ Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintLinearPhaseEq(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(14, 18, 26));
    g.fillRect(bounds);

    const float w = static_cast<float>(bounds.getWidth());
    const float h = static_cast<float>(bounds.getHeight());
    const float midY = bounds.getY() + (h - 26.0f) * 0.5f;

    // Log frequency grid lines
    constexpr std::array<float, 5> freqs = {20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f};
    g.setColour(juce::Colour::fromRGB(28, 35, 50));
    for (float f : freqs)
    {
        const float normX = (std::log10(f) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(bounds.getY()), static_cast<float>(bounds.getY() + h - 26.0f));
    }

    // Gain lines
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = midY - (static_cast<float>(db) / 18.0f) * ((h - 26.0f) * 0.45f);
        g.setColour((db == 0) ? juce::Colour::fromRGB(65, 80, 110) : juce::Colour::fromRGB(25, 32, 45));
        g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    // Linear-Phase Combined Curve
    juce::Path staticPath;
    juce::Path dynPath;

    for (int px = 0; px < bounds.getWidth(); ++px)
    {
        const float normX = static_cast<float>(px) / w;
        const float freq = 20.0f * std::pow(1000.0f, normX);

        float staticGainDb = 0.0f;
        float dynGainDb = 0.0f;

        for (const auto& band : m_masterEqBands)
        {
            const float octDiff = std::log2(freq / band.freqHz);
            const float width = 1.0f / std::max(0.1f, band.q);
            const float bell = std::exp(-0.5f * (octDiff * octDiff) / (width * width));
            staticGainDb += band.gainDb * bell;
            dynGainDb += (band.gainDb + band.dynReductionDb) * bell;
        }

        const float yStatic = midY - (staticGainDb / 18.0f) * ((h - 26.0f) * 0.45f);
        const float yDyn = midY - (dynGainDb / 18.0f) * ((h - 26.0f) * 0.45f);

        if (px == 0)
        {
            staticPath.startNewSubPath(bounds.getX() + static_cast<float>(px), yStatic);
            dynPath.startNewSubPath(bounds.getX() + static_cast<float>(px), yDyn);
        }
        else
        {
            staticPath.lineTo(bounds.getX() + static_cast<float>(px), yStatic);
            dynPath.lineTo(bounds.getX() + static_cast<float>(px), yDyn);
        }
    }

    // Dynamic Reduction Ribbon Fill
    juce::Path ribbonPath = staticPath;
    ribbonPath.lineTo(static_cast<float>(bounds.getRight()), midY);
    ribbonPath.lineTo(static_cast<float>(bounds.getX()), midY);
    ribbonPath.closeSubPath();
    g.setColour(juce::Colour::fromRGBA(50, 130, 240, 25));
    g.fillPath(ribbonPath);

    // Static Curve Stroke
    g.setColour(juce::Colour::fromRGB(80, 160, 255));
    g.strokePath(staticPath, juce::PathStrokeType(2.0f));

    // Dynamic Ghost Curve
    g.setColour(juce::Colour::fromRGBA(255, 160, 50, 180));
    g.strokePath(dynPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Band node handles
    for (std::size_t i = 0; i < m_masterEqBands.size(); ++i)
    {
        const auto& band = m_masterEqBands[i];
        const float normX = (std::log10(band.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
        const float x = bounds.getX() + normX * w;
        const float y = midY - (band.gainDb / 18.0f) * ((h - 26.0f) * 0.45f);

        // Dynamic reduction ring
        if (band.dynReductionDb < -0.1f)
        {
            const float dynY = midY - ((band.gainDb + band.dynReductionDb) / 18.0f) * ((h - 26.0f) * 0.45f);
            g.setColour(juce::Colour::fromRGBA(255, 140, 40, 120));
            g.drawLine(x, y, x, dynY, 2.0f);
            g.fillEllipse(x - 3.0f, dynY - 3.0f, 6.0f, 6.0f);
        }

        g.setColour(juce::Colour::fromRGB(120, 190, 255));
        g.fillEllipse(x - 5.0f, y - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colour::fromRGB(255, 255, 255));
        g.fillEllipse(x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }

    // Bottom Info Bar
    auto bottom = bounds.removeFromBottom(24);
    g.setColour(juce::Colour::fromRGB(20, 24, 34));
    g.fillRect(bottom);
    g.setColour(juce::Colour::fromRGB(190, 210, 240));
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText("MasterPhase Linear EQ: 4096-Tap FIR  |  0 deg Phase Distortion  |  Dynamic Compression: Active",
               bottom.reduced(8, 2), juce::Justification::centredLeft);
}

// ----------------------------------------------------------------------------
// Complex 259 West-Coast Synthesizer Visualizer
// ----------------------------------------------------------------------------
void DspEditorComponent::paintBuchlaComplexSynth(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(18, 19, 22));
    g.fillRect(bounds);

    auto workArea = bounds.reduced(6);

    // Left pane: CRT Oscilloscope Screen
    auto scopeArea = workArea.removeFromLeft(240).reduced(4);
    g.setColour(juce::Colour::fromRGB(10, 12, 14));
    g.fillRoundedRectangle(scopeArea.toFloat(), 6.0f);
    g.setColour(juce::Colour::fromRGB(45, 50, 40));
    g.drawRoundedRectangle(scopeArea.toFloat(), 6.0f, 2.0f);

    const float sw = static_cast<float>(scopeArea.getWidth());
    const float sh = static_cast<float>(scopeArea.getHeight());
    const float midScopeY = scopeArea.getY() + sh * 0.5f;

    // Amber CRT Grid
    g.setColour(juce::Colour::fromRGBA(220, 160, 30, 35));
    for (int gx = 1; gx < 6; ++gx)
        g.drawVerticalLine(scopeArea.getX() + gx * (scopeArea.getWidth() / 6),
                           static_cast<float>(scopeArea.getY()), static_cast<float>(scopeArea.getBottom()));
    for (int gy = 1; gy < 4; ++gy)
        g.drawHorizontalLine(scopeArea.getY() + gy * (scopeArea.getHeight() / 4),
                             static_cast<float>(scopeArea.getX()), static_cast<float>(scopeArea.getRight()));

    // Wavefolder Oscilloscope Trace
    juce::Path tracePath;
    for (std::size_t i = 0; i < m_oscilloscopeTrace.size(); ++i)
    {
        const float x = scopeArea.getX() + m_oscilloscopeTrace[i].x * sw;
        const float y = midScopeY - m_oscilloscopeTrace[i].y * (sh * 0.38f);
        if (i == 0) tracePath.startNewSubPath(x, y); else tracePath.lineTo(x, y);
    }
    // Phosphor glow
    g.setColour(juce::Colour::fromRGBA(255, 175, 40, 60));
    g.strokePath(tracePath, juce::PathStrokeType(4.0f));
    g.setColour(juce::Colour::fromRGB(255, 195, 60));
    g.strokePath(tracePath, juce::PathStrokeType(1.6f));

    // Vactrol LPG Opto-Decay Pulse Indicator
    const float pulseRadius = 6.0f + m_vactrolDecayEnvelope * 10.0f;
    const float pulseX = scopeArea.getRight() - 20.0f;
    const float pulseY = scopeArea.getY() + 20.0f;
    g.setColour(juce::Colour::fromRGBA(255, 80, 40, static_cast<juce::uint8>(m_vactrolDecayEnvelope * 220.0f)));
    g.fillEllipse(pulseX - pulseRadius, pulseY - pulseRadius, pulseRadius * 2.0f, pulseRadius * 2.0f);
    g.setColour(juce::Colour::fromRGB(255, 230, 200));
    g.fillEllipse(pulseX - 3.0f, pulseY - 3.0f, 6.0f, 6.0f);

    // Right pane: Buchla Modular Parameters & Patch Matrix
    auto paramsArea = workArea.reduced(8, 4);
    g.setColour(juce::Colour::fromRGB(235, 215, 170));
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText("BUCHLA 259 DUAL OSCILLATOR", paramsArea.removeFromTop(20), juce::Justification::left);

    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colour::fromRGB(210, 200, 180));
    g.drawText("Carrier Freq: 220.0 Hz (Triangle Core)", paramsArea.removeFromTop(18), juce::Justification::left);
    g.drawText("Modulator Freq: 110.0 Hz (Square / Saw)", paramsArea.removeFromTop(18), juce::Justification::left);
    g.drawText("Through-Zero FM (TzFM): 1.80 idx", paramsArea.removeFromTop(18), juce::Justification::left);
    g.drawText("Wavefolder Drive: " + juce::String(m_wavefoldDrive, 1) + "x (Cascaded Diode)", paramsArea.removeFromTop(18), juce::Justification::left);
    g.drawText("Fold Symmetry: " + juce::String(m_wavefoldSymmetry, 2) + " bias", paramsArea.removeFromTop(18), juce::Justification::left);
    g.drawText("Vactrol LPG Decay: 180 ms (Slow Opto)", paramsArea.removeFromTop(18), juce::Justification::left);

    // Patch Jacks visual indicator
    paramsArea.removeFromTop(6);
    auto jacksRow = paramsArea.removeFromTop(22);
    const std::array<juce::Colour, 4> jackCols = {
        juce::Colour::fromRGB(60, 140, 240),  // Blue
        juce::Colour::fromRGB(230, 60, 60),   // Red
        juce::Colour::fromRGB(240, 210, 40),  // Yellow
        juce::Colour::fromRGB(60, 220, 120)   // Green
    };
    for (int j = 0; j < 4; ++j)
    {
        auto jack = jacksRow.removeFromLeft(36);
        const float jx = static_cast<float>(jack.getCentreX());
        const float jy = static_cast<float>(jack.getCentreY());
        g.setColour(jackCols[static_cast<std::size_t>(j)]);
        g.fillEllipse(jx - 7.0f, jy - 7.0f, 14.0f, 14.0f);
        g.setColour(juce::Colour::fromRGB(15, 15, 18));
        g.fillEllipse(jx - 3.5f, jy - 3.5f, 7.0f, 7.0f);
    }
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
    else if (m_kind == DspEditorKind::linearPhaseEq)
    {
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(32);
        const float w = static_cast<float>(area.getWidth());
        const float h = static_cast<float>(area.getHeight());
        const float midY = area.getY() + (h - 26.0f) * 0.5f;

        m_activeMasterEqBand = -1;
        for (std::size_t i = 0; i < m_masterEqBands.size(); ++i)
        {
            const auto& band = m_masterEqBands[i];
            const float normX = (std::log10(band.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
            const float x = area.getX() + normX * w;
            const float y = midY - (band.gainDb / 18.0f) * ((h - 26.0f) * 0.45f);

            if (std::hypot(e.position.x - x, e.position.y - y) <= 15.0f)
            {
                m_activeMasterEqBand = static_cast<int>(i);
                break;
            }
        }
    }
    else if (m_kind == DspEditorKind::resonanceSuppressor)
    {
        const float w = static_cast<float>(getWidth());
        const float scopeH = static_cast<float>(getHeight()) - 30.0f;

        m_activeResoNode = -1;
        for (std::size_t i = 0; i < m_resoNodes.size(); ++i)
        {
            const auto& node = m_resoNodes[i];
            const float normX = (std::log10(node.freqHz) - std::log10(20.0f)) / (std::log10(20000.0f) - std::log10(20.0f));
            const float x = normX * w;
            const float normY = (-node.depthDb) / 24.0f;
            const float y = scopeH * std::clamp(normY, 0.1f, 0.9f);

            if (std::hypot(e.position.x - x, e.position.y - y) <= 15.0f)
            {
                m_activeResoNode = static_cast<int>(i);
                break;
            }
        }
        repaint();
    }
    else if (m_kind == DspEditorKind::multibandImager)
    {
        m_activeCrossoverDivider = -1;
        m_activeImagerBand = -1;

        if (e.position.x > 190)
        {
            const float bandsX = 190.0f;
            const float bandsW = static_cast<float>(getWidth()) - bandsX - 12.0f;
            const float bandW = bandsW / 4.0f;

            // Check if clicked near crossover dividers (dividers 0, 1, 2)
            for (int d = 0; d < 3; ++d)
            {
                const float divX = bandsX + static_cast<float>(d + 1) * bandW;
                if (std::abs(e.position.x - divX) <= 8.0f)
                {
                    m_activeCrossoverDivider = d;
                    return;
                }
            }

            const int bandIdx = std::clamp(static_cast<int>((e.position.x - bandsX) / bandW), 0, 3);
            const float bandLeft = bandsX + static_cast<float>(bandIdx) * bandW;

            // Check if clicked on Solo / Mute pills
            if (e.position.y >= 26.0f && e.position.y <= 44.0f)
            {
                const float relX = e.position.x - bandLeft;
                if (relX < bandW * 0.5f)
                {
                    m_imagerSolos[static_cast<std::size_t>(bandIdx)] = ! m_imagerSolos[static_cast<std::size_t>(bandIdx)];
                }
                else
                {
                    m_imagerMutes[static_cast<std::size_t>(bandIdx)] = ! m_imagerMutes[static_cast<std::size_t>(bandIdx)];
                }
            }
            else
            {
                m_activeImagerBand = bandIdx;
                const float normY = 1.0f - std::clamp(e.position.y / static_cast<float>(getHeight()), 0.0f, 1.0f);
                m_imagerWidths[static_cast<std::size_t>(bandIdx)] = std::clamp(normY * 2.0f, 0.0f, 2.0f);
            }
            repaint();
        }
    }
    else if (m_kind == DspEditorKind::buchlaComplexSynth)
    {
        // Click to adjust wavefold drive and symmetry bias
        const float normX = std::clamp(e.position.x / static_cast<float>(getWidth()), 0.0f, 1.0f);
        const float normY = (static_cast<float>(getHeight()) * 0.5f - e.position.y) / (static_cast<float>(getHeight()) * 0.5f);
        m_wavefoldDrive = std::clamp(0.5f + normX * 4.5f, 0.5f, 5.0f);
        m_wavefoldSymmetry = std::clamp(normY * 0.5f, -0.5f, 0.5f);
        repaint();
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
    else if (m_kind == DspEditorKind::linearPhaseEq && m_activeMasterEqBand >= 0 && m_activeMasterEqBand < static_cast<int>(m_masterEqBands.size()))
    {
        const auto bandIdx = static_cast<std::size_t>(m_activeMasterEqBand);
        auto area = getLocalBounds().reduced(8);
        area.removeFromTop(32);
        const float w = static_cast<float>(area.getWidth());
        const float h = static_cast<float>(area.getHeight());
        const float midY = area.getY() + (h - 26.0f) * 0.5f;

        const float normX = std::clamp((e.position.x - area.getX()) / w, 0.0f, 1.0f);
        m_masterEqBands[bandIdx].freqHz = 20.0f * std::pow(1000.0f, normX);

        const float normY = (midY - e.position.y) / ((h - 26.0f) * 0.45f);
        m_masterEqBands[bandIdx].gainDb = std::clamp(normY * 18.0f, -18.0f, 18.0f);
        repaint();
    }
    else if (m_kind == DspEditorKind::resonanceSuppressor)
    {
        if (m_activeResoNode >= 0 && m_activeResoNode < static_cast<int>(m_resoNodes.size()))
        {
            const float w = static_cast<float>(getWidth());
            const float scopeH = static_cast<float>(getHeight()) - 30.0f;

            const float normX = std::clamp(e.position.x / w, 0.0f, 1.0f);
            m_resoNodes[static_cast<std::size_t>(m_activeResoNode)].freqHz = 20.0f * std::pow(1000.0f, normX);

            const float normY = std::clamp(e.position.y / scopeH, 0.05f, 0.95f);
            m_resoNodes[static_cast<std::size_t>(m_activeResoNode)].depthDb = -normY * 24.0f;
            repaint();
        }
        else
        {
            const float normY = 1.0f - std::clamp(e.position.y / static_cast<float>(getHeight()), 0.0f, 1.0f);
            m_resoDepth = std::clamp(normY, 0.05f, 0.95f);
            const float normX = std::clamp(e.position.x / static_cast<float>(getWidth()), 0.0f, 1.0f);
            m_resoSharpness = std::clamp(0.5f + normX * 4.5f, 0.5f, 5.0f);
            repaint();
        }
    }
    else if (m_kind == DspEditorKind::multibandImager)
    {
        if (m_activeCrossoverDivider >= 0 && m_activeCrossoverDivider < 3)
        {
            const float bandsX = 190.0f;
            const float bandsW = static_cast<float>(getWidth()) - bandsX - 12.0f;
            const float normX = std::clamp((e.position.x - bandsX) / bandsW, 0.05f, 0.95f);
            const float newFreq = 40.0f * std::pow(500.0f, normX);
            m_imagerCrossovers[static_cast<std::size_t>(m_activeCrossoverDivider)] = newFreq;
            repaint();
        }
        else if (m_activeImagerBand >= 0 && m_activeImagerBand < 4)
        {
            const float normY = 1.0f - std::clamp(e.position.y / static_cast<float>(getHeight()), 0.0f, 1.0f);
            m_imagerWidths[static_cast<std::size_t>(m_activeImagerBand)] = std::clamp(normY * 2.0f, 0.0f, 2.0f);
            repaint();
        }
    }
    else if (m_kind == DspEditorKind::buchlaComplexSynth)
    {
        const float normX = std::clamp(e.position.x / static_cast<float>(getWidth()), 0.0f, 1.0f);
        const float normY = (static_cast<float>(getHeight()) * 0.5f - e.position.y) / (static_cast<float>(getHeight()) * 0.5f);
        m_wavefoldDrive = std::clamp(0.5f + normX * 4.5f, 0.5f, 5.0f);
        m_wavefoldSymmetry = std::clamp(normY * 0.5f, -0.5f, 0.5f);
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
    else if (m_kind == DspEditorKind::linearPhaseEq && m_activeMasterEqBand >= 0 && m_activeMasterEqBand < static_cast<int>(m_masterEqBands.size()))
    {
        const auto bandIdx = static_cast<std::size_t>(m_activeMasterEqBand);
        m_masterEqBands[bandIdx].q = std::clamp(m_masterEqBands[bandIdx].q + wheel.deltaY * 0.5f, 0.1f, 10.0f);
        repaint();
    }
    else if (m_kind == DspEditorKind::resonanceSuppressor && m_activeResoNode >= 0 && m_activeResoNode < static_cast<int>(m_resoNodes.size()))
    {
        const auto nodeIdx = static_cast<std::size_t>(m_activeResoNode);
        m_resoNodes[nodeIdx].q = std::clamp(m_resoNodes[nodeIdx].q + wheel.deltaY * 0.5f, 0.5f, 12.0f);
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
    centreWithSize(660, 380);
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
    static std::array<juce::Component::SafePointer<DspEditorWindow>, 9> s_activeWindows;
    const auto kindIdx = static_cast<std::size_t>(kind);
    if (kindIdx < s_activeWindows.size() && s_activeWindows[kindIdx] != nullptr)
    {
        s_activeWindows[kindIdx]->toFront(true);
        return;
    }

    juce::String title;
    switch (kind)
    {
    case DspEditorKind::parametricEq: title = "Parametric EQ - " + juce::String(trackName); break;
    case DspEditorKind::compressor: title = "Dynamics Compressor - " + juce::String(trackName); break;
    case DspEditorKind::synth: title = "TNHC Poly Synth - " + juce::String(trackName); break;
    case DspEditorKind::reverb: title = "Algorithmic Reverb - " + juce::String(trackName); break;
    case DspEditorKind::delay: title = "Stereo Delay - " + juce::String(trackName); break;
    case DspEditorKind::resonanceSuppressor: title = "ResoTamer Dynamic Suppressor - " + juce::String(trackName); break;
    case DspEditorKind::multibandImager: title = "StereoScope Mastering Imager - " + juce::String(trackName); break;
    case DspEditorKind::linearPhaseEq: title = "MasterPhase Linear EQ - " + juce::String(trackName); break;
    case DspEditorKind::buchlaComplexSynth: title = "Complex 259 West-Coast Synth - " + juce::String(trackName); break;
    }

    auto* win = new DspEditorWindow(title, kind, std::move(onClose));
    if (kindIdx < s_activeWindows.size())
    {
        s_activeWindows[kindIdx] = win;
    }
}

} // namespace dawg::ui
