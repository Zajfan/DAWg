#include "Juno106EditorView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

std::unique_ptr<Juno106Window> Juno106Window::s_instance = nullptr;

Juno106EditorView::Juno106EditorView(engine::Juno106SynthNode* node)
    : m_node(node)
{
    // Define the iconic Roland Juno-106 faders
    const juce::Colour colOrange = juce::Colour::fromRGB(240, 95, 45);
    const juce::Colour colBlue   = juce::Colour::fromRGB(45, 140, 235);
    const juce::Colour colGrey   = juce::Colour::fromRGB(210, 215, 225);
    const juce::Colour colRed    = juce::Colour::fromRGB(230, 45, 55);

    m_faders = {
        // LFO
        {"lfo_rate",  "RATE",  "LFO", 0.40f, {}, colGrey},
        {"lfo_delay", "DELAY", "LFO", 0.15f, {}, colGrey},

        // DCO
        {"dco_lfo",   "LFO",   "DCO", 0.00f, {}, colGrey},
        {"dco_pwm",   "PWM",   "DCO", 0.60f, {}, colOrange},
        {"dco_sub",   "SUB",   "DCO", 0.35f, {}, colGrey},
        {"dco_noise", "NOISE", "DCO", 0.00f, {}, colGrey},

        // VCF
        {"vcf_freq",  "FREQ",  "VCF", 0.65f, {}, colOrange},
        {"vcf_res",   "RES",   "VCF", 0.35f, {}, colOrange},
        {"vcf_env",   "ENV",   "VCF", 0.55f, {}, colOrange},
        {"vcf_lfo",   "LFO",   "VCF", 0.10f, {}, colGrey},
        {"vcf_kybd",  "KYBD",  "VCF", 0.70f, {}, colBlue},

        // VCA
        {"vca_level", "LEVEL", "VCA", 0.85f, {}, colGrey},

        // ENV
        {"env_a",     "A",     "ENV", 0.20f, {}, colRed},
        {"env_d",     "D",     "ENV", 0.45f, {}, colRed},
        {"env_s",     "S",     "ENV", 0.80f, {}, colRed},
        {"env_r",     "R",     "ENV", 0.50f, {}, colRed}
    };

    setSize(920, 360);
    startTimerHz(30);
}

Juno106EditorView::~Juno106EditorView()
{
    stopTimer();
}

void Juno106EditorView::timerCallback()
{
    repaint();
}

void Juno106EditorView::resized()
{
    // Layout sections horizontally across 920px
    const int topY = 74;
    const int faderH = 180;
    const int faderW = 32;

    int curX = 24;

    // LFO section (2 faders)
    m_faders[0].bounds = {curX, topY, faderW, faderH}; curX += 40;
    m_faders[1].bounds = {curX, topY, faderW, faderH}; curX += 54;

    // DCO section (Range buttons + 4 faders)
    m_btnRange16 = {curX, topY, 26, 22};
    m_btnRange8  = {curX, topY + 28, 26, 22};
    m_btnRange4  = {curX, topY + 56, 26, 22};
    m_btnPulse   = {curX, topY + 96, 26, 24};
    m_btnSaw     = {curX, topY + 128, 26, 24};
    curX += 34;

    m_faders[2].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[3].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[4].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[5].bounds = {curX, topY, faderW, faderH}; curX += 52;

    // HPF buttons (4 buttons)
    m_btnHpf0 = {curX, topY, 28, 24};
    m_btnHpf1 = {curX, topY + 32, 28, 24};
    m_btnHpf2 = {curX, topY + 64, 28, 24};
    m_btnHpf3 = {curX, topY + 96, 28, 24};
    curX += 44;

    // VCF section (5 faders)
    m_faders[6].bounds  = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[7].bounds  = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[8].bounds  = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[9].bounds  = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[10].bounds = {curX, topY, faderW, faderH}; curX += 52;

    // VCA (1 fader)
    m_faders[11].bounds = {curX, topY, faderW, faderH}; curX += 52;

    // ENV (4 faders)
    m_faders[12].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[13].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[14].bounds = {curX, topY, faderW, faderH}; curX += 38;
    m_faders[15].bounds = {curX, topY, faderW, faderH}; curX += 52;

    // CHORUS buttons
    m_btnChorusOff  = {curX, topY + 12, 42, 26};
    m_btnChorusI    = {curX, topY + 46, 42, 26};
    m_btnChorusII   = {curX, topY + 80, 42, 26};
    m_btnChorusBoth = {curX, topY + 114, 42, 26};

    // Preset selector buttons
    m_btnPresetPrev = {getWidth() - 170, 18, 30, 24};
    m_btnPresetNext = {getWidth() - 50, 18, 30, 24};
}

void Juno106EditorView::drawFader(juce::Graphics& g, const FaderDef& fader, const bool isHot)
{
    const auto b = fader.bounds;

    // 1. Label above
    g.setColour(juce::Colour::fromRGB(220, 225, 235));
    g.setFont(juce::FontOptions(9.5f).withStyle("Bold"));
    g.drawText(fader.label, b.getX() - 6, b.getY() - 18, b.getWidth() + 12, 14, juce::Justification::centred);

    // 2. Track slot
    const int slotX = b.getCentreX() - 2;
    const int slotY = b.getY() + 4;
    const int slotH = b.getHeight() - 8;

    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRoundedRectangle(static_cast<float>(slotX), static_cast<float>(slotY), 4.0f, static_cast<float>(slotH), 2.0f);
    g.setColour(juce::Colour::fromRGB(45, 48, 55));
    g.drawRoundedRectangle(static_cast<float>(slotX), static_cast<float>(slotY), 4.0f, static_cast<float>(slotH), 2.0f, 1.0f);

    // 3. Tick marks (0 to 10)
    g.setColour(juce::Colour::fromRGB(110, 115, 130));
    for (int t = 0; t <= 10; t += 2)
    {
        const float ty = static_cast<float>(slotY + slotH) - (static_cast<float>(t) / 10.0f) * static_cast<float>(slotH);
        g.drawHorizontalLine(static_cast<int>(ty), static_cast<float>(slotX - 6), static_cast<float>(slotX - 2));
        g.drawHorizontalLine(static_cast<int>(ty), static_cast<float>(slotX + 6), static_cast<float>(slotX + 10));
    }

    // 4. Slider Cap
    const float capY = static_cast<float>(slotY + slotH) - fader.value * static_cast<float>(slotH) - 10.0f;
    const juce::Rectangle<float> capBounds {static_cast<float>(b.getX() + 2), capY, static_cast<float>(b.getWidth() - 4), 20.0f};

    // Body
    g.setColour(juce::Colour::fromRGB(25, 28, 35));
    g.fillRoundedRectangle(capBounds, 3.0f);
    g.setColour(isHot ? juce::Colours::white : juce::Colour::fromRGB(80, 85, 95));
    g.drawRoundedRectangle(capBounds, 3.0f, 1.0f);

    // Colored indicator bar in middle of cap
    g.setColour(fader.capColour);
    g.fillRect(capBounds.getX() + 3.0f, capBounds.getCentreY() - 1.5f, capBounds.getWidth() - 6.0f, 3.0f);
}

void Juno106EditorView::drawButton(juce::Graphics& g, const juce::Rectangle<int>& bounds, const std::string& text, const bool active)
{
    g.setColour(active ? juce::Colour::fromRGB(235, 75, 45) : juce::Colour::fromRGB(35, 38, 48));
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

    g.setColour(active ? juce::Colour::fromRGB(255, 190, 160) : juce::Colour::fromRGB(80, 85, 100));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);

    // LED lamp above button if active
    if (active)
    {
        g.setColour(juce::Colour::fromRGB(255, 60, 40));
        g.fillEllipse(bounds.getCentreX() - 3.0f, static_cast<float>(bounds.getY() - 6), 6.0f, 6.0f);
    }

    g.setColour(active ? juce::Colours::white : juce::Colour::fromRGB(180, 185, 200));
    g.setFont(juce::FontOptions(9.0f).withStyle("Bold"));
    g.drawText(text, bounds, juce::Justification::centred);
}

void Juno106EditorView::paint(juce::Graphics& g)
{
    // 1. Dark matte aluminum chassis
    g.fillAll(juce::Colour::fromRGB(20, 22, 26));

    // 2. Roland Juno signature accent stripe ribbons
    // Orange/Red top stripe
    g.setColour(juce::Colour::fromRGB(235, 75, 45));
    g.fillRect(0, 48, getWidth(), 4);

    // Blue secondary stripe
    g.setColour(juce::Colour::fromRGB(45, 130, 225));
    g.fillRect(0, 53, getWidth(), 2);

    // 3. Top branding ribbon
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    g.drawText("ROLAND JUNO-106", 24, 12, 220, 20, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(150, 155, 170));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("PROGRAMMABLE POLYPHONIC SYNTHESIZER", 24, 30, 320, 14, juce::Justification::centredLeft);

    // Preset readout box
    const juce::Rectangle<int> presetBox {getWidth() - 260, 16, 240, 26};
    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRoundedRectangle(presetBox.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(65, 70, 82));
    g.drawRoundedRectangle(presetBox.toFloat(), 4.0f, 1.0f);

    const std::string prName = m_node != nullptr ? m_node->presetName(m_currentPreset) : "Juno Lush Strings";
    g.setColour(juce::Colour::fromRGB(255, 170, 60));
    g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
    g.drawText("BANK " + std::to_string(m_currentPreset + 1) + ": " + prName, presetBox.reduced(6, 2), juce::Justification::centredLeft);

    drawButton(g, m_btnPresetPrev, "<", false);
    drawButton(g, m_btnPresetNext, ">", false);

    // 4. Section Dividers & Headers
    constexpr std::array<const char*, 7> sections = {
        "LFO", "DCO", "HPF", "VCF", "VCA", "ENV", "CHORUS"
    };
    constexpr std::array<int, 7> secX = {
        24, 118, 326, 370, 604, 656, 856
    };
    constexpr std::array<int, 7> secW = {
        84, 198, 38, 224, 42, 190, 52
    };

    for (std::size_t s = 0; s < sections.size(); ++s)
    {
        const juce::Rectangle<int> secBox {secX[s] - 6, 62, secW[s], getHeight() - 80};
        g.setColour(juce::Colour::fromRGB(28, 30, 38));
        g.fillRoundedRectangle(secBox.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(45, 48, 58));
        g.drawRoundedRectangle(secBox.toFloat(), 4.0f, 1.0f);

        // Header label
        g.setColour(juce::Colour::fromRGB(240, 95, 45));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(sections[s], secBox.getX(), secBox.getY() - 14, secBox.getWidth(), 12, juce::Justification::centred);
    }

    // 5. Draw Faders
    for (std::size_t i = 0; i < m_faders.size(); ++i)
    {
        drawFader(g, m_faders[i], m_activeFaderIdx == static_cast<int>(i));
    }

    // 6. Draw DCO & HPF Buttons
    const auto patch = m_node != nullptr ? m_node->patch() : engine::Juno106Patch {};

    drawButton(g, m_btnRange16, "16'", patch.dcoRange == 0);
    drawButton(g, m_btnRange8,  "8'",  patch.dcoRange == 1);
    drawButton(g, m_btnRange4,  "4'",  patch.dcoRange == 2);

    drawButton(g, m_btnPulse, "PULSE", patch.pulseEnabled);
    drawButton(g, m_btnSaw,   "SAW",   patch.sawEnabled);

    drawButton(g, m_btnHpf0, "0", patch.hpfSetting == 0);
    drawButton(g, m_btnHpf1, "1", patch.hpfSetting == 1);
    drawButton(g, m_btnHpf2, "2", patch.hpfSetting == 2);
    drawButton(g, m_btnHpf3, "3", patch.hpfSetting == 3);

    // 7. Draw Chorus Buttons
    drawButton(g, m_btnChorusOff,  "OFF",  patch.chorusMode == engine::JunoChorusMode::off);
    drawButton(g, m_btnChorusI,    "I",    patch.chorusMode == engine::JunoChorusMode::chorusI);
    drawButton(g, m_btnChorusII,   "II",   patch.chorusMode == engine::JunoChorusMode::chorusII);
    drawButton(g, m_btnChorusBoth, "I+II", patch.chorusMode == engine::JunoChorusMode::chorusIandII);
}

void Juno106EditorView::mouseDown(const juce::MouseEvent& e)
{
    // Check buttons
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

    if (m_node != nullptr)
    {
        auto patch = m_node->patch();
        if (m_btnRange16.contains(e.getPosition())) { patch.dcoRange = 0; m_node->setPatch(patch); }
        else if (m_btnRange8.contains(e.getPosition()))  { patch.dcoRange = 1; m_node->setPatch(patch); }
        else if (m_btnRange4.contains(e.getPosition()))  { patch.dcoRange = 2; m_node->setPatch(patch); }
        else if (m_btnPulse.contains(e.getPosition()))   { patch.pulseEnabled = ! patch.pulseEnabled; m_node->setPatch(patch); }
        else if (m_btnSaw.contains(e.getPosition()))     { patch.sawEnabled = ! patch.sawEnabled; m_node->setPatch(patch); }
        else if (m_btnHpf0.contains(e.getPosition()))    { patch.hpfSetting = 0; m_node->setPatch(patch); }
        else if (m_btnHpf1.contains(e.getPosition()))    { patch.hpfSetting = 1; m_node->setPatch(patch); }
        else if (m_btnHpf2.contains(e.getPosition()))    { patch.hpfSetting = 2; m_node->setPatch(patch); }
        else if (m_btnHpf3.contains(e.getPosition()))    { patch.hpfSetting = 3; m_node->setPatch(patch); }
        else if (m_btnChorusOff.contains(e.getPosition()))  { m_node->setChorusMode(engine::JunoChorusMode::off); }
        else if (m_btnChorusI.contains(e.getPosition()))    { m_node->setChorusMode(engine::JunoChorusMode::chorusI); }
        else if (m_btnChorusII.contains(e.getPosition()))   { m_node->setChorusMode(engine::JunoChorusMode::chorusII); }
        else if (m_btnChorusBoth.contains(e.getPosition())) { m_node->setChorusMode(engine::JunoChorusMode::chorusIandII); }
    }

    // Check faders
    for (std::size_t i = 0; i < m_faders.size(); ++i)
    {
        if (m_faders[i].bounds.contains(e.getPosition()))
        {
            m_activeFaderIdx = static_cast<int>(i);
            m_dragStartY = e.y;
            m_dragStartVal = m_faders[i].value;
            repaint();
            return;
        }
    }
}

void Juno106EditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_activeFaderIdx >= 0 && m_activeFaderIdx < static_cast<int>(m_faders.size()))
    {
        const int deltaY = m_dragStartY - e.y;
        const float newVal = std::clamp(m_dragStartVal + static_cast<float>(deltaY) * 0.007f, 0.0f, 1.0f);
        m_faders[static_cast<std::size_t>(m_activeFaderIdx)].value = newVal;

        if (m_node != nullptr)
        {
            auto p = m_node->patch();
            const auto& id = m_faders[static_cast<std::size_t>(m_activeFaderIdx)].id;

            if (id == "vcf_freq")  p.vcfCutoff = 20.0f * std::pow(1000.0f, newVal);
            else if (id == "vcf_res")   p.vcfResonance = newVal;
            else if (id == "vcf_env")   p.vcfEnvAmount = newVal;
            else if (id == "dco_pwm")   p.pwmAmount = newVal;
            else if (id == "dco_sub")   p.subLevel = newVal;
            else if (id == "dco_noise") p.noiseLevel = newVal;
            else if (id == "lfo_rate")  p.lfoRateHz = 0.1f + newVal * 20.0f;
            else if (id == "env_a")     p.attackMs = 1.0f + newVal * 2000.0f;
            else if (id == "env_d")     p.decayMs = 10.0f + newVal * 3000.0f;
            else if (id == "env_s")     p.sustainLevel = newVal;
            else if (id == "env_r")     p.releaseMs = 10.0f + newVal * 4000.0f;

            m_node->setPatch(p);
        }
        repaint();
    }
}

void Juno106EditorView::mouseUp(const juce::MouseEvent&)
{
    m_activeFaderIdx = -1;
    repaint();
}

// ============================================================================
// Juno106Window Implementation
// ============================================================================

Juno106Window::Juno106Window(const juce::String& name, engine::Juno106SynthNode* node)
    : DocumentWindow(name, juce::Colour::fromRGB(20, 22, 26), DocumentWindow::closeButton),
      m_view(node)
{
    if (node == nullptr)
    {
        m_ownedNode = std::make_unique<engine::Juno106SynthNode>();
        m_ownedNode->prepare(48000.0, 512, 2);
        m_view.setSynthNode(m_ownedNode.get());
    }

    setContentNonOwned(&m_view, true);
    setResizable(false, false);
    centreWithSize(920, 360);
    setVisible(true);
}

Juno106Window::~Juno106Window() = default;

void Juno106Window::closeButtonPressed()
{
    close();
}

void Juno106Window::open(engine::Juno106SynthNode* node)
{
    if (s_instance == nullptr)
    {
        s_instance = std::make_unique<Juno106Window>("Roland Juno-106 Synthesizer", node);
    }
    else if (node != nullptr)
    {
        s_instance->m_view.setSynthNode(node);
    }
    s_instance->setVisible(true);
    s_instance->toFront(true);
}

void Juno106Window::close()
{
    if (s_instance)
    {
        s_instance->setVisible(false);
    }
}

bool Juno106Window::isOpen()
{
    return s_instance != nullptr && s_instance->isVisible();
}

} // namespace dawg::ui
