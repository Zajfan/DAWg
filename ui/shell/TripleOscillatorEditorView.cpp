#include "TripleOscillatorEditorView.h"

#include <cmath>
#include <numbers>

namespace dawg::ui {

namespace {

constexpr int kWidth = 860;
constexpr int kHeight = 420;

struct ThreeOscPreset
{
    std::string name;
    int osc1Wave;
    int osc2Wave;
    float osc2Coarse;
    float osc2Fine;
    int osc2Mod;
    int osc3Wave;
    float osc3Coarse;
    float osc3Fine;
    int osc3Mod;
    float cutoff;
    float reso;
    int filterType;
};

const std::array<ThreeOscPreset, 5> s_presets {{
    {"Classic Trance Lead", 2, 2, 0.0f, 12.0f, 0, 3, -12.0f, -8.0f, 0, 6500.0f, 1.8f, 0},
    {"Deep Sub Bass", 0, 0, -12.0f, 0.0f, 0, 3, -24.0f, 0.0f, 0, 850.0f, 1.2f, 0},
    {"Screaming Hardstyle Saw", 2, 2, 7.0f, 25.0f, 2, 2, -12.0f, 0.0f, 0, 12000.0f, 3.2f, 0},
    {"FM Bell / Chime", 0, 0, 12.0f, 0.0f, 2, 0, 24.0f, 4.0f, 1, 9500.0f, 1.5f, 2},
    {"80s Retro PWM Pad", 3, 3, 0.0f, 15.0f, 0, 1, -12.0f, 0.0f, 0, 3200.0f, 2.0f, 0}
}};

} // namespace

TripleOscillatorEditorView::TripleOscillatorEditorView(engine::dsp::TripleOscillatorSynthNode* node)
    : m_node(node)
{
    setSize(kWidth, kHeight);

    // Osc 1 Knobs: Coarse, Fine, Pan, Vol
    m_knobs.push_back({"Coarse", 0.0f, -24.0f, 24.0f, "st", {}, juce::Colour::fromRGB(0, 220, 240)});
    m_knobs.push_back({"Fine", 0.0f, -100.0f, 100.0f, "cents", {}, juce::Colour::fromRGB(0, 220, 240)});
    m_knobs.push_back({"Pan", 0.0f, -1.0f, 1.0f, "%", {}, juce::Colour::fromRGB(0, 220, 240)});
    m_knobs.push_back({"Vol", 1.0f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(0, 220, 240)});

    // Osc 2 Knobs: Coarse, Fine, Pan, Vol
    m_knobs.push_back({"Coarse", 0.0f, -24.0f, 24.0f, "st", {}, juce::Colour::fromRGB(255, 170, 40)});
    m_knobs.push_back({"Fine", 12.0f, -100.0f, 100.0f, "cents", {}, juce::Colour::fromRGB(255, 170, 40)});
    m_knobs.push_back({"Pan", -0.3f, -1.0f, 1.0f, "%", {}, juce::Colour::fromRGB(255, 170, 40)});
    m_knobs.push_back({"Vol", 0.8f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(255, 170, 40)});

    // Osc 3 Knobs: Coarse, Fine, Pan, Vol
    m_knobs.push_back({"Coarse", -12.0f, -24.0f, 24.0f, "st", {}, juce::Colour::fromRGB(160, 110, 255)});
    m_knobs.push_back({"Fine", -8.0f, -100.0f, 100.0f, "cents", {}, juce::Colour::fromRGB(160, 110, 255)});
    m_knobs.push_back({"Pan", 0.3f, -1.0f, 1.0f, "%", {}, juce::Colour::fromRGB(160, 110, 255)});
    m_knobs.push_back({"Vol", 0.6f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(160, 110, 255)});

    // Master Filter Knobs: Cutoff, Reso
    m_knobs.push_back({"Cutoff", 4500.0f, 20.0f, 20000.0f, "Hz", {}, juce::Colour::fromRGB(0, 220, 240)});
    m_knobs.push_back({"Reso", 1.5f, 0.1f, 10.0f, "Q", {}, juce::Colour::fromRGB(0, 220, 240)});

    // ADSR Knobs: Attack, Decay, Sustain, Release
    m_knobs.push_back({"Attack", 5.0f, 1.0f, 2000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Decay", 250.0f, 5.0f, 5000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Sustain", 0.8f, 0.0f, 1.0f, "%", {}, juce::Colour::fromRGB(220, 225, 235)});
    m_knobs.push_back({"Release", 300.0f, 5.0f, 5000.0f, "ms", {}, juce::Colour::fromRGB(220, 225, 235)});

    startTimerHz(25);
}

TripleOscillatorEditorView::~TripleOscillatorEditorView()
{
    stopTimer();
}

void TripleOscillatorEditorView::timerCallback()
{
    if (m_node != nullptr)
    {
        // Osc 1
        auto cfg1 = m_node->oscConfig(0);
        cfg1.coarsePitchSemitones = static_cast<int>(m_knobs[0].value);
        cfg1.finePitchCents = m_knobs[1].value;
        cfg1.pan = m_knobs[2].value;
        cfg1.volume = m_knobs[3].value;
        m_node->setOscConfig(0, cfg1);

        // Osc 2
        auto cfg2 = m_node->oscConfig(1);
        cfg2.coarsePitchSemitones = static_cast<int>(m_knobs[4].value);
        cfg2.finePitchCents = m_knobs[5].value;
        cfg2.pan = m_knobs[6].value;
        cfg2.volume = m_knobs[7].value;
        m_node->setOscConfig(1, cfg2);

        // Osc 3
        auto cfg3 = m_node->oscConfig(2);
        cfg3.coarsePitchSemitones = static_cast<int>(m_knobs[8].value);
        cfg3.finePitchCents = m_knobs[9].value;
        cfg3.pan = m_knobs[10].value;
        cfg3.volume = m_knobs[11].value;
        m_node->setOscConfig(2, cfg3);

        m_node->setFilterCutoff(m_knobs[12].value);
        m_node->setFilterResonance(m_knobs[13].value);
        m_node->setAmpAdsr(m_knobs[14].value, m_knobs[15].value, m_knobs[16].value, m_knobs[17].value);
    }
}

void TripleOscillatorEditorView::resized()
{
    m_btnPresetPrev = juce::Rectangle<int>(200, 8, 22, 18);
    m_btnPresetNext = juce::Rectangle<int>(390, 8, 22, 18);

    const int stripH = 88;
    const int startY = 36;
    const int stripW = getWidth() - 260;

    for (int o = 0; o < 3; ++o)
    {
        const int y = startY + o * stripH;
        // Wave buttons
        for (int w = 0; w < 5; ++w)
        {
            m_waveButtons[static_cast<std::size_t>(o)][static_cast<std::size_t>(w)].bounds =
                juce::Rectangle<int>(20 + w * 28, y + 26, 24, 24);
        }

        // Knobs
        const int knobX = 180;
        const int baseKnobIdx = o * 4;
        m_knobs[static_cast<std::size_t>(baseKnobIdx)].bounds = juce::Rectangle<int>(knobX, y + 14, 52, 52);
        m_knobs[static_cast<std::size_t>(baseKnobIdx + 1)].bounds = juce::Rectangle<int>(knobX + 65, y + 14, 52, 52);
        m_knobs[static_cast<std::size_t>(baseKnobIdx + 2)].bounds = juce::Rectangle<int>(knobX + 130, y + 14, 52, 52);
        m_knobs[static_cast<std::size_t>(baseKnobIdx + 3)].bounds = juce::Rectangle<int>(knobX + 195, y + 14, 52, 52);

        // AM/FM buttons
        if (o == 1)
        {
            m_modButtonsOsc2[0] = juce::Rectangle<int>(knobX + 270, y + 26, 32, 22);
            m_modButtonsOsc2[1] = juce::Rectangle<int>(knobX + 306, y + 26, 32, 22);
        }
        else if (o == 2)
        {
            m_modButtonsOsc3[0] = juce::Rectangle<int>(knobX + 270, y + 26, 32, 22);
            m_modButtonsOsc3[1] = juce::Rectangle<int>(knobX + 306, y + 26, 32, 22);
        }
    }

    // Right Master & Filter Section
    const int rightX = stripW + 20;
    m_knobs[12].bounds = juce::Rectangle<int>(rightX + 15, startY + 25, 55, 55); // Cutoff
    m_knobs[13].bounds = juce::Rectangle<int>(rightX + 85, startY + 25, 55, 55); // Reso

    for (int f = 0; f < 4; ++f)
    {
        m_filterTypeButtons[static_cast<std::size_t>(f)] = juce::Rectangle<int>(rightX + 15 + f * 34, startY + 95, 30, 20);
    }

    // ADSR
    const int adsrY = startY + 135;
    m_knobs[14].bounds = juce::Rectangle<int>(rightX + 10, adsrY, 48, 48);
    m_knobs[15].bounds = juce::Rectangle<int>(rightX + 65, adsrY, 48, 48);
    m_knobs[16].bounds = juce::Rectangle<int>(rightX + 120, adsrY, 48, 48);
    m_knobs[17].bounds = juce::Rectangle<int>(rightX + 175, adsrY, 48, 48);
}

void TripleOscillatorEditorView::paint(juce::Graphics& g)
{
    // Dark modern FL Studio styled panel
    g.fillAll(juce::Colour::fromRGB(24, 26, 31));

    // Top title bar
    auto topBar = juce::Rectangle<int>(0, 0, getWidth(), 32);
    g.setColour(juce::Colour::fromRGB(18, 19, 23));
    g.fillRect(topBar);

    g.setColour(juce::Colour::fromRGB(0, 220, 240));
    g.setFont(juce::FontOptions {13.0f}.withStyle("Bold"));
    g.drawText("3xOsc STUDIO EDITION", 15, 0, 180, 32, juce::Justification::centredLeft);

    // Preset selector
    g.setColour(juce::Colour::fromRGB(36, 40, 48));
    g.fillRoundedRectangle(m_btnPresetPrev.toFloat(), 3.0f);
    g.fillRoundedRectangle(m_btnPresetNext.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(200, 205, 220));
    g.drawText("<", m_btnPresetPrev, juce::Justification::centred);
    g.drawText(">", m_btnPresetNext, juce::Justification::centred);

    const auto presetArea = juce::Rectangle<int>(m_btnPresetPrev.getRight() + 4, m_btnPresetPrev.getY(), m_btnPresetNext.getX() - m_btnPresetPrev.getRight() - 8, 18);
    g.setColour(juce::Colour::fromRGB(28, 30, 36));
    g.fillRoundedRectangle(presetArea.toFloat(), 3.0f);
    g.setColour(juce::Colour::fromRGB(255, 170, 40));
    g.setFont(juce::FontOptions {11.0f}.withStyle("Bold"));
    g.drawText(s_presets[m_currentPreset].name, presetArea, juce::Justification::centred);

    // Draw Oscillator Strips
    const int stripH = 88;
    const int startY = 36;
    const int stripW = getWidth() - 260;

    for (int o = 0; o < 3; ++o)
    {
        drawOscStrip(g, o, juce::Rectangle<int>(10, startY + o * stripH, stripW, stripH - 4));
    }

    // Right Section: Filter & Envelopes
    const auto rightBounds = juce::Rectangle<int>(stripW + 15, startY, getWidth() - stripW - 25, getHeight() - startY - 12);
    g.setColour(juce::Colour::fromRGB(30, 33, 40));
    g.fillRoundedRectangle(rightBounds.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 50, 60));
    g.drawRoundedRectangle(rightBounds.toFloat(), 4.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(180, 185, 200));
    g.setFont(juce::FontOptions {11.0f}.withStyle("Bold"));
    g.drawText("STATE-VARIABLE FILTER", rightBounds.getX() + 10, rightBounds.getY() + 6, rightBounds.getWidth() - 20, 16, juce::Justification::left);

    // Filter Type buttons
    const std::array<std::string, 4> fTypes = {"LP", "HP", "BP", "NOTCH"};
    const int curFiltType = m_node ? static_cast<int>(m_node->filterType()) : 0;
    for (int f = 0; f < 4; ++f)
    {
        const auto b = m_filterTypeButtons[static_cast<std::size_t>(f)];
        const bool active = (f == curFiltType);
        g.setColour(active ? juce::Colour::fromRGB(0, 220, 240) : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(b.toFloat(), 2.0f);
        g.setColour(active ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(190, 195, 210));
        g.setFont(juce::FontOptions {9.0f}.withStyle("Bold"));
        g.drawText(fTypes[static_cast<std::size_t>(f)], b, juce::Justification::centred);
    }

    // Knobs
    for (std::size_t i = 0; i < m_knobs.size(); ++i)
    {
        drawKnob(g, m_knobs[i], false, static_cast<int>(i) == m_activeKnobIdx);
    }
}

void TripleOscillatorEditorView::drawOscStrip(juce::Graphics& g, int oscIdx, juce::Rectangle<int> bounds)
{
    g.setColour(juce::Colour::fromRGB(30, 33, 40));
    g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(45, 50, 60));
    g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.0f);

    // Strip Header
    const std::array<std::string, 3> titles = {"OSCILLATOR 1", "OSCILLATOR 2", "OSCILLATOR 3"};
    const std::array<juce::Colour, 3> stripColours = {
        juce::Colour::fromRGB(0, 220, 240),
        juce::Colour::fromRGB(255, 170, 40),
        juce::Colour::fromRGB(160, 110, 255)
    };

    g.setColour(stripColours[static_cast<std::size_t>(oscIdx)]);
    g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
    g.drawText(titles[static_cast<std::size_t>(oscIdx)], bounds.getX() + 10, bounds.getY() + 6, 120, 14, juce::Justification::left);

    // Wave buttons
    const int curWave = m_node ? static_cast<int>(m_node->oscConfig(static_cast<std::size_t>(oscIdx)).waveform) : 2;
    const std::array<std::string, 5> waveIcons = {"~", "/\\", "|\\", "[_]", "*"};
    for (int w = 0; w < 5; ++w)
    {
        const auto b = m_waveButtons[static_cast<std::size_t>(oscIdx)][static_cast<std::size_t>(w)].bounds;
        const bool active = (w == curWave);
        g.setColour(active ? stripColours[static_cast<std::size_t>(oscIdx)] : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(b.toFloat(), 2.0f);
        g.setColour(active ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(190, 195, 210));
        g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
        g.drawText(waveIcons[static_cast<std::size_t>(w)], b, juce::Justification::centred);
    }

    // AM/FM buttons
    if (oscIdx == 1)
    {
        const auto modMode = m_node ? m_node->oscConfig(1).modMode : engine::dsp::TripleOscModulation::mix;
        g.setColour(modMode == engine::dsp::TripleOscModulation::am ? juce::Colour::fromRGB(255, 170, 40) : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(m_modButtonsOsc2[0].toFloat(), 2.0f);
        g.setColour(modMode == engine::dsp::TripleOscModulation::am ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(180, 185, 200));
        g.setFont(juce::FontOptions {9.0f}.withStyle("Bold"));
        g.drawText("AM", m_modButtonsOsc2[0], juce::Justification::centred);

        g.setColour(modMode == engine::dsp::TripleOscModulation::fm ? juce::Colour::fromRGB(255, 170, 40) : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(m_modButtonsOsc2[1].toFloat(), 2.0f);
        g.setColour(modMode == engine::dsp::TripleOscModulation::fm ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(180, 185, 200));
        g.drawText("FM", m_modButtonsOsc2[1], juce::Justification::centred);
    }
    else if (oscIdx == 2)
    {
        const auto modMode = m_node ? m_node->oscConfig(2).modMode : engine::dsp::TripleOscModulation::mix;
        g.setColour(modMode == engine::dsp::TripleOscModulation::am ? juce::Colour::fromRGB(160, 110, 255) : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(m_modButtonsOsc3[0].toFloat(), 2.0f);
        g.setColour(modMode == engine::dsp::TripleOscModulation::am ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(180, 185, 200));
        g.setFont(juce::FontOptions {9.0f}.withStyle("Bold"));
        g.drawText("AM", m_modButtonsOsc3[0], juce::Justification::centred);

        g.setColour(modMode == engine::dsp::TripleOscModulation::fm ? juce::Colour::fromRGB(160, 110, 255) : juce::Colour::fromRGB(38, 42, 50));
        g.fillRoundedRectangle(m_modButtonsOsc3[1].toFloat(), 2.0f);
        g.setColour(modMode == engine::dsp::TripleOscModulation::fm ? juce::Colour::fromRGB(15, 16, 20) : juce::Colour::fromRGB(180, 185, 200));
        g.drawText("FM", m_modButtonsOsc3[1], juce::Justification::centred);
    }
}

void TripleOscillatorEditorView::drawKnob(juce::Graphics& g, const ModernKnob& knob, bool /*isHovered*/, bool isActive)
{
    const auto b = knob.bounds;
    const auto centre = b.getCentre().toFloat();
    const float radius = static_cast<float>(std::min(b.getWidth(), b.getHeight())) * 0.40f;

    // Track arc background
    g.setColour(juce::Colour::fromRGB(38, 41, 48));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 2.5f);

    // Active arc fill
    const float norm = (knob.value - knob.minVal) / std::max(1e-4f, knob.maxVal - knob.minVal);
    constexpr float minAngle = -2.4f;
    constexpr float maxAngle = 2.4f;
    const float angle = minAngle + norm * (maxAngle - minAngle);

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, minAngle, angle, true);
    g.setColour(knob.trackColour);
    g.strokePath(arc, juce::PathStrokeType(2.5f));

    // Inner cap
    const float innerRadius = radius * 0.70f;
    g.setColour(isActive ? juce::Colour::fromRGB(45, 50, 60) : juce::Colour::fromRGB(26, 28, 33));
    g.fillEllipse(centre.x - innerRadius, centre.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

    // Indicator point
    const float ptX = centre.x + std::sin(angle) * (innerRadius * 0.65f);
    const float ptY = centre.y - std::cos(angle) * (innerRadius * 0.65f);
    g.setColour(juce::Colours::white);
    g.fillEllipse(ptX - 1.5f, ptY - 1.5f, 3.0f, 3.0f);

    // Label
    g.setColour(juce::Colour::fromRGB(175, 180, 195));
    g.setFont(juce::FontOptions {8.5f});
    g.drawText(knob.label, b.getX() - 8, b.getBottom() + 1, b.getWidth() + 16, 12, juce::Justification::centred);
}

void TripleOscillatorEditorView::mouseDown(const juce::MouseEvent& e)
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

    // Wave buttons
    for (std::size_t o = 0; o < 3; ++o)
    {
        for (std::size_t w = 0; w < 5; ++w)
        {
            if (m_waveButtons[o][w].bounds.contains(e.getPosition()))
            {
                if (m_node != nullptr)
                {
                    auto cfg = m_node->oscConfig(o);
                    cfg.waveform = static_cast<engine::dsp::TripleOscWaveform>(w);
                    m_node->setOscConfig(o, cfg);
                }
                repaint();
                return;
            }
        }
    }

    // Mod buttons Osc 2
    if (m_modButtonsOsc2[0].contains(e.getPosition()))
    {
        if (m_node != nullptr)
        {
            auto cfg = m_node->oscConfig(1);
            cfg.modMode = (cfg.modMode == engine::dsp::TripleOscModulation::am)
                ? engine::dsp::TripleOscModulation::mix
                : engine::dsp::TripleOscModulation::am;
            m_node->setOscConfig(1, cfg);
        }
        repaint();
        return;
    }
    if (m_modButtonsOsc2[1].contains(e.getPosition()))
    {
        if (m_node != nullptr)
        {
            auto cfg = m_node->oscConfig(1);
            cfg.modMode = (cfg.modMode == engine::dsp::TripleOscModulation::fm)
                ? engine::dsp::TripleOscModulation::mix
                : engine::dsp::TripleOscModulation::fm;
            m_node->setOscConfig(1, cfg);
        }
        repaint();
        return;
    }

    // Mod buttons Osc 3
    if (m_modButtonsOsc3[0].contains(e.getPosition()))
    {
        if (m_node != nullptr)
        {
            auto cfg = m_node->oscConfig(2);
            cfg.modMode = (cfg.modMode == engine::dsp::TripleOscModulation::am)
                ? engine::dsp::TripleOscModulation::mix
                : engine::dsp::TripleOscModulation::am;
            m_node->setOscConfig(2, cfg);
        }
        repaint();
        return;
    }
    if (m_modButtonsOsc3[1].contains(e.getPosition()))
    {
        if (m_node != nullptr)
        {
            auto cfg = m_node->oscConfig(2);
            cfg.modMode = (cfg.modMode == engine::dsp::TripleOscModulation::fm)
                ? engine::dsp::TripleOscModulation::mix
                : engine::dsp::TripleOscModulation::fm;
            m_node->setOscConfig(2, cfg);
        }
        repaint();
        return;
    }

    // Filter type buttons
    for (std::size_t f = 0; f < 4; ++f)
    {
        if (m_filterTypeButtons[f].contains(e.getPosition()))
        {
            if (m_node != nullptr)
            {
                m_node->setFilterType(static_cast<engine::dsp::TripleOscFilterType>(f));
            }
            repaint();
            return;
        }
    }

    // Knobs
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

void TripleOscillatorEditorView::mouseDrag(const juce::MouseEvent& e)
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

void TripleOscillatorEditorView::mouseUp(const juce::MouseEvent&)
{
    m_activeKnobIdx = -1;
    repaint();
}

void TripleOscillatorEditorView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
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
// TripleOscillatorWindow
// =============================================================================

std::unique_ptr<TripleOscillatorWindow> TripleOscillatorWindow::s_instance = nullptr;

void TripleOscillatorWindow::open(engine::dsp::TripleOscillatorSynthNode* node)
{
    if (s_instance != nullptr)
    {
        if (node != nullptr) s_instance->m_view.setSynthNode(node);
        s_instance->toFront(true);
        s_instance->setVisible(true);
        return;
    }
    s_instance = std::make_unique<TripleOscillatorWindow>("3xOsc Studio Edition - The No-hands Company", node);
}

void TripleOscillatorWindow::close()
{
    if (s_instance != nullptr)
        s_instance->setVisible(false);
}

bool TripleOscillatorWindow::isOpen()
{
    return s_instance != nullptr && s_instance->isVisible();
}

TripleOscillatorWindow::TripleOscillatorWindow(const juce::String& name, engine::dsp::TripleOscillatorSynthNode* node)
    : DocumentWindow(name, juce::Colour::fromRGB(22, 24, 28), DocumentWindow::allButtons)
    , m_ownedNode(node == nullptr ? std::make_unique<engine::dsp::TripleOscillatorSynthNode>() : nullptr)
    , m_view(node != nullptr ? node : m_ownedNode.get())
{
    setUsingNativeTitleBar(true);
    setContentNonOwned(&m_view, true);
    setResizable(false, false);
    centreWithSize(kWidth, kHeight);
    setVisible(true);
}

TripleOscillatorWindow::~TripleOscillatorWindow() = default;

void TripleOscillatorWindow::closeButtonPressed()
{
    setVisible(false);
}

} // namespace dawg::ui
