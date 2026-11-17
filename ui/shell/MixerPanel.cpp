#include "MixerPanel.h"
#include "DspEditorDialog.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

// ============================================================================
// ChannelStripComponent
// ============================================================================

ChannelStripComponent::ChannelStripComponent(CommandHandler handler)
    : m_handler(std::move(handler))
{
    m_nameLabel.setJustificationType(juce::Justification::centred);
    m_nameLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(m_nameLabel);

    m_panKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_panKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    m_panKnob.setRange(-1.0, 1.0, 0.01);
    m_panKnob.setValue(0.0, juce::dontSendNotification);
    addAndMakeVisible(m_panKnob);

    m_fader.setSliderStyle(juce::Slider::LinearVertical);
    m_fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 16);
    m_fader.setRange(0.0, 2.0, 0.01);
    m_fader.setValue(1.0, juce::dontSendNotification);
    addAndMakeVisible(m_fader);

    addAndMakeVisible(m_muteButton);
    addAndMakeVisible(m_soloButton);
    addAndMakeVisible(m_phaseButton);
    addAndMakeVisible(m_armButton);
    addAndMakeVisible(m_eqButton);
    addAndMakeVisible(m_sslEqButton);
    m_sslEqButton.setClickingTogglesState(true);

    m_panLawCombo.addItem("-3dB", 1);
    m_panLawCombo.addItem("-4.5dB", 2);
    m_panLawCombo.addItem("-6dB", 3);
    m_panLawCombo.addItem("0dB", 4);
    m_panLawCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_panLawCombo);

    m_vcaCombo.addItem("VCA -", 1);
    for (int v = 1; v <= 8; ++v)
        m_vcaCombo.addItem("VCA " + std::to_string(v), v + 1);
    m_vcaCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_vcaCombo);

    addAndMakeVisible(m_compButton);
    addAndMakeVisible(m_synthButton);
    addAndMakeVisible(m_revButton);
    addAndMakeVisible(m_delayButton);
    addAndMakeVisible(m_limiterButton);

    m_panKnob.onValueChange = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_fader.onValueChange = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_muteButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackMute {
            m_trackId,
            m_muteButton.getToggleState()
        });
    };

    m_soloButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackSolo {
            m_trackId,
            m_soloButton.getToggleState()
        });
    };

    m_phaseButton.onClick = [this] {
        if (! m_updatingFromModel)
            sendGainPan();
    };

    m_armButton.onClick = [this] {
        if (m_updatingFromModel || ! m_handler || m_trackId == 0)
            return;
        m_handler(project::SetTrackRecordArm {
            m_trackId,
            m_armButton.getToggleState()
        });
    };

    m_eqButton.onClick = [this] {
        if (m_eqButton.getToggleState())
            m_eqButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(60, 160, 220));
        DspEditorWindow::open(DspEditorKind::parametricEq, m_nameLabel.getText().toStdString());
    };

    m_compButton.onClick = [this] {
        if (m_compButton.getToggleState())
            m_compButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 140, 50));
        DspEditorWindow::open(DspEditorKind::compressor, m_nameLabel.getText().toStdString());
    };

    m_synthButton.onClick = [this] {
        if (m_synthButton.getToggleState())
            m_synthButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(180, 80, 230));
        DspEditorWindow::open(DspEditorKind::synth, m_nameLabel.getText().toStdString());
    };

    m_revButton.onClick = [this] {
        if (m_revButton.getToggleState())
            m_revButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(50, 180, 220));
        DspEditorWindow::open(DspEditorKind::reverb, m_nameLabel.getText().toStdString());
    };

    m_delayButton.onClick = [this] {
        if (m_delayButton.getToggleState())
            m_delayButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(230, 170, 40));
        DspEditorWindow::open(DspEditorKind::delay, m_nameLabel.getText().toStdString());
    };

    m_limiterButton.onClick = [this] {
        if (m_limiterButton.getToggleState())
            m_limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 60, 80));
    };

    m_muteButton.setClickingTogglesState(true);
    m_soloButton.setClickingTogglesState(true);
    m_phaseButton.setClickingTogglesState(true);
    m_armButton.setClickingTogglesState(true);
    m_eqButton.setClickingTogglesState(true);
    m_compButton.setClickingTogglesState(true);
    m_synthButton.setClickingTogglesState(true);
    m_revButton.setClickingTogglesState(true);
    m_delayButton.setClickingTogglesState(true);
    m_limiterButton.setClickingTogglesState(true);
}

void ChannelStripComponent::sendGainPan()
{
    if (! m_handler || m_trackId == 0)
        return;
    m_handler(project::SetTrackGainPan {
        m_trackId,
        static_cast<float>(m_fader.getValue()),
        static_cast<float>(m_panKnob.getValue()),
        m_phaseButton.getToggleState()
    });
}

void ChannelStripComponent::updateTrack(const project::Track& track)
{
    m_trackId = track.id;
    m_updatingFromModel = true;

    m_nameLabel.setText(track.name, juce::dontSendNotification);
    m_fader.setValue(track.gainLinear, juce::dontSendNotification);
    m_panKnob.setValue(track.pan, juce::dontSendNotification);
    m_muteButton.setToggleState(track.muted, juce::dontSendNotification);
    m_soloButton.setToggleState(track.solo, juce::dontSendNotification);
    m_phaseButton.setToggleState(track.phaseInvert, juce::dontSendNotification);
    m_armButton.setToggleState(track.recordArmed, juce::dontSendNotification);

    if (track.muted)
        m_muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(200, 140, 30));
    if (track.solo)
        m_soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 200, 40));
    if (track.phaseInvert)
        m_phaseButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(140, 90, 220));
    if (track.recordArmed)
        m_armButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(220, 40, 40));

    m_updatingFromModel = false;
}

void ChannelStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(36, 38, 44));
    g.setColour(juce::Colour::fromRGB(50, 54, 62));
    g.drawRect(getLocalBounds(), 1);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 4);

    m_nameLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(2);

    // DSP Inserts row 1: EQ, SSL 4k
    auto dspRow1 = area.removeFromTop(16);
    m_eqButton.setBounds(dspRow1.removeFromLeft(dspRow1.getWidth() / 2).reduced(1));
    m_sslEqButton.setBounds(dspRow1.reduced(1));
    area.removeFromTop(1);

    // DSP Inserts row 2: COMP, SYNTH
    auto dspRow2 = area.removeFromTop(16);
    m_compButton.setBounds(dspRow2.removeFromLeft(dspRow2.getWidth() / 2).reduced(1));
    m_synthButton.setBounds(dspRow2.reduced(1));
    area.removeFromTop(1);

    // DSP Inserts row 3: REV, DLY
    auto dspRow3 = area.removeFromTop(16);
    m_revButton.setBounds(dspRow3.removeFromLeft(dspRow3.getWidth() / 2).reduced(1));
    m_delayButton.setBounds(dspRow3.reduced(1));
    area.removeFromTop(2);

    // Routing Dropdowns: Pan Law & VCA
    auto routeRow = area.removeFromTop(18);
    m_panLawCombo.setBounds(routeRow.removeFromLeft(routeRow.getWidth() / 2).reduced(1));
    m_vcaCombo.setBounds(routeRow.reduced(1));
    area.removeFromTop(2);

    m_panKnob.setBounds(area.removeFromTop(24).reduced(6, 0));
    area.removeFromTop(2);

    // Button row: M, S, Ø, REC
    auto buttonRow1 = area.removeFromTop(20);
    m_muteButton.setBounds(buttonRow1.removeFromLeft(buttonRow1.getWidth() / 2).reduced(1));
    m_soloButton.setBounds(buttonRow1.reduced(1));
    area.removeFromTop(2);

    auto buttonRow2 = area.removeFromTop(20);
    m_phaseButton.setBounds(buttonRow2.removeFromLeft(buttonRow2.getWidth() / 2).reduced(1));
    m_armButton.setBounds(buttonRow2.reduced(1));
    area.removeFromTop(3);

    m_fader.setBounds(area);
}

// ============================================================================
// MasterStripComponent
// ============================================================================

MasterStripComponent::MasterStripComponent(LimiterCallback limiterCb)
    : m_limiterCallback(std::move(limiterCb))
{
    m_nameLabel.setJustificationType(juce::Justification::centred);
    m_nameLabel.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    addAndMakeVisible(m_nameLabel);

    m_limiterButton.setClickingTogglesState(true);
    m_limiterButton.onClick = [this] {
        const bool active = m_limiterButton.getToggleState();
        if (m_limiterCallback)
            m_limiterCallback(active);
        if (active)
            m_limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(40, 160, 220));
    };
    addAndMakeVisible(m_limiterButton);
}

void MasterStripComponent::updateMeter(const engine::MeterReading& meter, const bool limiterActive)
{
    m_meter = meter;
    m_limiterActive = limiterActive;
    m_limiterButton.setToggleState(limiterActive, juce::dontSendNotification);
    if (limiterActive)
        m_limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(40, 160, 220));

    // Decay falloff
    constexpr float decay = 0.85f;
    m_displayPeakL = std::max(meter.peakLeft, m_displayPeakL * decay);
    m_displayPeakR = std::max(meter.peakRight, m_displayPeakR * decay);
    m_displayRmsL = std::max(meter.rmsLeft, m_displayRmsL * decay);
    m_displayRmsR = std::max(meter.rmsRight, m_displayRmsR * decay);

    repaint();
}

void MasterStripComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(28, 30, 36));
    g.setColour(juce::Colour::fromRGB(60, 64, 74));
    g.drawRect(getLocalBounds(), 1);

    auto meterArea = getLocalBounds();
    meterArea.removeFromTop(44); // room for label and limiter button

    // Bottom section: Phase Correlation & LUFS Radar Readout
    auto bottomArea = meterArea.removeFromBottom(36).reduced(4, 2);
    auto lufsBadge = bottomArea.removeFromTop(16);
    g.setColour(juce::Colour::fromRGB(36, 40, 50));
    g.fillRoundedRectangle(lufsBadge.toFloat(), 2.0f);
    g.setColour(juce::Colour::fromRGB(120, 220, 255));
    g.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
    g.drawText(juce::String(m_lufs, 1) + " LUFS", lufsBadge, juce::Justification::centred);

    // Phase Correlation Bar (-1 to +1)
    auto corrBar = bottomArea.reduced(2, 2);
    g.setColour(juce::Colour::fromRGB(20, 22, 28));
    g.fillRect(corrBar);
    const float midX = corrBar.toFloat().getCentreX();
    const float corrNorm = std::clamp(m_correlation, -1.0f, 1.0f);
    const float corrWidth = (corrBar.getWidth() * 0.48f) * corrNorm;
    g.setColour(corrNorm >= 0.0f ? juce::Colour::fromRGB(50, 200, 100) : juce::Colour::fromRGB(240, 60, 60));
    if (corrNorm >= 0.0f)
        g.fillRect(juce::Rectangle<float>(midX, static_cast<float>(corrBar.getY()), corrWidth, static_cast<float>(corrBar.getHeight())));
    else
        g.fillRect(juce::Rectangle<float>(midX + corrWidth, static_cast<float>(corrBar.getY()), -corrWidth, static_cast<float>(corrBar.getHeight())));

    meterArea.reduce(8, 4);

    const int barWidth = (meterArea.getWidth() - 4) / 2;

    auto leftBar = meterArea.removeFromLeft(barWidth);
    meterArea.removeFromLeft(4);
    auto rightBar = meterArea.removeFromLeft(barWidth);

    drawMeterBar(g, leftBar, m_displayPeakL, m_displayRmsL);
    drawMeterBar(g, rightBar, m_displayPeakR, m_displayRmsR);
}

void MasterStripComponent::drawMeterBar(juce::Graphics& g, juce::Rectangle<int> barBounds, float peak, float rms)
{
    g.setColour(juce::Colour::fromRGB(18, 19, 22));
    g.fillRect(barBounds);

    const int totalHeight = barBounds.getHeight();
    if (totalHeight <= 0)
        return;

    const float clampedRms = std::clamp(rms, 0.0f, 1.5f);
    const float clampedPeak = std::clamp(peak, 0.0f, 1.5f);

    const int rmsHeight = static_cast<int>(std::round((clampedRms / 1.0f) * static_cast<float>(totalHeight)));
    const int peakY = barBounds.getBottom() - static_cast<int>(std::round((clampedPeak / 1.0f) * static_cast<float>(totalHeight)));

    auto rmsFill = barBounds.removeFromBottom(std::min(totalHeight, rmsHeight));
    if (! rmsFill.isEmpty())
    {
        if (clampedRms > 1.0f)
            g.setColour(juce::Colour::fromRGB(220, 50, 50));
        else if (clampedRms > 0.5f)
            g.setColour(juce::Colour::fromRGB(220, 180, 40));
        else
            g.setColour(juce::Colour::fromRGB(50, 190, 80));

        g.fillRect(rmsFill);
    }

    if (peakY >= barBounds.getY() && peakY <= barBounds.getBottom())
    {
        g.setColour(clampedPeak > 1.0f ? juce::Colour::fromRGB(255, 60, 60) : juce::Colours::white);
        g.drawHorizontalLine(peakY, static_cast<float>(barBounds.getX()), static_cast<float>(barBounds.getRight()));
    }
}

void MasterStripComponent::resized()
{
    auto area = getLocalBounds().reduced(3, 4);
    m_nameLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(2);
    m_limiterButton.setBounds(area.removeFromTop(20).reduced(2, 0));
}

// ============================================================================
// ModulatorRackComponent
// ============================================================================

ModulatorRackComponent::ModulatorRackComponent()
{
    const std::array<std::pair<std::string, juce::Colour>, 8> config {{
        {"Cutoff", juce::Colour::fromRGB(0, 229, 255)},
        {"Res",    juce::Colour::fromRGB(255, 145, 0)},
        {"Drive",  juce::Colour::fromRGB(255, 82, 82)},
        {"Space",  juce::Colour::fromRGB(179, 136, 255)},
        {"Decay",  juce::Colour::fromRGB(105, 240, 174)},
        {"Tone",   juce::Colour::fromRGB(255, 215, 64)},
        {"Width",  juce::Colour::fromRGB(68, 138, 255)},
        {"Morph",  juce::Colour::fromRGB(255, 64, 129)}
    }};

    for (std::size_t i = 0; i < 8; ++i)
    {
        auto& m = m_macros[i];
        m.name = config[i].first;
        m.color = config[i].second;

        m.slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        m.slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m.slider.setRange(0.0, 1.0, 0.01);
        m.slider.setValue(0.5, juce::dontSendNotification);
        addAndMakeVisible(m.slider);

        m.label.setText(m.name, juce::dontSendNotification);
        m.label.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
        m.label.setJustificationType(juce::Justification::centred);
        m.label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(160, 165, 175));
        addAndMakeVisible(m.label);
    }

    m_lfoShapeCombo.addItem("LFO: Sine", 1);
    m_lfoShapeCombo.addItem("LFO: Triangle", 2);
    m_lfoShapeCombo.addItem("LFO: Saw", 3);
    m_lfoShapeCombo.addItem("LFO: Square", 4);
    m_lfoShapeCombo.addItem("LFO: S&H", 5);
    m_lfoShapeCombo.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(m_lfoShapeCombo);

    m_lfoRateCombo.addItem("1/4", 1);
    m_lfoRateCombo.addItem("1/8", 2);
    m_lfoRateCombo.addItem("1/16", 3);
    m_lfoRateCombo.setSelectedId(2, juce::dontSendNotification);
    addAndMakeVisible(m_lfoRateCombo);

    startTimerHz(25);
}

ModulatorRackComponent::~ModulatorRackComponent()
{
    stopTimer();
}

void ModulatorRackComponent::timerCallback()
{
    m_lfoPhase += 0.08f;
    if (m_lfoPhase > juce::MathConstants<float>::twoPi)
        m_lfoPhase -= juce::MathConstants<float>::twoPi;
    repaint();
}

void ModulatorRackComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(22, 23, 27));
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRect(getLocalBounds(), 1);

    auto bounds = getLocalBounds();
    auto titleArea = bounds.removeFromTop(20);
    g.setColour(juce::Colour::fromRGB(30, 33, 38));
    g.fillRect(titleArea);
    g.setColour(juce::Colour::fromRGB(200, 205, 215));
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText("MACROS & MODULATORS", titleArea, juce::Justification::centred);

    // Draw animated LFO visualizer waveform at the bottom
    const auto lfoBox = bounds.removeFromBottom(34).reduced(6, 2);
    g.setColour(juce::Colour::fromRGB(16, 17, 20));
    g.fillRoundedRectangle(lfoBox.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.drawRoundedRectangle(lfoBox.toFloat(), 3.0f, 1.0f);

    juce::Path p;
    const float midY = static_cast<float>(lfoBox.getCentreY());
    const float amp = static_cast<float>(lfoBox.getHeight()) * 0.38f;
    bool first = true;
    for (int x = lfoBox.getX(); x < lfoBox.getRight(); ++x)
    {
        const float t = static_cast<float>(x - lfoBox.getX()) / static_cast<float>(lfoBox.getWidth());
        const float y = midY + std::sin(t * juce::MathConstants<float>::twoPi * 2.0f + m_lfoPhase) * amp;
        if (first) { p.startNewSubPath(static_cast<float>(x), y); first = false; }
        else p.lineTo(static_cast<float>(x), y);
    }
    g.setColour(juce::Colour::fromRGB(0, 229, 255));
    g.strokePath(p, juce::PathStrokeType(1.5f));

    // Animated traveling phase dot
    const float dotX = static_cast<float>(lfoBox.getX()) + (m_lfoPhase / juce::MathConstants<float>::twoPi) * static_cast<float>(lfoBox.getWidth());
    const float dotY = midY + std::sin((dotX - static_cast<float>(lfoBox.getX())) / static_cast<float>(lfoBox.getWidth()) * juce::MathConstants<float>::twoPi * 2.0f + m_lfoPhase) * amp;
    g.setColour(juce::Colour::fromRGB(255, 235, 100));
    g.fillEllipse(dotX - 2.5f, dotY - 2.5f, 5.0f, 5.0f);
}

void ModulatorRackComponent::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(20); // title
    auto lfoArea = area.removeFromBottom(58);

    // LFO controls row
    auto lfoControls = lfoArea.removeFromTop(22).reduced(4, 1);
    m_lfoShapeCombo.setBounds(lfoControls.removeFromLeft(lfoControls.getWidth() - 60));
    m_lfoRateCombo.setBounds(lfoControls.withTrimmedLeft(4));

    // 8 macros in 2 rows of 4
    const int colW = area.getWidth() / 4;
    const int rowH = area.getHeight() / 2;

    for (std::size_t i = 0; i < 8; ++i)
    {
        const int r = static_cast<int>(i / 4);
        const int c = static_cast<int>(i % 4);
        auto cell = juce::Rectangle<int>(c * colW, area.getY() + r * rowH, colW, rowH).reduced(2, 1);
        m_macros[i].label.setBounds(cell.removeFromBottom(12));
        m_macros[i].slider.setBounds(cell);
    }
}

// ============================================================================
// MixerPanel
// ============================================================================

MixerPanel::MixerPanel(CommandHandler handler, LimiterCallback limiterCb)
    : m_handler(std::move(handler))
    , m_masterStrip(std::move(limiterCb))
{
    m_viewport.setViewedComponent(&m_tracksContainer, false);
    m_viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(m_viewport);
    addAndMakeVisible(m_masterStrip);

    m_toggleModulatorsBtn.onClick = [this] { toggleModulatorRack(); };
    m_toggleModulatorsBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(38, 42, 50));
    addAndMakeVisible(m_toggleModulatorsBtn);
    addAndMakeVisible(m_modulatorRack);
}

void MixerPanel::toggleModulatorRack()
{
    m_showModulators = ! m_showModulators;
    m_modulatorRack.setVisible(m_showModulators);
    m_toggleModulatorsBtn.setColour(juce::TextButton::buttonColourId,
                                   m_showModulators ? juce::Colour::fromRGB(45, 95, 140) : juce::Colour::fromRGB(38, 42, 50));
    resized();
}

void MixerPanel::setViewModel(const EditorViewModel& model)
{
    const auto& tracks = model.project.tracks;
    while (m_strips.size() < tracks.size())
    {
        auto strip = std::make_unique<ChannelStripComponent>(m_handler);
        m_tracksContainer.addAndMakeVisible(strip.get());
        m_strips.push_back(std::move(strip));
    }
    while (m_strips.size() > tracks.size())
    {
        m_strips.pop_back();
    }

    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        m_strips[i]->updateTrack(tracks[i]);
    }

    m_masterStrip.updateMeter(model.masterMeter, model.masterLimiterEnabled);
    resized();
}

void MixerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 25, 29));
    g.setColour(juce::Colour::fromRGB(40, 42, 50));
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void MixerPanel::resized()
{
    auto area = getLocalBounds();
    constexpr int masterWidth = 84;
    auto masterArea = area.removeFromRight(masterWidth);
    m_toggleModulatorsBtn.setBounds(masterArea.removeFromTop(20).reduced(2, 1));
    m_masterStrip.setBounds(masterArea);

    if (m_showModulators)
    {
        constexpr int modWidth = 220;
        m_modulatorRack.setBounds(area.removeFromRight(modWidth));
    }

    m_viewport.setBounds(area);

    constexpr int stripWidth = 76;
    const int totalWidth = static_cast<int>(m_strips.size()) * stripWidth;
    m_tracksContainer.setBounds(0, 0, std::max(totalWidth, m_viewport.getWidth()), m_viewport.getHeight());

    int x = 0;
    for (auto& strip : m_strips)
    {
        strip->setBounds(x, 0, stripWidth, m_tracksContainer.getHeight());
        x += stripWidth;
    }
}

} // namespace dawg::ui
