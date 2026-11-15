#include "ModulationRackView.h"

#include <cmath>

namespace dawg::ui {

ModulationRackView::ModulationRackView(CommandHandler handler)
    : m_commandHandler(std::move(handler))
{
    m_rack.prepare(48000.0);

    // Setup 8 Macro Knobs
    for (std::size_t i = 0; i < 8; ++i)
    {
        auto& knob = m_macroKnobs[i];
        knob.slider.setRange(0.0, 1.0, 0.01);
        knob.slider.setValue(0.5, juce::dontSendNotification);
        knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 14);
        knob.slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour::fromRGB(40, 200, 230));
        knob.slider.onValueChange = [this, i] {
            m_rack.setMacroValue(i, static_cast<float>(m_macroKnobs[i].slider.getValue()));
        };
        addAndMakeVisible(knob.slider);

        knob.label.setText("Macro " + std::to_string(i + 1), juce::dontSendNotification);
        knob.label.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        knob.label.setJustificationType(juce::Justification::centred);
        knob.label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(200, 205, 215));
        addAndMakeVisible(knob.label);
    }

    // LFO Waveforms & Rates
    auto setupLfo = [this](juce::ComboBox& wave, juce::ComboBox& rate, juce::Slider& depth, std::size_t idx) {
        wave.addItem("Sine", 1);
        wave.addItem("Triangle", 2);
        wave.addItem("Saw Up", 3);
        wave.addItem("Saw Down", 4);
        wave.addItem("Square", 5);
        wave.addItem("Random S&H", 6);
        wave.setSelectedId(1, juce::dontSendNotification);
        wave.onChange = [this, &wave, idx] {
            m_rack.setLfoWaveform(idx, static_cast<engine::LfoWaveform>(wave.getSelectedId() - 1));
        };
        addAndMakeVisible(wave);

        rate.addItem("4 Bars", 1);
        rate.addItem("2 Bars", 2);
        rate.addItem("1 Bar", 3);
        rate.addItem("1/2", 4);
        rate.addItem("1/4", 5);
        rate.addItem("1/8", 6);
        rate.addItem("1/16", 7);
        rate.addItem("1/8 Triplet", 8);
        rate.addItem("1/16 Triplet", 9);
        rate.setSelectedId(5, juce::dontSendNotification);
        rate.onChange = [this, &rate, idx] {
            m_rack.setLfoSyncDivision(idx, static_cast<engine::ModSyncDivision>(rate.getSelectedId() - 1));
        };
        addAndMakeVisible(rate);

        depth.setRange(0.0, 1.0, 0.01);
        depth.setValue(1.0, juce::dontSendNotification);
        depth.setSliderStyle(juce::Slider::LinearHorizontal);
        depth.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        depth.onValueChange = [this, &depth, idx] {
            m_rack.setLfoDepth(idx, static_cast<float>(depth.getValue()));
        };
        addAndMakeVisible(depth);
    };

    setupLfo(m_lfo1WaveCombo, m_lfo1RateCombo, m_lfo1DepthSlider, 0);
    setupLfo(m_lfo2WaveCombo, m_lfo2RateCombo, m_lfo2DepthSlider, 1);

    // Envelope Follower
    auto setupSlider = [this](juce::Slider& s, double min, double max, double val) {
        s.setRange(min, max, (max - min) / 100.0);
        s.setValue(val, juce::dontSendNotification);
        s.setSliderStyle(juce::Slider::LinearBarVertical);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(s);
    };

    setupSlider(m_envAttackSlider, 0.5, 200.0, 5.0);
    m_envAttackSlider.onValueChange = [this] {
        m_rack.setEnvFollowerAttack(static_cast<float>(m_envAttackSlider.getValue()));
    };

    setupSlider(m_envReleaseSlider, 10.0, 1000.0, 120.0);
    m_envReleaseSlider.onValueChange = [this] {
        m_rack.setEnvFollowerRelease(static_cast<float>(m_envReleaseSlider.getValue()));
    };

    setupSlider(m_envGainSlider, 0.2, 5.0, 1.5);
    m_envGainSlider.onValueChange = [this] {
        m_rack.setEnvFollowerGain(static_cast<float>(m_envGainSlider.getValue()));
    };

    // Step Modulator Rate
    m_stepRateCombo.addItem("1/4 Beat", 5);
    m_stepRateCombo.addItem("1/8 Beat", 6);
    m_stepRateCombo.addItem("1/16 Beat", 7);
    m_stepRateCombo.addItem("1/32 Beat", 8);
    m_stepRateCombo.setSelectedId(7, juce::dontSendNotification);
    m_stepRateCombo.onChange = [this] {
        m_rack.setStepDivision(static_cast<engine::ModSyncDivision>(m_stepRateCombo.getSelectedId() - 1));
    };
    addAndMakeVisible(m_stepRateCombo);

    startTimerHz(30);
}

ModulationRackView::~ModulationRackView()
{
    stopTimer();
}

void ModulationRackView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    repaint();
}

void ModulationRackView::timerCallback()
{
    // Advance modulation state using current transport
    const double bpm = m_model.project.tempo.bpm > 0.0 ? m_model.project.tempo.bpm : 120.0;
    const auto playPos = m_model.playbackPosition >= 0 ? m_model.playbackPosition : 0;
    m_rack.process(nullptr, 0, 512, bpm, playPos);
    repaint();
}

void ModulationRackView::mouseDown(const juce::MouseEvent& e)
{
    if (m_stepBounds.contains(e.getPosition()) && m_stepBounds.getWidth() > 0)
    {
        const int stepW = m_stepBounds.getWidth() / 16;
        const int stepIdx = (e.getPosition().getX() - m_stepBounds.getX()) / std::max(1, stepW);
        if (stepIdx >= 0 && stepIdx < 16)
        {
            m_draggingStepIdx = stepIdx;
            const float val = std::clamp(1.0f - static_cast<float>(e.getPosition().getY() - m_stepBounds.getY()) / static_cast<float>(m_stepBounds.getHeight()), 0.0f, 1.0f);
            m_rack.setStepValue(static_cast<std::size_t>(stepIdx), val);
            repaint();
        }
    }
}

void ModulationRackView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggingStepIdx >= 0 && m_stepBounds.getWidth() > 0)
    {
        const int stepW = m_stepBounds.getWidth() / 16;
        const int stepIdx = std::clamp((e.getPosition().getX() - m_stepBounds.getX()) / std::max(1, stepW), 0, 15);
        const float val = std::clamp(1.0f - static_cast<float>(e.getPosition().getY() - m_stepBounds.getY()) / static_cast<float>(m_stepBounds.getHeight()), 0.0f, 1.0f);
        m_rack.setStepValue(static_cast<std::size_t>(stepIdx), val);
        repaint();
    }
}

void ModulationRackView::mouseUp(const juce::MouseEvent& /*e*/)
{
    m_draggingStepIdx = -1;
}

void ModulationRackView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(32, 35, 41));

    auto bounds = getLocalBounds();
    auto headerBounds = bounds.removeFromTop(m_headerHeight);
    drawHeader(g, headerBounds);

    // Section 1: 8 Macros on the left (width: 380px)
    auto macroArea = bounds.removeFromLeft(380);
    drawMacroSection(g, macroArea);

    // Section 2: LFO 1 & 2 (width: 260px)
    auto lfoArea = bounds.removeFromLeft(260);
    auto lfo1Bounds = lfoArea.removeFromTop(lfoArea.getHeight() / 2);
    drawLfoSection(g, lfo1Bounds, 0);
    drawLfoSection(g, lfoArea, 1);

    // Section 3: Envelope Follower (width: 140px)
    auto envArea = bounds.removeFromLeft(140);
    drawEnvelopeFollowerSection(g, envArea);

    // Section 4: Step Modulator (remaining area)
    drawStepModulatorSection(g, bounds);
}

void ModulationRackView::drawHeader(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(25, 27, 32));
    g.fillRect(bounds);
    g.setColour(juce::Colour::fromRGB(48, 52, 60));
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));

    g.setColour(juce::Colour::fromRGB(220, 225, 235));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText("Modular Modulation Rack (Macros, LFOs, Envelope Follower & Step Sequencer)",
               bounds.reduced(12, 0), juce::Justification::centredLeft);
}

void ModulationRackView::drawMacroSection(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(27, 29, 35));
    g.fillRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(160, 165, 178));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("TRACK MACRO CONTROLS", bounds.getX() + 10, bounds.getY() + 6, 200, 16, juce::Justification::topLeft);

    // Draw animated modulation rings around macro knobs
    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto& knob = m_macroKnobs[i].slider;
        if (! knob.isVisible())
            continue;

        const auto kBounds = knob.getBounds().toFloat();
        const float radius = std::min(kBounds.getWidth(), kBounds.getHeight()) * 0.42f;
        const float centerX = kBounds.getCentreX();
        const float centerY = kBounds.getCentreY() - 7.0f;

        // Animated modulation pulse ring
        const float lfoMod = (m_rack.lfoValue(0) - 0.5f) * 0.35f;
        const float modVal = std::clamp(static_cast<float>(knob.getValue()) + lfoMod, 0.0f, 1.0f);

        juce::Path ring;
        constexpr float startAngle = 2.4f;
        constexpr float endAngle = 7.0f;
        const float curAngle = startAngle + modVal * (endAngle - startAngle);
        ring.addCentredArc(centerX, centerY, radius + 3.0f, radius + 3.0f, 0.0f, startAngle, curAngle, true);

        g.setColour(juce::Colour::fromRGBA(255, 180, 50, 160));
        g.strokePath(ring, juce::PathStrokeType(2.5f));
    }
}

void ModulationRackView::drawLfoSection(juce::Graphics& g, juce::Rectangle<int> bounds, std::size_t lfoIdx)
{
    g.setColour(juce::Colour::fromRGB(27, 29, 35));
    g.fillRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(100, 200, 255));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText(lfoIdx == 0 ? "LFO 1 (TEMPO-SYNC)" : "LFO 2 (TEMPO-SYNC)", bounds.getX() + 10, bounds.getY() + 6, 180, 16, juce::Justification::topLeft);

    // Draw Oscilloscope Waveform Display
    const auto waveBox = juce::Rectangle<float>(static_cast<float>(bounds.getX() + 10), static_cast<float>(bounds.getY() + 26), 90.0f, static_cast<float>(bounds.getHeight() - 34));
    g.setColour(juce::Colour::fromRGB(18, 20, 24));
    g.fillRoundedRectangle(waveBox, 3.0f);

    juce::Path wavePath;
    const float midY = waveBox.getCentreY();
    const float amp = waveBox.getHeight() * 0.38f;
    for (int x = 0; x < static_cast<int>(waveBox.getWidth()); ++x)
    {
        const float normX = static_cast<float>(x) / waveBox.getWidth();
        const float y = midY - std::sin(normX * 6.28318f) * amp;
        if (x == 0)
            wavePath.startNewSubPath(waveBox.getX() + static_cast<float>(x), y);
        else
            wavePath.lineTo(waveBox.getX() + static_cast<float>(x), y);
    }
    g.setColour(juce::Colour::fromRGB(40, 210, 230));
    g.strokePath(wavePath, juce::PathStrokeType(1.5f));

    // Live Traveling Dot
    const float lfoVal = m_rack.lfoValue(lfoIdx);
    const float dotY = waveBox.getBottom() - lfoVal * waveBox.getHeight();
    g.setColour(juce::Colour::fromRGB(255, 230, 80));
    g.fillEllipse(waveBox.getRight() - 10.0f, dotY - 4.0f, 8.0f, 8.0f);
}

void ModulationRackView::drawEnvelopeFollowerSection(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(27, 29, 35));
    g.fillRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(255, 140, 80));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("ENV FOLLOWER", bounds.getX() + 10, bounds.getY() + 6, 120, 16, juce::Justification::topLeft);

    // Live Meter Bar
    const auto meterBox = juce::Rectangle<float>(static_cast<float>(bounds.getX() + 12), static_cast<float>(bounds.getY() + 28), 16.0f, static_cast<float>(bounds.getHeight() - 38));
    g.setColour(juce::Colour::fromRGB(18, 20, 24));
    g.fillRoundedRectangle(meterBox, 2.0f);

    const float envVal = m_rack.envFollowerValue();
    const float fillH = envVal * meterBox.getHeight();
    g.setColour(juce::Colour::fromRGB(255, 150, 50));
    g.fillRoundedRectangle(juce::Rectangle<float>(meterBox.getX(), meterBox.getBottom() - fillH, meterBox.getWidth(), fillH), 2.0f);
}

void ModulationRackView::drawStepModulatorSection(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(27, 29, 35));
    g.fillRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.drawRoundedRectangle(bounds.reduced(4).toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(200, 120, 255));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("16-STEP MODULATOR", bounds.getX() + 10, bounds.getY() + 6, 160, 16, juce::Justification::topLeft);

    m_stepBounds = bounds.reduced(10, 32);
    if (m_stepBounds.getWidth() <= 0)
        return;

    const int stepW = m_stepBounds.getWidth() / 16;
    const int curStep = m_rack.currentStepIndex();

    for (int s = 0; s < 16; ++s)
    {
        const float val = m_rack.stepValue(static_cast<std::size_t>(s));
        const auto barRect = juce::Rectangle<float>(
            static_cast<float>(m_stepBounds.getX() + s * stepW),
            static_cast<float>(m_stepBounds.getBottom()) - val * static_cast<float>(m_stepBounds.getHeight()),
            static_cast<float>(stepW - 2),
            val * static_cast<float>(m_stepBounds.getHeight()));

        const bool isCur = (s == curStep);
        g.setColour(isCur ? juce::Colour::fromRGB(240, 180, 255) : juce::Colour::fromRGB(150, 70, 210));
        g.fillRoundedRectangle(barRect, 2.0f);

        if (isCur)
        {
            g.setColour(juce::Colour::fromRGB(255, 255, 255));
            g.drawRoundedRectangle(barRect, 2.0f, 1.5f);
        }
    }
}

void ModulationRackView::resized()
{
    // 8 Macros in 2 rows of 4
    const int macroW = 85;
    const int macroH = 65;
    for (std::size_t i = 0; i < 8; ++i)
    {
        const int col = static_cast<int>(i % 4);
        const int row = static_cast<int>(i / 4);
        const int x = 12 + col * (macroW + 4);
        const int y = 42 + row * (macroH + 6);
        m_macroKnobs[i].slider.setBounds(x, y, macroW, macroH - 16);
        m_macroKnobs[i].label.setBounds(x, y + macroH - 16, macroW, 14);
    }

    // LFO controls
    const int lfoX = 490;
    m_lfo1WaveCombo.setBounds(lfoX, 42, 85, 20);
    m_lfo1RateCombo.setBounds(lfoX + 90, 42, 55, 20);
    m_lfo1DepthSlider.setBounds(lfoX, 68, 145, 14);

    const int lfo2Y = 110;
    m_lfo2WaveCombo.setBounds(lfoX, lfo2Y, 85, 20);
    m_lfo2RateCombo.setBounds(lfoX + 90, lfo2Y, 55, 20);
    m_lfo2DepthSlider.setBounds(lfoX, lfo2Y + 26, 145, 14);

    // Envelope Follower controls
    const int envX = 684;
    m_envAttackSlider.setBounds(envX, 44, 16, 80);
    m_envReleaseSlider.setBounds(envX + 22, 44, 16, 80);
    m_envGainSlider.setBounds(envX + 44, 44, 16, 80);

    // Step Modulator rate combo
    m_stepRateCombo.setBounds(getWidth() - 110, 6, 95, 22);
}

} // namespace dawg::ui
