#include "DspWireGridView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::ui {

DspWireGridView::DspWireGridView()
{
    // Presets Combo
    m_presetCombo.addItem("Preset: Acid Bass 303", 1);
    m_presetCombo.addItem("Preset: Ambient Drone", 2);
    m_presetCombo.addItem("Preset: West Coast Wavefolder", 3);
    m_presetCombo.addItem("Preset: Percussion Synth", 4);
    m_presetCombo.setSelectedId(1, juce::dontSendNotification);
    m_presetCombo.onChange = [this] {
        const int id = m_presetCombo.getSelectedId();
        if (id == 1) m_grid.loadPresetAcidBass();
        else if (id == 2) m_grid.loadPresetAmbientDrone();
        else if (id == 3) m_grid.loadPresetWestCoastWavefolder();
        else if (id == 4) m_grid.loadPresetPercussionSynth();
        repaint();
    };
    addAndMakeVisible(m_presetCombo);

    // Module Add Buttons
    auto setupBtn = [this](juce::TextButton& btn, auto onClick) {
        btn.onClick = onClick;
        addAndMakeVisible(btn);
    };

    setupBtn(m_addOscButton, [this] {
        m_grid.addModule(engine::dsp::GridModuleType::oscillator, 150.0f, 150.0f);
        repaint();
    });
    setupBtn(m_addFilterButton, [this] {
        m_grid.addModule(engine::dsp::GridModuleType::svfFilter, 280.0f, 150.0f);
        repaint();
    });
    setupBtn(m_addSatButton, [this] {
        m_grid.addModule(engine::dsp::GridModuleType::mathSaturation, 420.0f, 150.0f);
        repaint();
    });
    setupBtn(m_addLfoButton, [this] {
        m_grid.addModule(engine::dsp::GridModuleType::lfo, 150.0f, 320.0f);
        repaint();
    });
    setupBtn(m_addDelayButton, [this] {
        m_grid.addModule(engine::dsp::GridModuleType::delay, 320.0f, 320.0f);
        repaint();
    });
    setupBtn(m_clearButton, [this] {
        // Clear all except audio output
        while (m_grid.modules().size() > 1)
        {
            m_grid.removeModule(m_grid.modules().front().id);
        }
        repaint();
    });

    // Macros
    for (std::size_t i = 0; i < 4; ++i)
    {
        m_macroSliders[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        m_macroSliders[i].setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        m_macroSliders[i].setRange(0.0, 1.0, 0.01);
        m_macroSliders[i].setValue(0.5, juce::dontSendNotification);
        m_macroSliders[i].onValueChange = [this, i] {
            m_grid.setMacro(i, static_cast<float>(m_macroSliders[i].getValue()));
        };
        addAndMakeVisible(m_macroSliders[i]);

        m_macroLabels[i].setText("M" + std::to_string(i + 1), juce::dontSendNotification);
        m_macroLabels[i].setFont(juce::FontOptions(10.0f));
        m_macroLabels[i].setJustificationType(juce::Justification::centred);
        m_macroLabels[i].setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 200, 220));
        addAndMakeVisible(m_macroLabels[i]);
    }

    startTimerHz(30); // 30 FPS pulse animation and oscilloscope probe update
}

void DspWireGridView::timerCallback()
{
    m_animationPhase += 0.05f;
    if (m_animationPhase >= 1.0f)
    {
        m_animationPhase -= 1.0f;
    }

    // Refresh oscilloscope probe data
    m_grid.getProbeData(m_probeSamples.data(), m_probeSamples.size());
    repaint();
}

void DspWireGridView::setViewModel([[maybe_unused]] const EditorViewModel& model)
{
}

void DspWireGridView::resized()
{
    auto area = getLocalBounds();
    auto topBar = area.removeFromTop(36).reduced(4, 2);

    m_presetCombo.setBounds(topBar.removeFromLeft(200).reduced(2));
    m_addOscButton.setBounds(topBar.removeFromLeft(60).reduced(2));
    m_addFilterButton.setBounds(topBar.removeFromLeft(65).reduced(2));
    m_addSatButton.setBounds(topBar.removeFromLeft(65).reduced(2));
    m_addLfoButton.setBounds(topBar.removeFromLeft(60).reduced(2));
    m_addDelayButton.setBounds(topBar.removeFromLeft(65).reduced(2));
    m_clearButton.setBounds(topBar.removeFromLeft(55).reduced(2));

    auto macroArea = topBar.removeFromRight(200);
    const int macroW = macroArea.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        auto mCol = macroArea.removeFromLeft(macroW);
        m_macroLabels[static_cast<std::size_t>(i)].setBounds(mCol.removeFromTop(12));
        m_macroSliders[static_cast<std::size_t>(i)].setBounds(mCol);
    }
}

juce::Point<float> DspWireGridView::getPortPosition(std::uint32_t moduleId, bool isOutput, std::uint8_t portIdx) const noexcept
{
    for (const auto& mod : m_grid.modules())
    {
        if (mod.id == moduleId)
        {
            constexpr float modW = 160.0f;
            const float x = isOutput ? (mod.posX + modW - 12.0f) : (mod.posX + 12.0f);
            const float y = mod.posY + 45.0f + static_cast<float>(portIdx) * 22.0f;
            return { x, y };
        }
    }
    return { 0.0f, 0.0f };
}

DspWireGridView::PortHitResult DspWireGridView::findPortAt(juce::Point<float> pos) const noexcept
{
    constexpr float hitRadius = 12.0f;
    for (const auto& mod : m_grid.modules())
    {
        // Check inputs
        for (std::size_t i = 0; i < mod.inputs.size(); ++i)
        {
            const auto p = getPortPosition(mod.id, false, static_cast<std::uint8_t>(i));
            if (pos.getDistanceFrom(p) <= hitRadius)
            {
                return { true, mod.id, false, static_cast<std::uint8_t>(i), p };
            }
        }
        // Check outputs
        for (std::size_t i = 0; i < mod.outputs.size(); ++i)
        {
            const auto p = getPortPosition(mod.id, true, static_cast<std::uint8_t>(i));
            if (pos.getDistanceFrom(p) <= hitRadius)
            {
                return { true, mod.id, true, static_cast<std::uint8_t>(i), p };
            }
        }
    }
    return {};
}

int DspWireGridView::findModuleAt(juce::Point<float> pos) const noexcept
{
    constexpr float modW = 160.0f;
    constexpr float modH = 140.0f;

    for (int i = static_cast<int>(m_grid.modules().size()) - 1; i >= 0; --i)
    {
        const auto& mod = m_grid.modules()[static_cast<std::size_t>(i)];
        if (pos.x >= mod.posX && pos.x <= mod.posX + modW &&
            pos.y >= mod.posY && pos.y <= mod.posY + modH)
        {
            return i;
        }
    }
    return -1;
}

void DspWireGridView::mouseDown(const juce::MouseEvent& e)
{
    const auto mousePos = e.position;

    // Check if clicked on a port
    const auto portHit = findPortAt(mousePos);
    if (portHit.hit)
    {
        if (portHit.isOutput)
        {
            m_draggingCable = true;
            m_cableSrcModuleId = portHit.moduleId;
            m_cableSrcPort = portHit.portIdx;
            m_cableDragCurrentPos = mousePos;
        }
        return;
    }

    // Check if clicked on a module
    const int modIdx = findModuleAt(mousePos);
    if (modIdx >= 0)
    {
        m_draggedModuleIdx = modIdx;
        const auto& mod = m_grid.modules()[static_cast<std::size_t>(modIdx)];
        m_moduleDragOffset = { mousePos.x - mod.posX, mousePos.y - mod.posY };
        return;
    }

    // Cable selection for oscilloscope probe
    for (const auto& cable : m_grid.cables())
    {
        const auto p1 = getPortPosition(cable.srcModuleId, true, cable.srcPort);
        const auto p2 = getPortPosition(cable.dstModuleId, false, cable.dstPort);
        const auto midPoint = (p1 + p2) * 0.5f;
        if (mousePos.getDistanceFrom(midPoint) <= 20.0f)
        {
            m_grid.setActiveProbeCable(cable.id);
            repaint();
            return;
        }
    }
}

void DspWireGridView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_draggingCable)
    {
        m_cableDragCurrentPos = e.position;
        repaint();
    }
    else if (m_draggedModuleIdx >= 0 && m_draggedModuleIdx < static_cast<int>(m_grid.modules().size()))
    {
        // Update module position (const_cast is safe as modules() vector is internal)
        auto& mod = const_cast<engine::dsp::GridModule&>(m_grid.modules()[static_cast<std::size_t>(m_draggedModuleIdx)]);
        mod.posX = std::max(10.0f, e.position.x - m_moduleDragOffset.x);
        mod.posY = std::max(45.0f, e.position.y - m_moduleDragOffset.y);
        repaint();
    }
}

void DspWireGridView::mouseUp(const juce::MouseEvent& e)
{
    if (m_draggingCable)
    {
        const auto portHit = findPortAt(e.position);
        if (portHit.hit && ! portHit.isOutput && portHit.moduleId != m_cableSrcModuleId)
        {
            // Successfully connected!
            m_grid.connectCable(m_cableSrcModuleId, m_cableSrcPort, portHit.moduleId, portHit.portIdx);
        }
        m_draggingCable = false;
        repaint();
    }
    m_draggedModuleIdx = -1;
}

void DspWireGridView::paint(juce::Graphics& g)
{
    // Background Grid
    g.fillAll(juce::Colour::fromRGB(18, 20, 24));

    g.setColour(juce::Colour::fromRGB(28, 32, 40));
    for (int x = 0; x < getWidth(); x += 20)
    {
        g.drawVerticalLine(x, 36.0f, static_cast<float>(getHeight()));
    }
    for (int y = 36; y < getHeight(); y += 20)
    {
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }

    // Draw Patch Cables
    for (const auto& cable : m_grid.cables())
    {
        const auto p1 = getPortPosition(cable.srcModuleId, true, cable.srcPort);
        const auto p2 = getPortPosition(cable.dstModuleId, false, cable.dstPort);

        const float dx = std::max(40.0f, std::abs(p2.x - p1.x) * 0.5f);
        juce::Path cablePath;
        cablePath.startNewSubPath(p1);
        cablePath.cubicTo(p1.x + dx, p1.y, p2.x - dx, p2.y, p2.x, p2.y);

        // Cable shadow
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.strokePath(cablePath, juce::PathStrokeType(5.0f));

        // Active probe highlight
        const bool isProbed = (cable.id == m_grid.activeProbeCable());
        const auto cableCol = isProbed ? juce::Colour::fromRGB(255, 220, 50) : juce::Colour(cable.colorRgba);

        g.setColour(cableCol);
        g.strokePath(cablePath, juce::PathStrokeType(isProbed ? 3.5f : 2.5f));

        // Animated signal pulse traveling along the wire
        const float pulseT = std::fmod(m_animationPhase + static_cast<float>(cable.id) * 0.2f, 1.0f);
        const auto pulsePos = cablePath.getPointAlongPath(cablePath.getLength() * pulseT);
        g.setColour(juce::Colours::white);
        g.fillEllipse(pulsePos.x - 3.5f, pulsePos.y - 3.5f, 7.0f, 7.0f);
    }

    // In-progress dragged cable
    if (m_draggingCable)
    {
        const auto p1 = getPortPosition(m_cableSrcModuleId, true, m_cableSrcPort);
        const auto p2 = m_cableDragCurrentPos;
        const float dx = std::max(40.0f, std::abs(p2.x - p1.x) * 0.5f);

        juce::Path dragPath;
        dragPath.startNewSubPath(p1);
        dragPath.cubicTo(p1.x + dx, p1.y, p2.x - dx, p2.y, p2.x, p2.y);

        g.setColour(juce::Colour::fromRGB(0, 240, 255).withAlpha(0.8f));
        g.strokePath(dragPath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Draw Modular Blocks
    constexpr float modW = 160.0f;
    constexpr float modH = 140.0f;

    for (const auto& mod : m_grid.modules())
    {
        juce::Rectangle<float> bounds(mod.posX, mod.posY, modW, modH);

        // Drop shadow & Chassis
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds.translated(2.0f, 3.0f), 8.0f);

        g.setColour(juce::Colour::fromRGB(26, 30, 38));
        g.fillRoundedRectangle(bounds, 8.0f);

        // Header accent bar
        juce::Colour headerCol;
        switch (mod.type)
        {
        case engine::dsp::GridModuleType::oscillator: headerCol = juce::Colour::fromRGB(0, 180, 240); break;
        case engine::dsp::GridModuleType::noise: headerCol = juce::Colour::fromRGB(160, 170, 190); break;
        case engine::dsp::GridModuleType::svfFilter: headerCol = juce::Colour::fromRGB(240, 140, 30); break;
        case engine::dsp::GridModuleType::mathSaturation: headerCol = juce::Colour::fromRGB(230, 60, 90); break;
        case engine::dsp::GridModuleType::lfo: headerCol = juce::Colour::fromRGB(180, 80, 220); break;
        case engine::dsp::GridModuleType::envelope: headerCol = juce::Colour::fromRGB(50, 200, 120); break;
        case engine::dsp::GridModuleType::delay: headerCol = juce::Colour::fromRGB(40, 210, 200); break;
        case engine::dsp::GridModuleType::audioOut: headerCol = juce::Colour::fromRGB(250, 210, 50); break;
        }

        auto headerBounds = bounds.removeFromTop(24.0f);
        g.setColour(headerCol.withAlpha(0.2f));
        g.fillRoundedRectangle(headerBounds, 8.0f);
        g.setColour(headerCol);
        g.fillRect(headerBounds.removeFromBottom(2.0f));

        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(mod.name, headerBounds.toNearestInt(), juce::Justification::centred, true);

        // Border outline
        g.setColour(juce::Colour::fromRGB(45, 52, 65));
        g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

        // Draw Input Ports (Left)
        for (std::size_t i = 0; i < mod.inputs.size(); ++i)
        {
            const auto p = getPortPosition(mod.id, false, static_cast<std::uint8_t>(i));
            // Jack bezel
            g.setColour(juce::Colour::fromRGB(15, 18, 22));
            g.fillEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
            g.setColour(juce::Colour::fromRGB(80, 95, 115));
            g.drawEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f, 1.5f);
            // Hole
            g.setColour(juce::Colours::black);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

            // Label
            g.setColour(juce::Colour::fromRGB(160, 180, 200));
            g.setFont(juce::FontOptions(9.0f));
            g.drawText(mod.inputs[i].name, static_cast<int>(p.x + 10.0f), static_cast<int>(p.y - 8.0f), 55, 16, juce::Justification::left, false);
        }

        // Draw Output Ports (Right)
        for (std::size_t i = 0; i < mod.outputs.size(); ++i)
        {
            const auto p = getPortPosition(mod.id, true, static_cast<std::uint8_t>(i));
            // Jack bezel
            g.setColour(juce::Colour::fromRGB(15, 18, 22));
            g.fillEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f);
            g.setColour(headerCol);
            g.drawEllipse(p.x - 6.0f, p.y - 6.0f, 12.0f, 12.0f, 1.5f);
            // Hole
            g.setColour(juce::Colours::black);
            g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

            // Label
            g.setColour(juce::Colour::fromRGB(160, 180, 200));
            g.setFont(juce::FontOptions(9.0f));
            g.drawText(mod.outputs[i].name, static_cast<int>(p.x - 65.0f), static_cast<int>(p.y - 8.0f), 55, 16, juce::Justification::right, false);
        }

        // Parameter Value Readout Bar
        const auto valBounds = juce::Rectangle<float>(mod.posX + 15.0f, mod.posY + modH - 22.0f, modW - 30.0f, 12.0f);
        g.setColour(juce::Colour::fromRGB(20, 24, 30));
        g.fillRoundedRectangle(valBounds, 3.0f);
        g.setColour(headerCol.withAlpha(0.6f));
        g.fillRoundedRectangle(valBounds.withWidth(valBounds.getWidth() * std::clamp(mod.params[0], 0.0f, 1.0f)), 3.0f);
    }

    // Live Oscilloscope Probe HUD Overlay
    auto probeArea = juce::Rectangle<int>(getWidth() - 170, getHeight() - 95, 160, 85);
    g.setColour(juce::Colour::fromRGB(15, 18, 24).withAlpha(0.9f));
    g.fillRoundedRectangle(probeArea.toFloat(), 6.0f);
    g.setColour(juce::Colour::fromRGB(45, 55, 70));
    g.drawRoundedRectangle(probeArea.toFloat(), 6.0f, 1.0f);

    g.setColour(juce::Colour::fromRGB(0, 240, 255));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("PROBE OSCILLOSCOPE", probeArea.removeFromTop(16), juce::Justification::centred, false);

    // Waveform Trace
    juce::Path wavePath;
    const float centerY = static_cast<float>(probeArea.getCentreY());
    const float waveH = static_cast<float>(probeArea.getHeight()) * 0.45f;
    const auto stepX = static_cast<float>(probeArea.getWidth()) / static_cast<float>(m_probeSamples.size());

    for (std::size_t i = 0; i < m_probeSamples.size(); ++i)
    {
        const float x = static_cast<float>(probeArea.getX()) + static_cast<float>(i) * stepX;
        const float y = centerY - std::clamp(m_probeSamples[i], -1.0f, 1.0f) * waveH;
        if (i == 0) wavePath.startNewSubPath(x, y);
        else wavePath.lineTo(x, y);
    }

    g.setColour(juce::Colour::fromRGB(0, 240, 255));
    g.strokePath(wavePath, juce::PathStrokeType(1.5f));
}

} // namespace dawg::ui
