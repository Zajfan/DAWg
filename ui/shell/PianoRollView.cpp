#include "PianoRollView.h"

#include <algorithm>
#include <cmath>

namespace dawg::ui {
namespace {

bool isBlackKey(const int pitch) noexcept
{
    const int noteInOctave = pitch % 12;
    return noteInOctave == 1 || noteInOctave == 3 || noteInOctave == 6
        || noteInOctave == 8 || noteInOctave == 10;
}

const char* getNoteName(const int pitch) noexcept
{
    static constexpr const char* names[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return names[pitch % 12];
}

} // namespace

PianoRollView::PianoRollView(CommandHandler handler)
    : m_handler(std::move(handler))
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    m_gridCombo.addItem("Grid: 1/4", 1);
    m_gridCombo.addItem("Grid: 1/8", 2);
    m_gridCombo.addItem("Grid: 1/16", 3);
    m_gridCombo.addItem("Grid: 1/32", 4);
    m_gridCombo.addItem("Grid: 1/8T", 5);
    m_gridCombo.addItem("Grid: 1/16T", 6);
    m_gridCombo.setSelectedId(3);
    m_gridCombo.onChange = [this] {
        switch (m_gridCombo.getSelectedId())
        {
        case 1: m_grid = QuantizeGrid::quarter; break;
        case 2: m_grid = QuantizeGrid::eighth; break;
        case 3: m_grid = QuantizeGrid::sixteenth; break;
        case 4: m_grid = QuantizeGrid::thirtySecond; break;
        case 5: m_grid = QuantizeGrid::eighthTriplet; break;
        case 6: m_grid = QuantizeGrid::sixteenthTriplet; break;
        default: m_grid = QuantizeGrid::sixteenth; break;
        }
        repaint();
    };
    addAndMakeVisible(m_gridCombo);

    m_swingCombo.addItem("Straight (0%)", 1);
    m_swingCombo.addItem("Swing 25%", 2);
    m_swingCombo.addItem("Swing 50%", 3);
    m_swingCombo.addItem("Swing 75%", 4);
    m_swingCombo.setSelectedId(1);
    m_swingCombo.onChange = [this] {
        switch (m_swingCombo.getSelectedId())
        {
        case 1: m_swing = 0.0f; break;
        case 2: m_swing = 0.25f; break;
        case 3: m_swing = 0.50f; break;
        case 4: m_swing = 0.75f; break;
        default: m_swing = 0.0f; break;
        }
    };
    addAndMakeVisible(m_swingCombo);

    m_quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 110, 160));
    m_quantizeButton.onClick = [this] { quantizeNotes(); };
    addAndMakeVisible(m_quantizeButton);
}

void PianoRollView::setViewModel(const EditorViewModel& model)
{
    m_model = model;
    if (! m_activeClipId.has_value() && ! m_model.project.midiClips.empty())
        m_activeClipId = m_model.project.midiClips.front().id;
    repaint();
}

void PianoRollView::setActiveClipId(const std::optional<project::MidiClipId> clipId)
{
    m_activeClipId = clipId;
    m_selectedNoteIds.clear();
    repaint();
}

const project::MidiClip* PianoRollView::findActiveClip() const noexcept
{
    if (! m_activeClipId.has_value())
        return nullptr;

    for (const auto& clip : m_model.project.midiClips)
    {
        if (clip.id == *m_activeClipId)
            return &clip;
    }
    return nullptr;
}

bool PianoRollView::isNoteSelected(const project::NoteId id) const noexcept
{
    return std::find(m_selectedNoteIds.begin(), m_selectedNoteIds.end(), id) != m_selectedNoteIds.end();
}

double PianoRollView::gridIntervalSamples() const noexcept
{
    const double bpm = std::max(20.0, m_model.project.tempo.bpm);
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (60.0 / bpm) * sampleRate;

    switch (m_grid)
    {
    case QuantizeGrid::quarter: return samplesPerBeat;
    case QuantizeGrid::eighth: return samplesPerBeat / 2.0;
    case QuantizeGrid::sixteenth: return samplesPerBeat / 4.0;
    case QuantizeGrid::thirtySecond: return samplesPerBeat / 8.0;
    case QuantizeGrid::eighthTriplet: return samplesPerBeat / 3.0;
    case QuantizeGrid::sixteenthTriplet: return samplesPerBeat / 6.0;
    }
    return samplesPerBeat / 4.0;
}

std::int64_t PianoRollView::snapSample(const std::int64_t sample) const noexcept
{
    const double interval = gridIntervalSamples();
    if (interval <= 0.0)
        return std::max<std::int64_t>(0, sample);

    const double snapped = std::round(static_cast<double>(sample) / interval) * interval;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(snapped));
}

void PianoRollView::quantizeNotes()
{
    const auto* clip = findActiveClip();
    if (clip == nullptr || ! m_handler)
        return;

    const double interval = gridIntervalSamples();
    if (interval <= 0.0)
        return;

    for (const auto& note : clip->notes)
    {
        if (! m_selectedNoteIds.empty() && ! isNoteSelected(note.id))
            continue;

        const auto stepIndex = static_cast<std::int64_t>(std::round(static_cast<double>(note.startSample) / interval));
        double targetSample = static_cast<double>(stepIndex) * interval;

        // Apply swing on odd 16th/8th beats
        if (std::abs(stepIndex % 2) == 1 && m_swing > 0.0f)
        {
            targetSample += static_cast<double>(m_swing) * (interval * 0.3333333333);
        }

        const auto newStart = static_cast<std::uint64_t>(std::max(0.0, targetSample));
        if (newStart != note.startSample)
        {
            m_handler(project::MoveMidiNote {clip->id, note.id, newStart, note.pitch});
        }
    }
}

void PianoRollView::selectAllNotes()
{
    const auto* clip = findActiveClip();
    if (clip == nullptr)
        return;

    m_selectedNoteIds.clear();
    for (const auto& n : clip->notes)
        m_selectedNoteIds.push_back(n.id);
    repaint();
}

int PianoRollView::sampleToPixel(const std::int64_t sample) const noexcept
{
    if (m_samplesPerPixel <= 0.0)
        return 0;
    return static_cast<int>(std::round(static_cast<double>(sample) / m_samplesPerPixel));
}

std::int64_t PianoRollView::pixelToSample(const int x) const noexcept
{
    const int gridX = std::max(0, x - m_keyWidth);
    return static_cast<std::int64_t>(std::round(static_cast<double>(gridX) * m_samplesPerPixel));
}

int PianoRollView::pitchToPixel(const std::uint8_t pitch) const noexcept
{
    const int clampedPitch = std::clamp<int>(pitch, 0, 127);
    return m_toolbarHeight + (127 - clampedPitch) * m_noteRowHeight - m_scrollY;
}

std::uint8_t PianoRollView::pixelToPitch(const int y) const noexcept
{
    const int effectiveY = (y - m_toolbarHeight) + m_scrollY;
    const int row = effectiveY / m_noteRowHeight;
    const int pitch = 127 - row;
    return static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
}

const project::MidiNote* PianoRollView::findNoteAt(const juce::Point<int> pos, bool& outNearRightEdge) const noexcept
{
    outNearRightEdge = false;
    const auto* clip = findActiveClip();
    if (clip == nullptr || pos.x < m_keyWidth || pos.y < m_toolbarHeight)
        return nullptr;

    const auto targetPitch = pixelToPitch(pos.y);
    const auto clickSample = pixelToSample(pos.x);

    for (const auto& note : clip->notes)
    {
        if (note.pitch == targetPitch)
        {
            const auto noteStart = static_cast<std::int64_t>(note.startSample);
            const auto noteEnd = noteStart + static_cast<std::int64_t>(note.lengthSamples);
            if (clickSample >= noteStart && clickSample < noteEnd)
            {
                const int noteRightPx = m_keyWidth + sampleToPixel(noteEnd);
                if (std::abs(pos.x - noteRightPx) <= 6)
                    outNearRightEdge = true;
                return &note;
            }
        }
    }
    return nullptr;
}

void PianoRollView::drawPianoKeys(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    g.saveState();
    g.reduceClipRegion(bounds);

    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        const int y = pitchToPixel(static_cast<std::uint8_t>(pitch));
        if (y + m_noteRowHeight < bounds.getY() || y > bounds.getBottom())
            continue;

        const auto black = isBlackKey(pitch);
        const auto keyBounds = juce::Rectangle<int> {0, y, m_keyWidth, m_noteRowHeight};

        g.setColour(black ? juce::Colour::fromRGB(24, 25, 29) : juce::Colour::fromRGB(45, 47, 54));
        g.fillRect(keyBounds);
        g.setColour(juce::Colour::fromRGB(18, 19, 22));
        g.drawHorizontalLine(y + m_noteRowHeight - 1, 0.0f, static_cast<float>(m_keyWidth));

        if (pitch % 12 == 0)
        {
            g.setColour(juce::Colour::fromRGB(0, 229, 255));
            g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
            const int octave = (pitch / 12) - 1;
            const auto label = juce::String("C") + juce::String(octave);
            g.drawText(label, keyBounds.reduced(4, 1), juce::Justification::centredRight, false);
        }
    }

    g.setColour(juce::Colour::fromRGB(15, 16, 18));
    g.drawVerticalLine(m_keyWidth - 1, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    g.restoreState();
}

void PianoRollView::drawGrid(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    g.saveState();
    g.reduceClipRegion(bounds);

    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        const int y = pitchToPixel(static_cast<std::uint8_t>(pitch));
        if (y + m_noteRowHeight < bounds.getY() || y > bounds.getBottom())
            continue;

        const auto black = isBlackKey(pitch);
        g.setColour(black ? juce::Colour::fromRGB(21, 22, 26) : juce::Colour::fromRGB(27, 28, 33));
        g.fillRect(bounds.getX(), y, bounds.getWidth(), m_noteRowHeight);

        g.setColour(juce::Colour::fromRGB(34, 36, 42));
        g.drawHorizontalLine(y + m_noteRowHeight - 1, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    const double bpm = std::max(20.0, m_model.project.tempo.bpm);
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (60.0 / bpm) * sampleRate;
    const double interval = gridIntervalSamples();

    if (interval <= 0.0)
    {
        g.restoreState();
        return;
    }

    const auto maxSample = pixelToSample(bounds.getRight());
    double currentSample = 0.0;
    int index = 0;

    while (currentSample <= static_cast<double>(maxSample))
    {
        const int px = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(currentSample));
        if (px >= bounds.getX() && px <= bounds.getRight())
        {
            const bool isBeat = std::fmod(currentSample + 1.0, samplesPerBeat) < 2.0;
            if (isBeat)
            {
                g.setColour(juce::Colour::fromRGB(65, 70, 82));
                g.drawVerticalLine(px, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
            }
            else
            {
                g.setColour(juce::Colour::fromRGB(38, 40, 48));
                g.drawVerticalLine(px, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
            }
        }
        currentSample += interval;
        ++index;
    }

    g.restoreState();
}

void PianoRollView::drawNotes(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    const auto* clip = findActiveClip();
    if (clip == nullptr)
        return;

    g.saveState();
    g.reduceClipRegion(bounds);

    for (const auto& note : clip->notes)
    {
        const int noteX = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
        const int noteW = std::max(4, sampleToPixel(static_cast<std::int64_t>(note.lengthSamples)));
        const int noteY = pitchToPixel(note.pitch);

        if (noteY + m_noteRowHeight < bounds.getY() || noteY > bounds.getBottom())
            continue;

        const auto noteRect = juce::Rectangle<float> {
            static_cast<float>(noteX),
            static_cast<float>(noteY + 1),
            static_cast<float>(noteW),
            static_cast<float>(m_noteRowHeight - 2)
        };

        const bool isSelected = isNoteSelected(note.id);

        const float velNorm = static_cast<float>(note.velocity) / 127.0f;
        const auto baseColor = isSelected
            ? juce::Colour::fromRGB(255, 170, 0)
            : juce::Colour::fromRGB(0, static_cast<juce::uint8>(180 + 75.0f * velNorm), 255);

        g.setColour(baseColor.withAlpha(0.85f + 0.15f * velNorm));
        g.fillRoundedRectangle(noteRect, 2.0f);

        g.setColour(isSelected ? juce::Colours::white : baseColor.brighter(0.3f));
        g.drawRoundedRectangle(noteRect, 2.0f, 1.0f);

        if (noteW > 28)
        {
            g.setColour(juce::Colour::fromRGB(15, 16, 20));
            g.setFont(juce::FontOptions {9.0f}.withStyle("Bold"));
            g.drawText(getNoteName(note.pitch), noteRect.toNearestInt().reduced(2, 0),
                       juce::Justification::centredLeft, false);
        }
    }

    g.restoreState();
}

void PianoRollView::drawLasso(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    if (m_dragMode == DragMode::selectLasso && ! m_lassoRect.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion(bounds);
        g.setColour(juce::Colour::fromRGB(0, 200, 255).withAlpha(0.2f));
        g.fillRect(m_lassoRect);
        g.setColour(juce::Colour::fromRGB(0, 220, 255).withAlpha(0.8f));
        g.drawRect(m_lassoRect, 1);
        g.restoreState();
    }
}

void PianoRollView::drawVelocityLane(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    g.saveState();
    g.reduceClipRegion(bounds);

    g.setColour(juce::Colour::fromRGB(18, 19, 23));
    g.fillRect(bounds);

    g.setColour(juce::Colour::fromRGB(36, 38, 45));
    g.drawHorizontalLine(bounds.getY(), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));

    g.setColour(juce::Colour::fromRGB(120, 125, 140));
    g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
    g.drawText("VELOCITY", juce::Rectangle<int> {4, bounds.getY() + 4, m_keyWidth - 8, 16},
               juce::Justification::centredLeft, false);

    const auto* clip = findActiveClip();
    if (clip != nullptr)
    {
        for (const auto& note : clip->notes)
        {
            const int noteX = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
            const float velNorm = std::clamp(static_cast<float>(note.velocity) / 127.0f, 0.0f, 1.0f);
            const int stemH = static_cast<int>(static_cast<float>(bounds.getHeight() - 8) * velNorm);
            const int stemY = bounds.getBottom() - 4 - stemH;

            const bool isSelected = isNoteSelected(note.id);
            g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 0) : juce::Colour::fromRGB(0, 229, 255));
            g.drawVerticalLine(noteX, static_cast<float>(stemY), static_cast<float>(bounds.getBottom() - 4));
            g.fillEllipse(static_cast<float>(noteX - 2), static_cast<float>(stemY - 2), 5.0f, 5.0f);
        }
    }

    g.restoreState();
}

void PianoRollView::drawPlayhead(juce::Graphics& g, const juce::Rectangle<int> bounds)
{
    const int px = m_keyWidth + sampleToPixel(m_model.playbackPosition);
    if (px >= bounds.getX() && px <= bounds.getRight())
    {
        g.setColour(juce::Colour::fromRGB(255, 51, 102));
        g.drawVerticalLine(px, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
    }
}

void PianoRollView::paint(juce::Graphics& g)
{
    auto totalBounds = getLocalBounds();
    const auto toolbarBounds = totalBounds.removeFromTop(m_toolbarHeight);
    const auto velocityBounds = totalBounds.removeFromBottom(m_velocityHeight);
    const auto keysBounds = totalBounds.removeFromLeft(m_keyWidth);
    const auto gridBounds = totalBounds;

    g.setColour(juce::Colour::fromRGB(26, 28, 35));
    g.fillRect(toolbarBounds);
    g.setColour(juce::Colour::fromRGB(40, 42, 50));
    g.drawHorizontalLine(toolbarBounds.getBottom() - 1, 0.0f, static_cast<float>(getWidth()));

    drawGrid(g, gridBounds);
    drawNotes(g, gridBounds);
    drawLasso(g, gridBounds);
    drawPlayhead(g, gridBounds);
    drawPianoKeys(g, keysBounds);
    drawVelocityLane(g, velocityBounds);
}

void PianoRollView::resized()
{
    auto toolbarArea = getLocalBounds().removeFromTop(m_toolbarHeight).reduced(4, 2);
    m_gridCombo.setBounds(toolbarArea.removeFromLeft(104));
    toolbarArea.removeFromLeft(6);
    m_swingCombo.setBounds(toolbarArea.removeFromLeft(104));
    toolbarArea.removeFromLeft(6);
    m_quantizeButton.setBounds(toolbarArea.removeFromLeft(110));
}

void PianoRollView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    m_dragStartPos = e.getPosition();

    if (e.y < m_toolbarHeight)
        return;

    if (e.y >= getHeight() - m_velocityHeight)
    {
        m_dragMode = DragMode::adjustVelocity;
        return;
    }

    if (e.x < m_keyWidth)
    {
        return;
    }

    bool nearRightEdge = false;
    const auto* note = findNoteAt(e.getPosition(), nearRightEdge);

    if (note != nullptr)
    {
        if (e.mods.isShiftDown() || e.mods.isCtrlDown())
        {
            const auto it = std::find(m_selectedNoteIds.begin(), m_selectedNoteIds.end(), note->id);
            if (it != m_selectedNoteIds.end())
                m_selectedNoteIds.erase(it);
            else
                m_selectedNoteIds.push_back(note->id);
        }
        else
        {
            if (! isNoteSelected(note->id))
                m_selectedNoteIds = { note->id };
        }

        m_draggedNoteId = note->id;
        m_dragInitialNote = *note;
        m_dragMode = nearRightEdge ? DragMode::resizeNote : DragMode::moveNote;
        repaint();
        return;
    }

    // Double-click on empty grid: create note
    const auto* clip = findActiveClip();
    if (e.getNumberOfClicks() >= 2 && clip != nullptr && m_handler)
    {
        const auto pitch = pixelToPitch(e.y);
        const auto startSample = static_cast<std::uint64_t>(snapSample(pixelToSample(e.x)));
        const auto len = static_cast<std::uint64_t>(std::max<std::int64_t>(1000, static_cast<std::int64_t>(gridIntervalSamples())));
        project::MidiNote newNote {
            .id = 0,
            .pitch = pitch,
            .velocity = 100,
            .startSample = startSample,
            .lengthSamples = len
        };
        m_handler(project::AddMidiNote {clip->id, newNote});
        return;
    }

    // Single click on empty grid: selection lasso
    if (! e.mods.isShiftDown() && ! e.mods.isCtrlDown())
    {
        m_selectedNoteIds.clear();
    }
    m_dragMode = DragMode::selectLasso;
    m_lassoRect = { e.x, e.y, 0, 0 };
    repaint();
}

void PianoRollView::mouseDrag(const juce::MouseEvent& e)
{
    const auto* clip = findActiveClip();
    if (clip == nullptr || ! m_handler)
        return;

    const int deltaX = e.x - m_dragStartPos.x;
    const auto deltaSample = static_cast<std::int64_t>(static_cast<double>(deltaX) * m_samplesPerPixel);

    if (m_dragMode == DragMode::selectLasso)
    {
        const int x1 = std::min(m_dragStartPos.x, e.x);
        const int y1 = std::min(m_dragStartPos.y, e.y);
        const int x2 = std::max(m_dragStartPos.x, e.x);
        const int y2 = std::max(m_dragStartPos.y, e.y);
        m_lassoRect = juce::Rectangle<int>::leftTopRightBottom(x1, y1, x2, y2);

        for (const auto& note : clip->notes)
        {
            const int noteX = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
            const int noteW = std::max(4, sampleToPixel(static_cast<std::int64_t>(note.lengthSamples)));
            const int noteY = pitchToPixel(note.pitch);
            const juce::Rectangle<int> noteRect { noteX, noteY + 1, noteW, m_noteRowHeight - 2 };

            if (m_lassoRect.intersects(noteRect))
            {
                if (! isNoteSelected(note.id))
                    m_selectedNoteIds.push_back(note.id);
            }
        }
        repaint();
        return;
    }

    if (m_dragMode == DragMode::adjustVelocity)
    {
        const int deltaY = m_dragStartPos.y - e.y;
        const int deltaVel = static_cast<int>(static_cast<float>(deltaY) * 1.5f);

        for (const auto& note : clip->notes)
        {
            if (m_selectedNoteIds.empty() || isNoteSelected(note.id))
            {
                const int newVel = std::clamp(static_cast<int>(note.velocity) + deltaVel, 1, 127);
                m_handler(project::SetMidiNoteVelocity {clip->id, note.id, static_cast<std::uint8_t>(newVel)});
            }
        }
        m_dragStartPos.y = e.y;
        return;
    }

    if (m_dragMode == DragMode::moveNote && m_draggedNoteId.has_value())
    {
        const auto rawStart = static_cast<std::int64_t>(m_dragInitialNote.startSample) + deltaSample;
        const auto newStart = static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapSample(rawStart)));
        const auto newPitch = pixelToPitch(e.y);
        m_handler(project::MoveMidiNote {clip->id, *m_draggedNoteId, newStart, newPitch});
    }
    else if (m_dragMode == DragMode::resizeNote && m_draggedNoteId.has_value())
    {
        const auto rawLen = static_cast<std::int64_t>(m_dragInitialNote.lengthSamples) + deltaSample;
        const auto minLen = static_cast<std::int64_t>(gridIntervalSamples() / 2.0);
        const auto newLen = static_cast<std::uint64_t>(std::max<std::int64_t>(minLen, rawLen));
        m_handler(project::ResizeMidiNote {clip->id, *m_draggedNoteId, newLen});
    }
}

void PianoRollView::mouseUp(const juce::MouseEvent&)
{
    m_dragMode = DragMode::none;
    m_draggedNoteId.reset();
    m_lassoRect = {};
    repaint();
}

void PianoRollView::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs(wheel.deltaY) > 1e-4f)
    {
        m_scrollY = std::clamp(m_scrollY - static_cast<int>(wheel.deltaY * 60.0f), 0, 128 * m_noteRowHeight - getHeight());
        repaint();
    }
}

bool PianoRollView::keyPressed(const juce::KeyPress& key)
{
    const auto* clip = findActiveClip();
    if (clip == nullptr || ! m_handler)
        return false;

    if (key.getKeyCode() == 'Q' || key.getKeyCode() == 'q')
    {
        quantizeNotes();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && (key.getKeyCode() == 'A' || key.getKeyCode() == 'a'))
    {
        selectAllNotes();
        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        for (const auto id : m_selectedNoteIds)
        {
            m_handler(project::RemoveMidiNote {clip->id, id});
        }
        m_selectedNoteIds.clear();
        return true;
    }
    return false;
}

} // namespace dawg::ui
