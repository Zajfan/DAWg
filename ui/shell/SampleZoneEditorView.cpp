#include "SampleZoneEditorView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {

SampleZoneEditorView::SampleZoneEditorView(AuditionCallback auditionCb)
    : m_auditionCb(std::move(auditionCb))
{
    // Default factory zones (Acoustic Piano & Strings multisample layout)
    engine::dsp::MultiSampleRegion r1 {};
    r1.rootNote = 48; // C3
    r1.lowNote = 36;  // C2
    r1.highNote = 59; // B3
    r1.lowVel = 1;
    r1.highVel = 64;
    r1.articulationName = "Piano Soft";
    r1.sampleDataL.resize(48000, 0.1f);
    m_regions.push_back(r1);

    engine::dsp::MultiSampleRegion r2 {};
    r2.rootNote = 48;
    r2.lowNote = 36;
    r2.highNote = 59;
    r2.lowVel = 65;
    r2.highVel = 127;
    r2.articulationName = "Piano Forte";
    r2.sampleDataL.resize(48000, 0.2f);
    m_regions.push_back(r2);

    engine::dsp::MultiSampleRegion r3 {};
    r3.rootNote = 72; // C5
    r3.lowNote = 60;  // C4
    r3.highNote = 84; // C6
    r3.lowVel = 1;
    r3.highVel = 64;
    r3.articulationName = "Piano Soft";
    r3.sampleDataL.resize(48000, 0.1f);
    m_regions.push_back(r3);

    engine::dsp::MultiSampleRegion r4 {};
    r4.rootNote = 72;
    r4.lowNote = 60;
    r4.highNote = 84;
    r4.lowVel = 65;
    r4.highVel = 127;
    r4.articulationName = "Piano Forte";
    r4.sampleDataL.resize(48000, 0.2f);
    m_regions.push_back(r4);

    m_addZoneButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 120, 75));
    m_addZoneButton.onClick = [this] {
        engine::dsp::MultiSampleRegion nr {};
        nr.rootNote = 60;
        nr.lowNote = 48;
        nr.highNote = 72;
        nr.lowVel = 1;
        nr.highVel = 127;
        nr.articulationName = "Custom Zone";
        nr.sampleDataL.resize(24000, 0.1f);
        m_regions.push_back(nr);
        m_selectedZoneIdx = static_cast<int>(m_regions.size()) - 1;
        repaint();
    };
    addAndMakeVisible(m_addZoneButton);

    m_removeZoneButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(140, 50, 50));
    m_removeZoneButton.onClick = [this] {
        if (m_selectedZoneIdx >= 0 && m_selectedZoneIdx < static_cast<int>(m_regions.size())) {
            m_regions.erase(m_regions.begin() + m_selectedZoneIdx);
            m_selectedZoneIdx = -1;
            repaint();
        }
    };
    addAndMakeVisible(m_removeZoneButton);

    m_loadSfzButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 90, 140));
    m_loadSfzButton.onClick = [this] {
        juce::FileChooser chooser("Select SFZ Instrument File", {}, "*.sfz");
        chooser.launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& fc) {
                const auto file = fc.getResult();
                if (file.existsAsFile()) {
                    engine::dsp::MultiSampleInstrumentNode inst;
                    if (inst.loadFromSfzString(file.loadFileAsString().toStdString())) {
                        m_regions = inst.regions();
                        m_selectedZoneIdx = -1;
                        repaint();
                    }
                }
            });
    };
    addAndMakeVisible(m_loadSfzButton);

    m_articulationCombo.addItem("All Articulations", 1);
    m_articulationCombo.addItem("KeySwitch C0 (24): Sustain", 2);
    m_articulationCombo.addItem("KeySwitch D0 (26): Staccato", 3);
    m_articulationCombo.addItem("KeySwitch E0 (28): Pizzicato", 4);
    m_articulationCombo.setSelectedId(1);
    m_articulationCombo.onChange = [this] { repaint(); };
    addAndMakeVisible(m_articulationCombo);

    m_infoLabel.setFont(juce::FontOptions(11.0f));
    m_infoLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(180, 195, 215));
    m_infoLabel.setText("Click key to audition. Drag zones to adjust velocity/key splits.", juce::dontSendNotification);
    addAndMakeVisible(m_infoLabel);
}

void SampleZoneEditorView::addSampleZone(engine::dsp::MultiSampleRegion region)
{
    m_regions.push_back(std::move(region));
    repaint();
}

void SampleZoneEditorView::clearZones() noexcept
{
    m_regions.clear();
    m_selectedZoneIdx = -1;
    repaint();
}

juce::Rectangle<int> SampleZoneEditorView::zoneToRect(const engine::dsp::MultiSampleRegion& reg, juce::Rectangle<int> canvas) const noexcept
{
    const int x1 = noteToX(reg.lowNote, canvas);
    const int x2 = noteToX(static_cast<std::uint8_t>(std::min<int>(127, reg.highNote + 1)), canvas);
    const int y1 = velToY(reg.highVel, canvas);
    const int y2 = velToY(reg.lowVel, canvas);

    return juce::Rectangle<int>(x1, y1, std::max(2, x2 - x1), std::max(2, y2 - y1));
}

int SampleZoneEditorView::noteToX(std::uint8_t note, juce::Rectangle<int> canvas) const noexcept
{
    const float norm = static_cast<float>(note) / 128.0f;
    return canvas.getX() + static_cast<int>(norm * static_cast<float>(canvas.getWidth()));
}

std::uint8_t SampleZoneEditorView::xToNote(int x, juce::Rectangle<int> canvas) const noexcept
{
    if (canvas.getWidth() <= 0) return 60;
    const float norm = std::clamp(static_cast<float>(x - canvas.getX()) / static_cast<float>(canvas.getWidth()), 0.0f, 0.999f);
    return static_cast<std::uint8_t>(norm * 128.0f);
}

int SampleZoneEditorView::velToY(std::uint8_t vel, juce::Rectangle<int> canvas) const noexcept
{
    const float norm = 1.0f - (static_cast<float>(vel) / 127.0f);
    return canvas.getY() + static_cast<int>(norm * static_cast<float>(canvas.getHeight()));
}

std::uint8_t SampleZoneEditorView::yToVel(int y, juce::Rectangle<int> canvas) const noexcept
{
    if (canvas.getHeight() <= 0) return 100;
    const float norm = std::clamp(1.0f - (static_cast<float>(y - canvas.getY()) / static_cast<float>(canvas.getHeight())), 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(norm * 127.0f), 1, 127));
}

void SampleZoneEditorView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(24, 26, 32));

    auto bounds = getLocalBounds();
    bounds.removeFromTop(m_toolbarHeight);

    auto velRuler = bounds.removeFromLeft(m_rulerWidth);
    auto keyboardArea = bounds.removeFromBottom(m_bottomKeyboardHeight);
    auto canvas = bounds;

    // Draw Velocity Ruler
    g.setColour(juce::Colour::fromRGB(32, 34, 42));
    g.fillRect(velRuler);
    g.setColour(juce::Colour::fromRGB(120, 130, 150));
    g.setFont(juce::FontOptions(9.0f));
    for (int v = 127; v >= 0; v -= 32) {
        const int y = velToY(static_cast<std::uint8_t>(v == 0 ? 1 : v), canvas);
        g.drawText(juce::String(v), velRuler.getX(), y - 6, velRuler.getWidth() - 4, 12, juce::Justification::centredRight);
    }

    // Draw Background Grid
    g.setColour(juce::Colour::fromRGB(28, 30, 38));
    g.fillRect(canvas);

    // Octave vertical lines
    for (int note = 0; note <= 128; note += 12) {
        const int x = noteToX(static_cast<std::uint8_t>(std::min(127, note)), canvas);
        g.setColour(juce::Colour::fromRGB(45, 48, 60));
        g.drawVerticalLine(x, static_cast<float>(canvas.getY()), static_cast<float>(canvas.getBottom()));
    }

    // Velocity horizontal markers
    for (int v = 32; v < 128; v += 32) {
        const int y = velToY(static_cast<std::uint8_t>(v), canvas);
        g.setColour(juce::Colour::fromRGB(38, 40, 50));
        g.drawHorizontalLine(y, static_cast<float>(canvas.getX()), static_cast<float>(canvas.getRight()));
    }

    // Draw Sample Zone Rectangles
    static const juce::Colour zonePalette[] = {
        juce::Colour::fromRGB(50, 130, 190),
        juce::Colour::fromRGB(180, 100, 60),
        juce::Colour::fromRGB(60, 160, 110),
        juce::Colour::fromRGB(150, 70, 170),
        juce::Colour::fromRGB(190, 150, 50)
    };

    for (std::size_t i = 0; i < m_regions.size(); ++i) {
        const auto& reg = m_regions[i];
        const auto rect = zoneToRect(reg, canvas);
        const bool isSelected = (static_cast<int>(i) == m_selectedZoneIdx);

        const auto color = zonePalette[i % 5];
        g.setColour(color.withAlpha(isSelected ? 0.75f : 0.45f));
        g.fillRect(rect);

        g.setColour(isSelected ? juce::Colours::white : color.brighter(0.3f));
        g.drawRect(rect, isSelected ? 2 : 1);

        // Root note indicator pin
        const int rx = noteToX(reg.rootNote, canvas);
        if (rx >= rect.getX() && rx <= rect.getRight()) {
            g.setColour(juce::Colour::fromRGB(255, 220, 60));
            g.fillEllipse(static_cast<float>(rx - 3), static_cast<float>(rect.getY() + 3), 6.0f, 6.0f);
        }

        // Zone label
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(juce::String(reg.articulationName) + " [C" + juce::String((reg.rootNote / 12) - 1) + "]",
                   rect.reduced(4, 2), juce::Justification::topLeft, true);
    }

    // Bottom Piano Keyboard Strip
    g.setColour(juce::Colour::fromRGB(30, 32, 40));
    g.fillRect(keyboardArea);
    for (int note = 0; note < 128; ++note) {
        const int x1 = noteToX(static_cast<std::uint8_t>(note), canvas);
        const int x2 = noteToX(static_cast<std::uint8_t>(note + 1), canvas);
        const int noteInOct = note % 12;
        const bool isBlack = (noteInOct == 1 || noteInOct == 3 || noteInOct == 6 || noteInOct == 8 || noteInOct == 10);

        juce::Rectangle<int> keyRect(x1, keyboardArea.getY(), std::max(1, x2 - x1), keyboardArea.getHeight());
        g.setColour(isBlack ? juce::Colour::fromRGB(20, 20, 24) : juce::Colour::fromRGB(200, 205, 215));
        g.fillRect(keyRect.reduced(1, 0));

        if (noteInOct == 0) {
            g.setColour(juce::Colour::fromRGB(80, 140, 220));
            g.setFont(juce::FontOptions(8.0f));
            g.drawText("C" + juce::String((note / 12) - 1), keyRect.getX(), keyRect.getY() + 8, keyRect.getWidth() + 14, 12, juce::Justification::left);
        }
    }
}

void SampleZoneEditorView::resized()
{
    auto area = getLocalBounds().reduced(4, 2);
    auto tb = area.removeFromTop(m_toolbarHeight);

    m_addZoneButton.setBounds(tb.removeFromLeft(85).reduced(2, 2));
    m_removeZoneButton.setBounds(tb.removeFromLeft(75).reduced(2, 2));
    m_loadSfzButton.setBounds(tb.removeFromLeft(75).reduced(2, 2));
    tb.removeFromLeft(8);
    m_articulationCombo.setBounds(tb.removeFromLeft(180).reduced(2, 2));
    tb.removeFromLeft(8);
    m_infoLabel.setBounds(tb);
}

void SampleZoneEditorView::mouseDown(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(m_toolbarHeight);
    bounds.removeFromLeft(m_rulerWidth);
    auto keyboardArea = bounds.removeFromBottom(m_bottomKeyboardHeight);
    auto canvas = bounds;

    if (keyboardArea.contains(e.getPosition())) {
        const auto note = xToNote(e.x, canvas);
        if (m_auditionCb) {
            m_auditionCb(note, 100);
        }
        return;
    }

    if (canvas.contains(e.getPosition())) {
        const auto note = xToNote(e.x, canvas);
        const auto vel = yToVel(e.y, canvas);

        m_selectedZoneIdx = -1;
        for (int i = static_cast<int>(m_regions.size()) - 1; i >= 0; --i) {
            const auto& reg = m_regions[static_cast<std::size_t>(i)];
            if (note >= reg.lowNote && note <= reg.highNote &&
                vel >= reg.lowVel && vel <= reg.highVel) {
                m_selectedZoneIdx = i;
                m_isDraggingZone = true;
                break;
            }
        }

        if (m_selectedZoneIdx >= 0 && m_auditionCb) {
            m_auditionCb(note, vel);
        }
        repaint();
    }
}

void SampleZoneEditorView::mouseDrag(const juce::MouseEvent& e)
{
    if (m_isDraggingZone && m_selectedZoneIdx >= 0 && m_selectedZoneIdx < static_cast<int>(m_regions.size())) {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(m_toolbarHeight);
        bounds.removeFromLeft(m_rulerWidth);
        bounds.removeFromBottom(m_bottomKeyboardHeight);
        auto canvas = bounds;

        const auto note = xToNote(e.x, canvas);
        const auto vel = yToVel(e.y, canvas);

        auto& reg = m_regions[static_cast<std::size_t>(m_selectedZoneIdx)];
        reg.highNote = std::max(reg.lowNote, note);
        reg.highVel = std::max(reg.lowVel, vel);
        repaint();
    }
}

void SampleZoneEditorView::mouseUp(const juce::MouseEvent&)
{
    m_isDraggingZone = false;
}

} // namespace dawg::ui
