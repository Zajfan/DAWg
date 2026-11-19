#include "OmniFmEditorView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

std::unique_ptr<OmniFmWindow> OmniFmWindow::s_instance = nullptr;

OmniFmEditorView::OmniFmEditorView(engine::OmniFmSynthNode* node)
    : m_node(node)
{
    setSize(920, 380);
    startTimerHz(30);
}

OmniFmEditorView::~OmniFmEditorView()
{
    stopTimer();
}

void OmniFmEditorView::timerCallback()
{
    repaint();
}

void OmniFmEditorView::resized()
{
    const int opW = 120;
    const int opH = 260;
    const int startX = 24;
    const int startY = 96;

    for (int i = 0; i < 6; ++i)
    {
        auto& op = m_opGuis[static_cast<std::size_t>(i)];
        op.box = {startX + i * (opW + 8), startY, opW, opH};

        const int bx = op.box.getX();
        const int by = op.box.getY();

        op.btnPower  = {bx + 8,  by + 8, 46, 20};
        op.btnWave   = {bx + 58, by + 8, 54, 20};

        op.knobRatio  = {bx + 10, by + 36, 46, 50};
        op.knobLevel  = {bx + 64, by + 36, 46, 50};

        op.knobDetune = {bx + 10, by + 94, 46, 50};
        op.knobAttack = {bx + 64, by + 94, 46, 50};

        op.knobDecay  = {bx + 37, by + 152, 46, 50};
    }

    // Top Master Section
    m_btnAlgoPrev    = {360, 24, 28, 22};
    m_btnAlgoNext    = {430, 24, 28, 22};
    m_knobFeedback   = {480, 16, 50, 52};

    m_btnPresetPrev  = {getWidth() - 170, 22, 30, 24};
    m_btnPresetNext  = {getWidth() - 50,  22, 30, 24};
}

void OmniFmEditorView::drawKnob(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& label, const float value, const bool isHot)
{
    // Label above knob
    g.setColour(juce::Colour::fromRGB(180, 210, 205));
    g.setFont(juce::FontOptions(8.5f).withStyle("Bold"));
    g.drawText(label, bounds.getX() - 4, bounds.getY() - 1, bounds.getWidth() + 8, 11, juce::Justification::centred);

    const juce::Rectangle<float> dialArea {
        static_cast<float>(bounds.getX() + (bounds.getWidth() - 28) / 2),
        static_cast<float>(bounds.getY() + 14),
        28.0f,
        28.0f
    };

    // Dark dial face
    g.setColour(juce::Colour::fromRGB(16, 24, 24));
    g.fillEllipse(dialArea);
    g.setColour(isHot ? juce::Colours::white : juce::Colour::fromRGB(65, 95, 90));
    g.drawEllipse(dialArea, 1.0f);

    // Mint-green indicator arc
    const float angle = -2.2f + value * 4.4f;
    juce::Path arc;
    arc.addCentredArc(dialArea.getCentreX(), dialArea.getCentreY(), 11.0f, 11.0f, 0.0f, -2.2f, angle, true);
    g.setColour(juce::Colour::fromRGB(65, 230, 180));
    g.strokePath(arc, juce::PathStrokeType(2.0f));

    // Pointer line
    const float cosA = std::cos(angle - static_cast<float>(std::numbers::pi) * 0.5f);
    const float sinA = std::sin(angle - static_cast<float>(std::numbers::pi) * 0.5f);
    g.setColour(juce::Colours::white);
    g.drawLine(dialArea.getCentreX(), dialArea.getCentreY(),
               dialArea.getCentreX() + cosA * 10.0f, dialArea.getCentreY() + sinA * 10.0f, 1.5f);
}

void OmniFmEditorView::drawAlgorithmDisplay(juce::Graphics& g, const juce::Rectangle<int>& bounds)
{
    g.setColour(juce::Colour::fromRGB(12, 22, 20));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(45, 95, 85));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

    const int algo = m_node != nullptr ? m_node->patch().algorithm : 16;

    g.setColour(juce::Colour::fromRGB(65, 240, 185));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("ALGO " + std::to_string(algo), bounds.reduced(4, 2), juce::Justification::centred);
}

void OmniFmEditorView::paint(juce::Graphics& g)
{
    // 1. Dark Teal/Forest Green Brushed Aluminum Chassis
    g.fillAll(juce::Colour::fromRGB(22, 34, 32));

    // 2. Mint Green Accent Line
    g.setColour(juce::Colour::fromRGB(65, 225, 175));
    g.fillRect(0, 72, getWidth(), 2);

    // 3. Branding
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    g.drawText("YAMAHA OmniFM 6-OP", 24, 14, 220, 20, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(140, 190, 180));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("FREQUENCY MODULATION SYNTHESIZER", 24, 32, 260, 14, juce::Justification::centredLeft);

    // 4. Algorithm & Feedback
    const juce::Rectangle<int> algoBox {392, 22, 34, 26};
    drawAlgorithmDisplay(g, algoBox);

    g.setColour(juce::Colour::fromRGB(35, 55, 50));
    g.fillRoundedRectangle(m_btnAlgoPrev.toFloat(), 3.0f);
    g.drawRoundedRectangle(m_btnAlgoPrev.toFloat(), 3.0f, 1.0f);
    g.fillRoundedRectangle(m_btnAlgoNext.toFloat(), 3.0f);
    g.drawRoundedRectangle(m_btnAlgoNext.toFloat(), 3.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    g.drawText("<", m_btnAlgoPrev, juce::Justification::centred);
    g.drawText(">", m_btnAlgoNext, juce::Justification::centred);

    const float fbVal = m_node != nullptr ? m_node->patch().masterFeedback : 0.25f;
    drawKnob(g, m_knobFeedback, "FEEDBACK", fbVal, m_dragParamIdx == 99);

    // 5. Preset Display Ribbon
    const juce::Rectangle<int> presetBox {getWidth() - 260, 20, 240, 28};
    g.setColour(juce::Colour::fromRGB(12, 22, 20));
    g.fillRoundedRectangle(presetBox.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(55, 105, 95));
    g.drawRoundedRectangle(presetBox.toFloat(), 4.0f, 1.0f);

    const std::string prName = m_node != nullptr ? m_node->presetName(m_currentPreset) : "Solid DX7 Electric Piano";
    g.setColour(juce::Colour::fromRGB(65, 240, 185));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("PRESET " + std::to_string(m_currentPreset + 1) + ": " + prName, presetBox.reduced(8, 2), juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(35, 55, 50));
    g.fillRoundedRectangle(m_btnPresetPrev.toFloat(), 3.0f);
    g.fillRoundedRectangle(m_btnPresetNext.toFloat(), 3.0f);
    g.setColour(juce::Colours::white);
    g.drawText("<", m_btnPresetPrev, juce::Justification::centred);
    g.drawText(">", m_btnPresetNext, juce::Justification::centred);

    // 6. 6 Operator Columns
    const auto patch = m_node != nullptr ? m_node->patch() : engine::OmniFmPatch {};

    for (std::size_t i = 0; i < 6; ++i)
    {
        const auto& opGui = m_opGuis[i];
        const auto& opCfg = patch.operators[i];

        // Operator background column box
        g.setColour(juce::Colour::fromRGB(26, 42, 39));
        g.fillRoundedRectangle(opGui.box.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(45, 75, 68));
        g.drawRoundedRectangle(opGui.box.toFloat(), 4.0f, 1.0f);

        // Power toggle button
        g.setColour(opCfg.enabled ? juce::Colour::fromRGB(45, 160, 125) : juce::Colour::fromRGB(35, 48, 46));
        g.fillRoundedRectangle(opGui.btnPower.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
        g.drawText("OP " + std::to_string(i + 1), opGui.btnPower, juce::Justification::centred);

        // Waveform button
        constexpr std::array<const char*, 5> waveNames = {"SIN", "TRI", "SAW", "SQR", "ABS"};
        const auto waveIdx = static_cast<std::size_t>(opCfg.waveform);
        const char* wName = waveIdx < waveNames.size() ? waveNames[waveIdx] : "SIN";

        g.setColour(juce::Colour::fromRGB(32, 52, 48));
        g.fillRoundedRectangle(opGui.btnWave.toFloat(), 3.0f);
        g.setColour(juce::Colour::fromRGB(180, 225, 215));
        g.setFont(juce::FontOptions(8.5f).withStyle("Bold"));
        g.drawText(wName, opGui.btnWave, juce::Justification::centred);

        // Operator Knobs
        const float normRatio = std::clamp(opCfg.ratio / 16.0f, 0.0f, 1.0f);
        const float normLevel = opCfg.outputLevel;
        const float normDetune = std::clamp((opCfg.fineDetuneHz + 5.0f) / 10.0f, 0.0f, 1.0f);
        const float normAtt = std::clamp(opCfg.attackRateMs / 500.0f, 0.0f, 1.0f);
        const float normDec = std::clamp(opCfg.decay1RateMs / 1000.0f, 0.0f, 1.0f);

        const bool isHot = (m_dragOpIdx == static_cast<int>(i));

        drawKnob(g, opGui.knobRatio,  "RATIO", normRatio, isHot && m_dragParamIdx == 0);
        drawKnob(g, opGui.knobLevel,  "LEVEL", normLevel, isHot && m_dragParamIdx == 1);
        drawKnob(g, opGui.knobDetune, "TUNE",  normDetune, isHot && m_dragParamIdx == 2);
        drawKnob(g, opGui.knobAttack, "ATT",   normAtt,   isHot && m_dragParamIdx == 3);
        drawKnob(g, opGui.knobDecay,  "DEC",   normDec,   isHot && m_dragParamIdx == 4);
    }
}

void OmniFmEditorView::mouseDown(const juce::MouseEvent& e)
{
    // Preset buttons
    if (m_btnPresetPrev.contains(e.getPosition()) && m_node != nullptr)
    {
        if (m_currentPreset > 0) m_currentPreset--;
        else m_currentPreset = m_node->presetCount() - 1;
        m_node->loadPreset(m_currentPreset);
        repaint();
        return;
    }
    if (m_btnPresetNext.contains(e.getPosition()) && m_node != nullptr)
    {
        m_currentPreset = (m_currentPreset + 1) % m_node->presetCount();
        m_node->loadPreset(m_currentPreset);
        repaint();
        return;
    }

    // Algorithm buttons
    if (m_node != nullptr)
    {
        auto p = m_node->patch();
        if (m_btnAlgoPrev.contains(e.getPosition()))
        {
            p.algorithm = p.algorithm > 1 ? p.algorithm - 1 : 32;
            m_node->setPatch(p);
            repaint();
            return;
        }
        if (m_btnAlgoNext.contains(e.getPosition()))
        {
            p.algorithm = p.algorithm < 32 ? p.algorithm + 1 : 1;
            m_node->setPatch(p);
            repaint();
            return;
        }
    }

    // Feedback knob
    if (m_knobFeedback.contains(e.getPosition()))
    {
        m_dragOpIdx = -1;
        m_dragParamIdx = 99;
        m_dragStartY = e.y;
        m_dragStartVal = m_node != nullptr ? m_node->patch().masterFeedback : 0.25f;
        return;
    }

    // Check Operator controls
    for (std::size_t i = 0; i < 6; ++i)
    {
        auto& op = m_opGuis[i];
        if (m_node != nullptr)
        {
            auto p = m_node->patch();
            if (op.btnPower.contains(e.getPosition()))
            {
                p.operators[i].enabled = ! p.operators[i].enabled;
                m_node->setPatch(p);
                repaint();
                return;
            }
            if (op.btnWave.contains(e.getPosition()))
            {
                const auto nextWave = static_cast<engine::FmWaveform>((static_cast<std::uint8_t>(p.operators[i].waveform) + 1) % 5);
                p.operators[i].waveform = nextWave;
                m_node->setPatch(p);
                repaint();
                return;
            }
        }

        // Knob drags
        const auto checkKnob = [&](const juce::Rectangle<int>& b, int pIdx, float curVal) {
            if (b.contains(e.getPosition()))
            {
                m_dragOpIdx = static_cast<int>(i);
                m_dragParamIdx = pIdx;
                m_dragStartY = e.y;
                m_dragStartVal = curVal;
                return true;
            }
            return false;
        };

        if (m_node != nullptr)
        {
            const auto& opCfg = m_node->patch().operators[i];
            if (checkKnob(op.knobRatio, 0, opCfg.ratio / 16.0f)) return;
            if (checkKnob(op.knobLevel, 1, opCfg.outputLevel)) return;
            if (checkKnob(op.knobDetune, 2, (opCfg.fineDetuneHz + 5.0f) / 10.0f)) return;
            if (checkKnob(op.knobAttack, 3, opCfg.attackRateMs / 500.0f)) return;
            if (checkKnob(op.knobDecay, 4, opCfg.decay1RateMs / 1000.0f)) return;
        }
    }
}

void OmniFmEditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_node == nullptr) return;

    const int deltaY = m_dragStartY - e.y;
    const float normDelta = static_cast<float>(deltaY) * 0.007f;
    const float newVal = std::clamp(m_dragStartVal + normDelta, 0.0f, 1.0f);

    auto p = m_node->patch();

    if (m_dragParamIdx == 99)
    {
        p.masterFeedback = newVal;
        m_node->setPatch(p);
        repaint();
        return;
    }

    if (m_dragOpIdx >= 0 && m_dragOpIdx < 6)
    {
        auto& opCfg = p.operators[static_cast<std::size_t>(m_dragOpIdx)];
        if (m_dragParamIdx == 0)      opCfg.ratio = std::max(0.25f, std::round(newVal * 16.0f * 2.0f) * 0.5f);
        else if (m_dragParamIdx == 1) opCfg.outputLevel = newVal;
        else if (m_dragParamIdx == 2) opCfg.fineDetuneHz = -5.0f + newVal * 10.0f;
        else if (m_dragParamIdx == 3) opCfg.attackRateMs = 0.5f + newVal * 500.0f;
        else if (m_dragParamIdx == 4) opCfg.decay1RateMs = 5.0f + newVal * 1000.0f;

        m_node->setPatch(p);
        repaint();
    }
}

void OmniFmEditorView::mouseUp(const juce::MouseEvent&)
{
    m_dragOpIdx = -1;
    m_dragParamIdx = -1;
    repaint();
}

// ============================================================================
// OmniFmWindow Implementation
// ============================================================================

OmniFmWindow::OmniFmWindow(const juce::String& name, engine::OmniFmSynthNode* node)
    : DocumentWindow(name, juce::Colour::fromRGB(22, 34, 32), DocumentWindow::closeButton),
      m_view(node)
{
    if (node == nullptr)
    {
        m_ownedNode = std::make_unique<engine::OmniFmSynthNode>();
        m_ownedNode->prepare(48000.0, 512, 2);
        m_view.setSynthNode(m_ownedNode.get());
    }

    setContentNonOwned(&m_view, true);
    setResizable(false, false);
    centreWithSize(920, 380);
    setVisible(true);
}

OmniFmWindow::~OmniFmWindow() = default;

void OmniFmWindow::closeButtonPressed()
{
    close();
}

void OmniFmWindow::open(engine::OmniFmSynthNode* node)
{
    if (s_instance == nullptr)
    {
        s_instance = std::make_unique<OmniFmWindow>("Yamaha OmniFM 6-Operator Synthesizer", node);
    }
    else if (node != nullptr)
    {
        s_instance->m_view.setSynthNode(node);
    }
    s_instance->setVisible(true);
    s_instance->toFront(true);
}

void OmniFmWindow::close()
{
    if (s_instance)
    {
        s_instance->setVisible(false);
    }
}

bool OmniFmWindow::isOpen()
{
    return s_instance != nullptr && s_instance->isVisible();
}

} // namespace dawg::ui
