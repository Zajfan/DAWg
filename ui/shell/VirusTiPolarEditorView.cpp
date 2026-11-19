#include "VirusTiPolarEditorView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

VirusTiPolarEditorView::VirusTiPolarEditorView()
    : m_presets(engine::VirusTiPolarSynthNode::factoryPresets())
{
    // Define Knobs with labels, ranges, units
    m_knobs[0]  = {"Density", 9.0f, 1.0f, 9.0f, " saws", {}};
    m_knobs[1]  = {"Detune", 0.42f, 0.0f, 1.0f, "%", {}};
    m_knobs[2]  = {"Spread", 0.90f, 0.0f, 1.0f, "%", {}};
    m_knobs[3]  = {"Osc Mix", 0.50f, 0.0f, 1.0f, "%", {}};

    m_knobs[4]  = {"Cutoff 1", 4800.0f, 20.0f, 20000.0f, " Hz", {}};
    m_knobs[5]  = {"Reso 1", 2.4f, 0.5f, 15.0f, " Q", {}};
    m_knobs[6]  = {"Cutoff 2", 2200.0f, 20.0f, 20000.0f, " Hz", {}};
    m_knobs[7]  = {"Filt Bal", 0.0f, -1.0f, 1.0f, "", {}};

    m_knobs[8]  = {"Drive", 0.35f, 0.0f, 1.0f, "%", {}};
    m_knobs[9]  = {"Attack", 0.004f, 0.001f, 2.0f, " s", {}};
    m_knobs[10] = {"Decay", 0.15f, 0.01f, 3.0f, " s", {}};
    m_knobs[11] = {"Release", 0.30f, 0.01f, 5.0f, " s", {}};

    m_knobs[12] = {"Dly Time", 0.375f, 0.05f, 1.5f, " s", {}};
    m_knobs[13] = {"Dly Mix", 0.35f, 0.0f, 1.0f, "%", {}};
    m_knobs[14] = {"Rev Decay", 0.55f, 0.1f, 1.0f, "%", {}};
    m_knobs[15] = {"Rev Mix", 0.28f, 0.0f, 1.0f, "%", {}};

    // Define LED buttons
    m_ledButtons[0] = {"HYPERSAW", true, {}};
    m_ledButtons[1] = {"SYNC", true, {}};
    m_ledButtons[2] = {"DRIVE", true, {}};
    m_ledButtons[3] = {"DELAY", true, {}};
    m_ledButtons[4] = {"REVERB", true, {}};
    m_ledButtons[5] = {"ARP", false, {}};

    // Setup preset navigation
    for (std::size_t i = 0; i < m_presets.size(); ++i)
    {
        m_presetSelector.addItem(m_presets[i].name, static_cast<int>(i + 1));
    }
    m_presetSelector.setSelectedId(1, juce::dontSendNotification);
    m_presetSelector.onChange = [this] {
        const int idx = m_presetSelector.getSelectedId() - 1;
        if (idx >= 0 && idx < static_cast<int>(m_presets.size()))
        {
            m_currentPresetIdx = idx;
            m_synth.loadPatch(m_presets[static_cast<std::size_t>(idx)]);
            repaint();
        }
    };
    addAndMakeVisible(m_presetSelector);

    m_prevPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_prevPresetBtn.onClick = [this] {
        if (! m_presets.empty())
        {
            m_currentPresetIdx = (m_currentPresetIdx - 1 + static_cast<int>(m_presets.size())) % static_cast<int>(m_presets.size());
            m_presetSelector.setSelectedId(m_currentPresetIdx + 1, juce::sendNotification);
        }
    };
    addAndMakeVisible(m_prevPresetBtn);

    m_nextPresetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 48, 56));
    m_nextPresetBtn.onClick = [this] {
        if (! m_presets.empty())
        {
            m_currentPresetIdx = (m_currentPresetIdx + 1) % static_cast<int>(m_presets.size());
            m_presetSelector.setSelectedId(m_currentPresetIdx + 1, juce::sendNotification);
        }
    };
    addAndMakeVisible(m_nextPresetBtn);

    startTimerHz(30); // 30 FPS LCD scope animation
}

VirusTiPolarEditorView::~VirusTiPolarEditorView()
{
    stopTimer();
}

void VirusTiPolarEditorView::timerCallback()
{
    m_scopePhase += 0.15f;
    if (m_scopePhase >= 2.0f * std::numbers::pi_v<float>)
        m_scopePhase -= 2.0f * std::numbers::pi_v<float>;

    // Shift in live synthetic waveform preview
    for (std::size_t i = 0; i < m_scopeBuffer.size() - 1; ++i)
    {
        m_scopeBuffer[i] = m_scopeBuffer[i + 1];
    }
    const float raw = std::sin(m_scopePhase * 4.0f) * 0.7f
                    + (m_ledButtons[0].state ? std::sin(m_scopePhase * 12.0f) * 0.25f : 0.0f);
    m_scopeBuffer[m_scopeBuffer.size() - 1] = std::clamp(raw, -0.95f, 0.95f);

    repaint();
}

void VirusTiPolarEditorView::drawLcdScreen(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    // Dark Ruby Glass display
    g.setColour(juce::Colour::fromRGB(32, 6, 10));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(65, 15, 22));
    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.5f);

    // Subtle LCD pixel grid background
    g.setColour(juce::Colour::fromRGBA(20, 2, 4, 120));
    for (int y = bounds.getY() + 2; y < bounds.getBottom(); y += 3)
    {
        g.drawHorizontalLine(y, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    auto lcdArea = bounds.reduced(10, 6);
    auto textCol = lcdArea.removeFromLeft(lcdArea.getWidth() - 120);

    // Glowing Amber / Ruby Red alphanumeric text
    g.setColour(juce::Colour::fromRGB(255, 60, 60));
    g.setFont(juce::FontOptions(12.0f).withStyle("Bold"));

    const std::string patchTitle = (! m_presets.empty() && m_currentPresetIdx < static_cast<int>(m_presets.size()))
        ? m_presets[static_cast<std::size_t>(m_currentPresetIdx)].name
        : "Polar Hypersaw Lead";
    const std::string patchCategory = (! m_presets.empty() && m_currentPresetIdx < static_cast<int>(m_presets.size()))
        ? m_presets[static_cast<std::size_t>(m_currentPresetIdx)].category
        : "Lead";

    g.drawText("[TI POLAR] " + patchCategory + ": " + patchTitle, textCol.removeFromTop(18), juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(255, 140, 70));
    g.setFont(juce::FontOptions(10.0f));
    const juce::String subInfo = "SAWS: " + juce::String(static_cast<int>(m_knobs[0].value))
        + " | DETUNE: " + juce::String(static_cast<int>(m_knobs[1].value * 100.0f)) + "%"
        + " | CUT: " + (m_knobs[4].value >= 1000.0f ? juce::String(m_knobs[4].value / 1000.0f, 1) + "kHz" : juce::String(static_cast<int>(m_knobs[4].value)) + "Hz")
        + " | DRIVE: " + juce::String(static_cast<int>(m_knobs[8].value * 100.0f)) + "%";
    g.drawText(subInfo, textCol.removeFromTop(16), juce::Justification::centredLeft);

    // Oscilloscope mini-canvas inside LCD
    auto scopeRect = lcdArea.reduced(2);
    g.setColour(juce::Colour::fromRGB(18, 3, 5));
    g.fillRect(scopeRect);
    g.setColour(juce::Colour::fromRGB(60, 10, 16));
    g.drawRect(scopeRect, 1);

    juce::Path scopePath;
    const float sw = static_cast<float>(scopeRect.getWidth());
    const float sh = static_cast<float>(scopeRect.getHeight());
    const float midY = static_cast<float>(scopeRect.getY()) + sh * 0.5f;

    for (std::size_t i = 0; i < m_scopeBuffer.size(); ++i)
    {
        const float x = static_cast<float>(scopeRect.getX()) + (static_cast<float>(i) / static_cast<float>(m_scopeBuffer.size() - 1)) * sw;
        const float y = midY - m_scopeBuffer[i] * (sh * 0.42f);
        if (i == 0) scopePath.startNewSubPath(x, y); else scopePath.lineTo(x, y);
    }
    g.setColour(juce::Colour::fromRGB(255, 70, 70));
    g.strokePath(scopePath, juce::PathStrokeType(1.5f));
}

void VirusTiPolarEditorView::drawKnob(juce::Graphics& g, const KnurledKnob& knob, const bool /*isHovered*/, const bool isActive)
{
    const auto b = knob.bounds;
    const float cx = static_cast<float>(b.getCentreX());
    const float cy = static_cast<float>(b.getY() + 22);
    constexpr float r = 16.0f;

    // Outer dark ring
    g.setColour(juce::Colour::fromRGB(45, 48, 56));
    g.fillEllipse(cx - r - 2.0f, cy - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f);

    // Knurled body (anodized black aluminum)
    g.setColour(isActive ? juce::Colour::fromRGB(32, 34, 42) : juce::Colour::fromRGB(24, 26, 32));
    g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    // LED Indicator Arc
    const float norm = std::clamp((knob.value - knob.minVal) / (knob.maxVal - knob.minVal), 0.0f, 1.0f);
    constexpr float startAngle = -2.2f;
    constexpr float endAngle = 2.2f;
    const float curAngle = startAngle + norm * (endAngle - startAngle);

    juce::Path arcPath;
    arcPath.addCentredArc(cx, cy, r + 2.0f, r + 2.0f, 0.0f, startAngle, curAngle, true);
    g.setColour(juce::Colour::fromRGB(255, 50, 50));
    g.strokePath(arcPath, juce::PathStrokeType(2.0f));

    // Pointer notch
    const float px = cx + std::sin(curAngle) * (r - 4.0f);
    const float py = cy - std::cos(curAngle) * (r - 4.0f);
    g.setColour(juce::Colours::white);
    g.fillEllipse(px - 1.5f, py - 1.5f, 3.0f, 3.0f);

    // Label
    g.setColour(juce::Colour::fromRGB(60, 65, 75));
    g.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
    g.drawText(knob.label, b.getX(), b.getY() + 40, b.getWidth(), 12, juce::Justification::centred);

    // Value string
    juce::String valStr;
    if (knob.maxVal >= 1000.0f)
    {
        valStr = (knob.value >= 1000.0f) ? (juce::String(knob.value / 1000.0f, 1) + "k") : juce::String(static_cast<int>(knob.value));
    }
    else if (knob.maxVal <= 1.0f)
    {
        valStr = juce::String(static_cast<int>(knob.value * 100.0f)) + "%";
    }
    else
    {
        valStr = juce::String(knob.value, 1) + knob.unit;
    }

    g.setColour(juce::Colour::fromRGB(110, 115, 125));
    g.setFont(juce::FontOptions(8.5f));
    g.drawText(valStr, b.getX(), b.getY() + 51, b.getWidth(), 11, juce::Justification::centred);
}

void VirusTiPolarEditorView::drawLedButton(juce::Graphics& g, const LedButton& btn)
{
    const auto b = btn.bounds;
    g.setColour(juce::Colour::fromRGB(215, 218, 224));
    g.fillRoundedRectangle(b.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(180, 185, 195));
    g.drawRoundedRectangle(b.toFloat(), 3.0f, 1.0f);

    // Glowing ruby LED lamp
    const auto ledRect = juce::Rectangle<float>(static_cast<float>(b.getX() + 5), static_cast<float>(b.getY() + 7), 6.0f, 6.0f);
    if (btn.state)
    {
        // Glowing ON
        g.setColour(juce::Colour::fromRGBA(255, 40, 40, 100));
        g.fillEllipse(ledRect.expanded(3.0f));
        g.setColour(juce::Colour::fromRGB(255, 50, 50));
        g.fillEllipse(ledRect);
    }
    else
    {
        // OFF
        g.setColour(juce::Colour::fromRGB(70, 20, 25));
        g.fillEllipse(ledRect);
    }

    g.setColour(juce::Colour::fromRGB(50, 55, 65));
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    g.drawText(btn.label, b.getX() + 15, b.getY(), b.getWidth() - 17, b.getHeight(), juce::Justification::centredLeft);
}

void VirusTiPolarEditorView::paint(juce::Graphics& g)
{
    // 1. Arctic White / Polar Silver anodized aluminum chassis
    g.fillAll(juce::Colour::fromRGB(234, 237, 242));

    auto total = getLocalBounds();

    // 2. White lacquered wooden end cheeks on left and right
    const auto cheekL = total.removeFromLeft(18);
    const auto cheekR = total.removeFromRight(18);

    g.setColour(juce::Colour::fromRGB(250, 252, 255));
    g.fillRect(cheekL);
    g.fillRect(cheekR);

    g.setColour(juce::Colour::fromRGB(205, 210, 218));
    g.drawVerticalLine(cheekL.getRight(), 0.0f, static_cast<float>(getHeight()));
    g.drawVerticalLine(cheekR.getX(), 0.0f, static_cast<float>(getHeight()));

    // 3. Top branding ribbon
    g.setColour(juce::Colour::fromRGB(45, 50, 60));
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText("ACCESS VIRUS TI POLAR", 26, 8, 220, 18, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(140, 145, 155));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("TOTAL INTEGRATION SYNTHESIZER  |  24-BIT DUAL DSP", 26, 24, 300, 14, juce::Justification::centredLeft);

    // 4. Center Top LCD dot matrix screen
    const juce::Rectangle<int> lcdBounds {330, 8, getWidth() - 330 - 32, 46};
    drawLcdScreen(g, lcdBounds);

    // 5. Section Group Boxes
    constexpr std::array<const char*, 4> groupNames = {
        "OSCILLATORS & HYPERSAW",
        "DUAL CASCADE FILTERS",
        "ENVELOPES & CHARACTER",
        "STEREO TAPE DELAY & REVERB"
    };

    const int sectionW = (getWidth() - 56) / 4;
    const int sectionY = 62;
    const int sectionH = getHeight() - 100;

    for (int s = 0; s < 4; ++s)
    {
        juce::Rectangle<int> box {24 + s * sectionW + 2, sectionY, sectionW - 4, sectionH};
        g.setColour(juce::Colour::fromRGB(226, 229, 235));
        g.fillRoundedRectangle(box.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(205, 209, 218));
        g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);

        // Section header tag
        g.setColour(juce::Colour::fromRGB(80, 85, 98));
        g.setFont(juce::FontOptions(8.5f).withStyle("Bold"));
        g.drawText(groupNames[static_cast<std::size_t>(s)], box.removeFromTop(16), juce::Justification::centred);
    }

    // 6. Draw Knobs
    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        drawKnob(g, m_knobs[i], false, m_activeKnobIdx == static_cast<int>(i));
    }

    // 7. Draw LED buttons
    for (const auto& btn : m_ledButtons)
    {
        drawLedButton(g, btn);
    }
}

void VirusTiPolarEditorView::resized()
{
    const int sectionW = (getWidth() - 56) / 4;
    const int sectionY = 62;

    // Distribute 4 knobs per section (2x2 grid)
    for (int s = 0; s < 4; ++s)
    {
        const int left = 24 + s * sectionW + 6;
        const int cellW = (sectionW - 16) / 2;

        for (int k = 0; k < 4; ++k)
        {
            const int idx = s * 4 + k;
            const int col = k % 2;
            const int row = k / 2;
            m_knobs[static_cast<std::size_t>(idx)].bounds = juce::Rectangle<int> {
                left + col * cellW, sectionY + 22 + row * 66, cellW, 64
            };
        }
    }

    // Bottom LED buttons row
    const int btnY = getHeight() - 32;
    for (std::size_t b = 0; b < m_ledButtons.size(); ++b)
    {
        m_ledButtons[b].bounds = juce::Rectangle<int> {
            26 + static_cast<int>(b) * 105, btnY, 95, 22
        };
    }

    // Preset selector buttons
    m_prevPresetBtn.setBounds(getWidth() - 170, btnY, 24, 22);
    m_presetSelector.setBounds(getWidth() - 144, btnY, 110, 22);
    m_nextPresetBtn.setBounds(getWidth() - 32, btnY, 24, 22);
}

void VirusTiPolarEditorView::mouseDown(const juce::MouseEvent& e)
{
    // Check knobs
    m_activeKnobIdx = -1;
    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        if (m_knobs[i].bounds.contains(e.getPosition()))
        {
            m_activeKnobIdx = static_cast<int>(i);
            m_dragStartPos = e.getPosition();
            m_dragStartValue = m_knobs[i].value;
            repaint();
            return;
        }
    }

    // Check LED buttons
    for (std::size_t b = 0; b < m_ledButtons.size(); ++b)
    {
        if (m_ledButtons[b].bounds.contains(e.getPosition()))
        {
            m_ledButtons[b].state = ! m_ledButtons[b].state;

            // Update synth parameters
            if (b == 0) m_synth.setHypersawDetune(m_ledButtons[b].state ? m_knobs[1].value : 0.0f);
            else if (b == 3) m_synth.setDelayMix(m_ledButtons[b].state ? m_knobs[13].value : 0.0f);
            else if (b == 4) m_synth.setReverbMix(m_ledButtons[b].state ? m_knobs[15].value : 0.0f);

            repaint();
            return;
        }
    }
}

void VirusTiPolarEditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_activeKnobIdx >= 0 && m_activeKnobIdx < static_cast<int>(m_knobs.size()))
    {
        auto& knob = m_knobs[static_cast<std::size_t>(m_activeKnobIdx)];
        const int deltaY = m_dragStartPos.y - e.y;
        const float range = knob.maxVal - knob.minVal;
        const float deltaNorm = static_cast<float>(deltaY) * 0.005f;

        knob.value = std::clamp(m_dragStartValue + deltaNorm * range, knob.minVal, knob.maxVal);

        // Apply to synth
        if (m_activeKnobIdx == 1) m_synth.setHypersawDetune(knob.value);
        else if (m_activeKnobIdx == 4) m_synth.setFilter1Cutoff(knob.value);
        else if (m_activeKnobIdx == 5) m_synth.setFilter1Resonance(knob.value);
        else if (m_activeKnobIdx == 8) m_synth.setDrive(knob.value);
        else if (m_activeKnobIdx == 13) m_synth.setDelayMix(knob.value);
        else if (m_activeKnobIdx == 15) m_synth.setReverbMix(knob.value);

        repaint();
    }
}

void VirusTiPolarEditorView::mouseUp(const juce::MouseEvent&)
{
    m_activeKnobIdx = -1;
    repaint();
}

void VirusTiPolarEditorView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        if (m_knobs[i].bounds.contains(e.getPosition()))
        {
            auto& knob = m_knobs[i];
            const float range = knob.maxVal - knob.minVal;
            knob.value = std::clamp(knob.value + wheel.deltaY * 0.05f * range, knob.minVal, knob.maxVal);

            if (i == 1) m_synth.setHypersawDetune(knob.value);
            else if (i == 4) m_synth.setFilter1Cutoff(knob.value);
            else if (i == 5) m_synth.setFilter1Resonance(knob.value);
            else if (i == 8) m_synth.setDrive(knob.value);
            else if (i == 13) m_synth.setDelayMix(knob.value);
            else if (i == 15) m_synth.setReverbMix(knob.value);

            repaint();
            return;
        }
    }
}

// ============================================================================
// VirusTiPolarWindow
// ============================================================================

std::unique_ptr<VirusTiPolarWindow> VirusTiPolarWindow::s_instance = nullptr;

void VirusTiPolarWindow::open()
{
    if (! s_instance)
    {
        s_instance = std::make_unique<VirusTiPolarWindow>();
    }
    s_instance->setVisible(true);
    s_instance->toFront(true);
}

void VirusTiPolarWindow::close()
{
    if (s_instance)
    {
        s_instance->setVisible(false);
    }
}

bool VirusTiPolarWindow::isOpen() noexcept
{
    return s_instance != nullptr && s_instance->isVisible();
}

VirusTiPolarWindow::VirusTiPolarWindow(std::function<void()> onClose)
    : DocumentWindow(
          "Access Virus TI Polar - Native Synth (DAWg)",
          juce::Colour::fromRGB(230, 233, 238),
          DocumentWindow::closeButton,
          true)
    , m_editor(std::make_unique<VirusTiPolarEditorView>())
    , m_onClose(std::move(onClose))
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(m_editor.get(), true);
    setResizable(false, false);
    centreWithSize(840, 280);
    setVisible(true);
}

void VirusTiPolarWindow::closeButtonPressed()
{
    setVisible(false);
    if (m_onClose)
    {
        m_onClose();
    }
}

} // namespace dawg::ui
