#include "ModelDEditorView.h"

#include <cmath>
#include <numbers>

namespace dawg::ui {

namespace {

constexpr int kPanelWidth = 920;
constexpr int kPanelHeight = 360;

struct ModelDPreset
{
    std::string name;
    float osc1Wave;
    float osc2Wave;
    float osc2Detune;
    float osc3Wave;
    float osc3Detune;
    float feedback;
    float cutoff;
    float resonance;
    float drive;
    float filtAttack;
    float filtDecay;
    float filtSustain;
    float ampAttack;
    float ampDecay;
    float ampSustain;
};

const std::array<ModelDPreset, 5> s_presets {{
    {"Fat Minimoog Bass", 1.0f, 1.0f, 0.05f, 2.0f, -12.0f, 0.35f, 480.0f, 0.65f, 8.0f, 5.0f, 200.0f, 0.1f, 3.0f, 250.0f, 0.8f},
    {"Screaming Ring Lead", 1.0f, 1.0f, 7.0f, 0.0f, 12.0f, 0.50f, 3200.0f, 0.82f, 16.0f, 12.0f, 400.0f, 0.5f, 8.0f, 300.0f, 0.9f},
    {"Warm Analog Brass", 1.0f, 1.0f, 0.08f, 1.0f, 0.0f, 0.10f, 1600.0f, 0.30f, 3.0f, 80.0f, 500.0f, 0.6f, 60.0f, 600.0f, 0.9f},
    {"Funk Slap Bass", 2.0f, 2.0f, -0.04f, 2.0f, -12.0f, 0.25f, 850.0f, 0.70f, 6.0f, 2.0f, 120.0f, 0.0f, 2.0f, 150.0f, 0.7f},
    {"Kraftwerk Zap Lead", 0.0f, 1.0f, 12.0f, 2.0f, 0.0f, 0.40f, 5500.0f, 0.88f, 12.0f, 1.0f, 90.0f, 0.0f, 1.0f, 110.0f, 0.5f}
}};

} // namespace

ModelDEditorView::ModelDEditorView(engine::dsp::AnalogLadderRingModSynthNode* node)
    : m_node(node)
{
    setSize(kPanelWidth, kPanelHeight);

    // Knobs setup:
    // Oscillator Bank
    m_knobs.push_back({"Wave 1", "OSCILLATOR 1", 1.0f, 0.0f, 4.0f, "idx", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Range 1", "OSCILLATOR 1", 2.0f, 0.0f, 5.0f, "ft", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Level 1", "OSCILLATOR 1", 0.9f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(240, 240, 245)});

    m_knobs.push_back({"Wave 2", "OSCILLATOR 2", 1.0f, 0.0f, 4.0f, "idx", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Detune 2", "OSCILLATOR 2", 0.05f, -7.0f, 7.0f, "st", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Level 2", "OSCILLATOR 2", 0.8f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(240, 240, 245)});

    m_knobs.push_back({"Wave 3", "OSCILLATOR 3", 2.0f, 0.0f, 4.0f, "idx", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Detune 3", "OSCILLATOR 3", -12.0f, -24.0f, 24.0f, "st", {}, juce::Colour::fromRGB(240, 240, 245)});
    m_knobs.push_back({"Level 3", "OSCILLATOR 3", 0.6f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(240, 240, 245)});

    // Mixer & Overdrive
    m_knobs.push_back({"Feedback Overdrive", "MIXER", 0.25f, 0.0f, 1.5f, "%", {}, juce::Colour::fromRGB(245, 140, 40)});
    m_knobs.push_back({"Ring Mod Mix", "MIXER", 0.0f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(240, 90, 70)});

    // Ladder Filter Modifiers
    m_knobs.push_back({"Cutoff", "LADDER FILTER", 650.0f, 20.0f, 20000.0f, "Hz", {}, juce::Colour::fromRGB(70, 180, 255)});
    m_knobs.push_back({"Emphasis", "LADDER FILTER", 0.65f, 0.0f, 1.0f, "Q", {}, juce::Colour::fromRGB(70, 180, 255)});
    m_knobs.push_back({"Drive Saturation", "LADDER FILTER", 8.0f, 0.0f, 24.0f, "dB", {}, juce::Colour::fromRGB(255, 160, 40)});
    m_knobs.push_back({"Filter Attack", "CONTOUR", 5.0f, 1.0f, 2000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Filter Decay", "CONTOUR", 250.0f, 5.0f, 5000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Filter Sustain", "CONTOUR", 0.2f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(220, 225, 235)});

    // Output & Loudness Contour
    m_knobs.push_back({"Amp Attack", "LOUDNESS", 3.0f, 1.0f, 2000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Amp Decay", "LOUDNESS", 300.0f, 5.0f, 5000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Amp Sustain", "LOUDNESS", 0.8f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Master Volume", "OUTPUT", 0.85f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(240, 240, 245)});

    // Rocker switches
    m_rockers.push_back({"Osc 1 On", true, {}, juce::Colour::fromRGB(60, 140, 240)});
    m_rockers.push_back({"Osc 2 On", true, {}, juce::Colour::fromRGB(60, 140, 240)});
    m_rockers.push_back({"Osc 3 On", true, {}, juce::Colour::fromRGB(60, 140, 240)});
    m_rockers.push_back({"Feedback On", true, {}, juce::Colour::fromRGB(240, 120, 40)});
    m_rockers.push_back({"A-440 Reference", false, {}, juce::Colour::fromRGB(230, 230, 235)});

    startTimerHz(25);
}

ModelDEditorView::~ModelDEditorView()
{
    stopTimer();
}

void ModelDEditorView::timerCallback()
{
    // Update node parameters if node is connected
    if (m_node != nullptr)
    {
        engine::dsp::AnalogOscConfig osc1;
        osc1.waveform = static_cast<engine::dsp::AnalogOscWaveform>(static_cast<int>(m_knobs[0].value) % 5);
        osc1.level = m_rockers[0].state ? m_knobs[2].value : 0.0f;
        m_node->setOscConfig(0, osc1);

        engine::dsp::AnalogOscConfig osc2;
        osc2.waveform = static_cast<engine::dsp::AnalogOscWaveform>(static_cast<int>(m_knobs[3].value) % 5);
        osc2.semitones = m_knobs[4].value;
        osc2.level = m_rockers[1].state ? m_knobs[5].value : 0.0f;
        m_node->setOscConfig(1, osc2);

        engine::dsp::AnalogOscConfig osc3;
        osc3.waveform = static_cast<engine::dsp::AnalogOscWaveform>(static_cast<int>(m_knobs[6].value) % 5);
        osc3.semitones = m_knobs[7].value;
        osc3.level = m_rockers[2].state ? m_knobs[8].value : 0.0f;
        m_node->setOscConfig(2, osc3);

        const float fb = m_rockers[3].state ? m_knobs[9].value : 0.0f;
        m_node->setFeedbackOverdrive(fb);
        m_node->setRingModMix(m_knobs[10].value);

        m_node->setTransistorLadder(m_knobs[11].value, m_knobs[12].value, m_knobs[13].value);
        m_node->setFilterAdsr(m_knobs[14].value, m_knobs[15].value, m_knobs[16].value, 200.0f, 0.6f);
        m_node->setAmpAdsr(m_knobs[17].value, m_knobs[18].value, m_knobs[19].value, 200.0f);
    }
}

void ModelDEditorView::resized()
{
    const int woodWidth = 26;
    const int panelLeft = woodWidth;
    const int panelWidth = getWidth() - 2 * woodWidth;

    // Header preset buttons in top bar
    m_btnPresetPrev = juce::Rectangle<int>(panelLeft + 180, 8, 24, 20);
    m_btnPresetNext = juce::Rectangle<int>(panelLeft + 360, 8, 24, 20);

    // Section areas
    const int colW = panelWidth / 5;
    const int startY = 38;

    // Col 0: Oscillator 1
    m_knobs[0].bounds = juce::Rectangle<int>(panelLeft + 10, startY + 25, 60, 60);
    m_knobs[1].bounds = juce::Rectangle<int>(panelLeft + 80, startY + 25, 60, 60);
    m_knobs[2].bounds = juce::Rectangle<int>(panelLeft + 45, startY + 115, 60, 60);
    m_rockers[0].bounds = juce::Rectangle<int>(panelLeft + 55, startY + 205, 40, 24);

    // Col 1: Oscillator 2 & 3
    const int col1X = panelLeft + colW;
    m_knobs[3].bounds = juce::Rectangle<int>(col1X + 10, startY + 25, 60, 60);
    m_knobs[4].bounds = juce::Rectangle<int>(col1X + 80, startY + 25, 60, 60);
    m_knobs[5].bounds = juce::Rectangle<int>(col1X + 45, startY + 95, 60, 60);
    m_rockers[1].bounds = juce::Rectangle<int>(col1X + 55, startY + 165, 40, 24);

    m_knobs[6].bounds = juce::Rectangle<int>(col1X + 10, startY + 205, 50, 50);
    m_knobs[7].bounds = juce::Rectangle<int>(col1X + 65, startY + 205, 50, 50);
    m_knobs[8].bounds = juce::Rectangle<int>(col1X + 120, startY + 205, 50, 50);
    m_rockers[2].bounds = juce::Rectangle<int>(col1X + 65, startY + 270, 40, 24);

    // Col 2: Mixer & Overdrive
    const int col2X = panelLeft + colW * 2;
    m_knobs[9].bounds = juce::Rectangle<int>(col2X + 25, startY + 45, 65, 65);
    m_rockers[3].bounds = juce::Rectangle<int>(col2X + 38, startY + 130, 40, 24);
    m_knobs[10].bounds = juce::Rectangle<int>(col2X + 25, startY + 185, 65, 65);

    // Col 3: Transistor Ladder Filter & Filter Contour
    const int col3X = panelLeft + colW * 3;
    m_knobs[11].bounds = juce::Rectangle<int>(col3X + 10, startY + 30, 65, 65);
    m_knobs[12].bounds = juce::Rectangle<int>(col3X + 85, startY + 30, 65, 65);
    m_knobs[13].bounds = juce::Rectangle<int>(col3X + 48, startY + 115, 65, 65);
    m_knobs[14].bounds = juce::Rectangle<int>(col3X + 10, startY + 205, 48, 48);
    m_knobs[15].bounds = juce::Rectangle<int>(col3X + 62, startY + 205, 48, 48);
    m_knobs[16].bounds = juce::Rectangle<int>(col3X + 114, startY + 205, 48, 48);

    // Col 4: Loudness & Output
    const int col4X = panelLeft + colW * 4;
    m_knobs[17].bounds = juce::Rectangle<int>(col4X + 10, startY + 35, 48, 48);
    m_knobs[18].bounds = juce::Rectangle<int>(col4X + 62, startY + 35, 48, 48);
    m_knobs[19].bounds = juce::Rectangle<int>(col4X + 114, startY + 35, 48, 48);
    m_knobs[20].bounds = juce::Rectangle<int>(col4X + 50, startY + 115, 65, 65);
    m_rockers[4].bounds = juce::Rectangle<int>(col4X + 62, startY + 205, 40, 24);
}

void ModelDEditorView::paint(juce::Graphics& g)
{
    // Background: Walnut wooden chassis
    g.fillAll(juce::Colour::fromRGB(45, 24, 12));

    const int woodWidth = 26;
    // Left walnut cheek
    g.setGradientFill(juce::ColourGradient(
        juce::Colour::fromRGB(95, 52, 28), 0.0f, 0.0f,
        juce::Colour::fromRGB(60, 30, 16), static_cast<float>(woodWidth), 0.0f, false));
    g.fillRect(0, 0, woodWidth, getHeight());

    // Right walnut cheek
    g.setGradientFill(juce::ColourGradient(
        juce::Colour::fromRGB(60, 30, 16), static_cast<float>(getWidth() - woodWidth), 0.0f,
        juce::Colour::fromRGB(95, 52, 28), static_cast<float>(getWidth()), 0.0f, false));
    g.fillRect(getWidth() - woodWidth, 0, woodWidth, getHeight());

    // Central tipped-up matte-black control panel
    auto panelBounds = juce::Rectangle<int>(woodWidth, 4, getWidth() - 2 * woodWidth, getHeight() - 8);
    g.setColour(juce::Colour::fromRGB(22, 24, 27));
    g.fillRect(panelBounds);

    // Panel hairline border
    g.setColour(juce::Colour::fromRGB(55, 58, 65));
    g.drawRect(panelBounds, 1);

    // Top metal branding bar
    auto topBar = panelBounds.removeFromTop(32);
    g.setColour(juce::Colour::fromRGB(16, 17, 20));
    g.fillRect(topBar);

    g.setColour(juce::Colour::fromRGB(230, 235, 245));
    g.setFont(juce::FontOptions {13.0f}.withStyle("Bold"));
    const auto brandArea = topBar.removeFromLeft(170).reduced(12, 0);
    g.drawText("MINIMOOG MODEL D", brandArea, juce::Justification::centredLeft);

    // Preset selector bar
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.fillRoundedRectangle(m_btnPresetPrev.toFloat(), 3.0f);
    g.fillRoundedRectangle(m_btnPresetNext.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(200, 205, 220));
    g.drawText("<", m_btnPresetPrev, juce::Justification::centred);
    g.drawText(">", m_btnPresetNext, juce::Justification::centred);

    const auto presetArea = juce::Rectangle<int>(m_btnPresetPrev.getRight() + 4, m_btnPresetPrev.getY(), m_btnPresetNext.getX() - m_btnPresetPrev.getRight() - 8, 20);
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRoundedRectangle(presetArea.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(0, 220, 255));
    g.setFont(juce::FontOptions {11.0f}.withStyle("Bold"));
    g.drawText(s_presets[m_currentPreset].name, presetArea, juce::Justification::centred);

    // Section dividers and labels
    const int colW = panelBounds.getWidth() / 5;
    const std::array<std::string, 5> sections = {"OSCILLATOR 1", "OSCILLATOR 2 & 3", "MIXER / FEEDBACK", "LADDER FILTER", "OUTPUT"};
    for (int c = 0; c < 5; ++c)
    {
        const auto sRect = juce::Rectangle<int>(panelBounds.getX() + c * colW, panelBounds.getY(), colW, panelBounds.getHeight());
        if (c > 0)
        {
            g.setColour(juce::Colour::fromRGB(42, 45, 52));
            g.drawVerticalLine(sRect.getX(), static_cast<float>(sRect.getY()), static_cast<float>(sRect.getBottom()));
        }
        drawSectionHeader(g, juce::Rectangle<int>(sRect.getX(), sRect.getY() + 4, sRect.getWidth(), 16), sections[static_cast<std::size_t>(c)]);
    }

    // Draw Knobs
    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        drawKnob(g, m_knobs[i], false, static_cast<int>(i) == m_activeKnobIdx);
    }

    // Draw Rockers
    for (const auto& rocker : m_rockers)
    {
        drawRocker(g, rocker);
    }
}

void ModelDEditorView::drawSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& title)
{
    g.setColour(juce::Colour::fromRGB(150, 155, 170));
    g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
    g.drawText(title, bounds, juce::Justification::centred, false);
}

void ModelDEditorView::drawKnob(juce::Graphics& g, const VintageKnob& knob, bool /*isHovered*/, bool isActive)
{
    const auto b = knob.bounds;
    const auto centre = b.getCentre().toFloat();
    const float radius = static_cast<float>(std::min(b.getWidth(), b.getHeight())) * 0.45f;

    // Fluted vintage skirt
    g.setColour(juce::Colour::fromRGB(15, 16, 18));
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Outer fluted ring
    g.setColour(juce::Colour::fromRGB(38, 41, 48));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Inner cap
    const float innerRadius = radius * 0.72f;
    g.setColour(isActive ? juce::Colour::fromRGB(45, 50, 60) : juce::Colour::fromRGB(26, 28, 33));
    g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

    // Indicator pointer line
    const float norm = (knob.value - knob.minVal) / std::max(1e-4f, knob.maxVal - knob.minVal);
    constexpr float minAngle = -2.4f;
    constexpr float maxAngle = 2.4f;
    const float angle = minAngle + norm * (maxAngle - minAngle);

    const float ptX = centre.x + std::sin(angle) * innerRadius;
    const float ptY = centre.y - std::cos(angle) * innerRadius;

    g.setColour(knob.capColour);
    g.drawLine(centre.x, centre.y, ptX, ptY, 2.0f);

    // Label
    g.setColour(juce::Colour::fromRGB(190, 195, 210));
    g.setFont(juce::FontOptions {9.0f});
    g.drawText(knob.label, b.getX() - 10, b.getBottom() + 1, b.getWidth() + 20, 14, juce::Justification::centred);
}

void ModelDEditorView::drawRocker(juce::Graphics& g, const RockerSwitch& rocker)
{
    auto b = rocker.bounds;
    g.setColour(juce::Colour::fromRGB(18, 19, 22));
    g.fillRoundedRectangle(b.toFloat(), 2.0f);
    g.setColour(juce::Colour::fromRGB(50, 53, 62));
    g.drawRoundedRectangle(b.toFloat(), 2.0f, 1.0f);

    // Rocker toggle position
    const auto half = rocker.state ? b.removeFromTop(b.getHeight() / 2) : b.removeFromBottom(b.getHeight() / 2);
    g.setColour(rocker.state ? rocker.colour : juce::Colour::fromRGB(38, 42, 50));
    g.fillRoundedRectangle(half.toFloat(), 2.0f);

    g.setColour(juce::Colour::fromRGB(170, 175, 190));
    g.setFont(juce::FontOptions {8.0f});
    g.drawText(rocker.label, rocker.bounds.getX() - 15, rocker.bounds.getBottom() + 2, rocker.bounds.getWidth() + 30, 12, juce::Justification::centred);
}

void ModelDEditorView::mouseDown(const juce::MouseEvent& e)
{
    if (m_btnPresetPrev.contains(e.getPosition()))
    {
        if (m_currentPreset > 0) --m_currentPreset;
        else m_currentPreset = s_presets.size() - 1;
        repaint();
        return;
    }
    if (m_btnPresetNext.contains(e.getPosition()))
    {
        m_currentPreset = (m_currentPreset + 1) % s_presets.size();
        repaint();
        return;
    }

    for (auto& rocker : m_rockers)
    {
        if (rocker.bounds.contains(e.getPosition()))
        {
            rocker.state = ! rocker.state;
            repaint();
            return;
        }
    }

    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        if (m_knobs[i].bounds.contains(e.getPosition()))
        {
            m_activeKnobIdx = static_cast<int>(i);
            m_dragStartY = e.y;
            m_dragStartVal = m_knobs[i].value;
            repaint();
            return;
        }
    }
}

void ModelDEditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_activeKnobIdx >= 0 && static_cast<std::size_t>(m_activeKnobIdx) < m_knobs.size())
    {
        auto& knob = m_knobs[static_cast<std::size_t>(m_activeKnobIdx)];
        const float range = knob.maxVal - knob.minVal;
        const float delta = static_cast<float>(m_dragStartY - e.y) / 150.0f;
        knob.value = std::clamp(m_dragStartVal + delta * range, knob.minVal, knob.maxVal);
        repaint();
    }
}

void ModelDEditorView::mouseUp(const juce::MouseEvent&)
{
    m_activeKnobIdx = -1;
    repaint();
}

void ModelDEditorView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    for (auto& knob : m_knobs)
    {
        if (knob.bounds.contains(e.getPosition()))
        {
            const float range = knob.maxVal - knob.minVal;
            knob.value = std::clamp(knob.value + wheel.deltaY * range * 0.05f, knob.minVal, knob.maxVal);
            repaint();
            return;
        }
    }
}

// =============================================================================
// ModelDWindow
// =============================================================================

std::unique_ptr<ModelDWindow> ModelDWindow::s_instance = nullptr;

void ModelDWindow::open(engine::dsp::AnalogLadderRingModSynthNode* node)
{
    if (s_instance != nullptr)
    {
        if (node != nullptr) s_instance->m_view.setSynthNode(node);
        s_instance->toFront(true);
        s_instance->setVisible(true);
        return;
    }
    s_instance = std::make_unique<ModelDWindow>("Moog Model D Monophonic Studio Synth", node);
}

void ModelDWindow::close()
{
    if (s_instance != nullptr)
        s_instance->setVisible(false);
}

bool ModelDWindow::isOpen()
{
    return s_instance != nullptr && s_instance->isVisible();
}

ModelDWindow::ModelDWindow(const juce::String& name, engine::dsp::AnalogLadderRingModSynthNode* node)
    : DocumentWindow(name, juce::Colour::fromRGB(22, 24, 28), DocumentWindow::allButtons)
    , m_ownedNode(node == nullptr ? std::make_unique<engine::dsp::AnalogLadderRingModSynthNode>() : nullptr)
    , m_view(node != nullptr ? node : m_ownedNode.get())
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_view, true);
    setResizable(false, false);
    centreWithSize(kPanelWidth, kPanelHeight);
    setVisible(true);
}

ModelDWindow::~ModelDWindow() = default;

void ModelDWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace dawg::ui
