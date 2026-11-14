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
    m_selectedNoteId.reset();
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
    // Pitch 127 is top (y = 0), Pitch 0 is bottom
    const int clampedPitch = std::clamp<int>(pitch, 0, 127);
    return (127 - clampedPitch) * m_noteRowHeight - m_scrollY;
}

std::uint8_t PianoRollView::pixelToPitch(const int y) const noexcept
{
    const int effectiveY = y + m_scrollY;
    const int row = effectiveY / m_noteRowHeight;
    const int pitch = 127 - row;
    return static_cast<std::uint8_t>(std::clamp(pitch, 0, 127));
}

const project::MidiNote* PianoRollView::findNoteAt(const juce::Point<int> pos, bool& outNearRightEdge) const noexcept
{
    outNearRightEdge = false;
    const auto* clip = findActiveClip();
    if (clip == nullptr || pos.x < m_keyWidth)
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

        if (pitch % 12 == 0) // C note
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

    // Row backgrounds
    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        const int y = pitchToPixel(static_cast<std::uint8_t>(pitch));
        if (y + m_noteRowHeight < bounds.getY() || y > bounds.getBottom())
            continue;

        const auto black = isBlackKey(pitch);
        g.setColour(black ? juce::Colour::fromRGB(20, 21, 24) : juce::Colour::fromRGB(26, 27, 32));
        g.fillRect(bounds.getX(), y, bounds.getWidth(), m_noteRowHeight);

        g.setColour(juce::Colour::fromRGB(32, 34, 40));
        g.drawHorizontalLine(y + m_noteRowHeight - 1, static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
    }

    // Vertical beat and bar lines
    const double bpm = std::max(20.0, m_model.project.tempo.bpm);
    constexpr double sampleRate = 48000.0;
    const double samplesPerBeat = (60.0 / bpm) * sampleRate;
    const int beatsPerBar = std::max<uint8_t>(1, m_model.project.tempo.timeSigNumerator);
    const double samplesPer16th = samplesPerBeat / 4.0;

    const int maxVisiblePx = bounds.getRight();
    double currentSample = 0.0;
    int index = 0;

    while (true)
    {
        const int px = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(currentSample));
        if (px > maxVisiblePx)
            break;

        if (px >= bounds.getX())
        {
            const bool isBar = (index % (beatsPerBar * 4) == 0);
            const bool isBeat = (index % 4 == 0);

            if (isBar)
            {
                g.setColour(juce::Colour::fromRGB(70, 75, 88));
                g.drawVerticalLine(px, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
            }
            else if (isBeat)
            {
                g.setColour(juce::Colour::fromRGB(45, 48, 56));
                g.drawVerticalLine(px, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
            }
        }
        currentSample += samplesPer16th;
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

        const bool isSelected = m_selectedNoteId.has_value() && *m_selectedNoteId == note.id;

        // Velocity affects opacity and brightness
        const float velNorm = static_cast<float>(note.velocity) / 127.0f;
        const auto baseColor = isSelected
            ? juce::Colour::fromRGB(255, 170, 0)
            : juce::Colour::fromRGB(0, static_cast<juce::uint8>(180 + 75.0f * velNorm), 255);

        g.setColour(baseColor.withAlpha(0.85f + 0.15f * velNorm));
        g.fillRoundedRectangle(noteRect, 2.0f);

        g.setColour(isSelected ? juce::Colours::white : baseColor.brighter(0.3f));
        g.drawRoundedRectangle(noteRect, 2.0f, 1.0f);

        // Note label if wide enough
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

            const bool isSelected = m_selectedNoteId.has_value() && *m_selectedNoteId == note.id;
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
    const auto velocityBounds = totalBounds.removeFromBottom(m_velocityHeight);
    const auto keysBounds = totalBounds.removeFromLeft(m_keyWidth);
    const auto gridBounds = totalBounds;

    drawGrid(g, gridBounds);
    drawNotes(g, gridBounds);
    drawPlayhead(g, gridBounds);
    drawPianoKeys(g, keysBounds);
    drawVelocityLane(g, velocityBounds);
}

void PianoRollView::resized()
{
}

void PianoRollView::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    m_dragStartPos = e.getPosition();

    if (e.y >= getHeight() - m_velocityHeight)
    {
        // Click in velocity lane
        m_dragMode = DragMode::adjustVelocity;
        return;
    }

    if (e.x < m_keyWidth)
    {
        // Click on piano key - preview note
        return;
    }

    bool nearRightEdge = false;
    const auto* note = findNoteAt(e.getPosition(), nearRightEdge);

    if (note != nullptr)
    {
        m_selectedNoteId = note->id;
        m_draggedNoteId = note->id;
        m_dragInitialNote = *note;
        m_dragMode = nearRightEdge ? DragMode::resizeNote : DragMode::moveNote;
        repaint();
        return;
    }

    // Double-click or single click on empty grid: create note
    const auto* clip = findActiveClip();
    if (clip != nullptr && m_handler)
    {
        const auto pitch = pixelToPitch(e.y);
        const auto startSample = pixelToSample(e.x);
        project::MidiNote newNote {
            .id = 0,
            .pitch = pitch,
            .velocity = 100,
            .startSample = static_cast<std::uint64_t>(startSample),
            .lengthSamples = 24000 // 1/4 note default length
        };
        m_handler(project::AddMidiNote {clip->id, newNote});
    }
}

void PianoRollView::mouseDrag(const juce::MouseEvent& e)
{
    const auto* clip = findActiveClip();
    if (clip == nullptr || ! m_handler || ! m_draggedNoteId.has_value())
        return;

    const int deltaX = e.x - m_dragStartPos.x;
    const auto deltaSample = static_cast<std::int64_t>(static_cast<double>(deltaX) * m_samplesPerPixel);

    if (m_dragMode == DragMode::moveNote)
    {
        const auto newStart = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, static_cast<std::int64_t>(m_dragInitialNote.startSample) + deltaSample));
        const auto newPitch = pixelToPitch(e.y);
        m_handler(project::MoveMidiNote {clip->id, *m_draggedNoteId, newStart, newPitch});
    }
    else if (m_dragMode == DragMode::resizeNote)
    {
        const auto newLen = static_cast<std::uint64_t>(
            std::max<std::int64_t>(1000, static_cast<std::int64_t>(m_dragInitialNote.lengthSamples) + deltaSample));
        m_handler(project::ResizeMidiNote {clip->id, *m_draggedNoteId, newLen});
    }
}

void PianoRollView::mouseUp(const juce::MouseEvent&)
{
    m_dragMode = DragMode::none;
    m_draggedNoteId.reset();
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

    if (m_selectedNoteId.has_value() && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        m_handler(project::RemoveMidiNote {clip->id, *m_selectedNoteId});
        m_selectedNoteId.reset();
        return true;
    }
    return false;
}

} // namespace dawg::ui
