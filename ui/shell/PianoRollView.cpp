#include "PianoRollView.h"

#include <dawg/engine/dsp/PolymetricSequencerEngine.h>

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

PianoRollView::PianoRollView(CommandHandler handler, AuditionCallback auditionCb)
    : m_handler(std::move(handler))
    , m_auditionCallback(std::move(auditionCb))
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

    m_swingCombo.addItem("Straight (50%)", 1);
    m_swingCombo.addItem("MPC60 Shuffle (54%)", 2);
    m_swingCombo.addItem("MPC60 Shuffle (58%)", 3);
    m_swingCombo.addItem("MPC60 Heavy (62%)", 4);
    m_swingCombo.addItem("SP-1200 Swing (59%)", 5);
    m_swingCombo.addItem("SP-1200 Swing (65%)", 6);
    m_swingCombo.addItem("Triplet Swing (66.7%)", 7);
    m_swingCombo.setSelectedId(1);
    m_swingCombo.onChange = [this] {
        switch (m_swingCombo.getSelectedId())
        {
        case 1: m_swing = 0.0f; break;
        case 2: m_swing = 0.08f; break; // 54%
        case 3: m_swing = 0.16f; break; // 58%
        case 4: m_swing = 0.24f; break; // 62%
        case 5: m_swing = 0.18f; break; // 59%
        case 6: m_swing = 0.30f; break; // 65%
        case 7: m_swing = 0.33f; break; // 66.7%
        default: m_swing = 0.0f; break;
        }
    };
    addAndMakeVisible(m_swingCombo);

    m_quantizeButton.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(45, 110, 160));
    m_quantizeButton.onClick = [this] { quantizeNotes(); };
    addAndMakeVisible(m_quantizeButton);

    m_btnEuclidean.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(60, 130, 80));
    m_btnEuclidean.onClick = [this] {
        const auto* clip = findActiveClip();
        if (clip == nullptr || ! m_handler)
            return;

        juce::PopupMenu menu;
        menu.addItem(1, "3 Hits in 8 (Tresillo)");
        menu.addItem(2, "5 Hits in 16 (Bossa Nova / Cumbia)");
        menu.addItem(3, "7 Hits in 16 (West African Bell)");
        menu.addItem(4, "4 Hits in 7 (Balkan Polymetric)");
        menu.addItem(5, "9 Hits in 16 (Steve Reich)");

        menu.showMenuAsync(juce::PopupMenu::Options {}, [this, clipId = clip->id](int res) {
            if (res <= 0) return;
            std::size_t hits = 5;
            std::size_t steps = 16;
            if (res == 1) { hits = 3; steps = 8; }
            else if (res == 2) { hits = 5; steps = 16; }
            else if (res == 3) { hits = 7; steps = 16; }
            else if (res == 4) { hits = 4; steps = 7; }
            else if (res == 5) { hits = 9; steps = 16; }

            engine::dsp::PolymetricSequencerEngine seq;
            seq.generateEuclidean(0, hits, steps, 0, 60, 0.85f);
            const auto& trk = seq.getTrack(0);

            const double stepSamples = gridIntervalSamples();
            for (std::size_t s = 0; s < trk.stepCount; ++s)
            {
                if (trk.steps[s].active)
                {
                    project::MidiNote n;
                    n.id = static_cast<project::NoteId>(m_model.project.midiClips.size() * 1000 + s + 1);
                    n.pitch = trk.steps[s].pitch;
                    n.velocity = static_cast<std::uint8_t>(trk.steps[s].velocity * 127.0f);
                    n.startSample = static_cast<std::uint64_t>(static_cast<double>(s) * stepSamples);
                    n.lengthSamples = static_cast<std::uint64_t>(stepSamples * static_cast<double>(trk.steps[s].gateFraction));
                    m_handler(project::AddMidiNote { clipId, n });
                }
            }
            repaint();
        });
    };
    addAndMakeVisible(m_btnEuclidean);

    m_btnMarkov.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(140, 80, 160));
    m_btnMarkov.onClick = [this] {
        const auto* clip = findActiveClip();
        if (clip == nullptr || ! m_handler)
            return;

        juce::PopupMenu menu;
        menu.addItem(1, "C Major: I - vi - IV - V (Pop)");
        menu.addItem(2, "A Minor: i - VI - III - VII (Epic Minor)");
        menu.addItem(3, "D Dorian: i - IV - v - i (Jazz / Fusion)");
        menu.addItem(4, "F Major: I - V - vi - IV (Anthem)");
        menu.addItem(5, "G Mixolydian: I - VII - IV - I (Blues / Funk)");

        menu.showMenuAsync(juce::PopupMenu::Options {}, [this, clipId = clip->id](int res) {
            if (res <= 0) return;
            std::uint8_t root = 60; // C4
            engine::dsp::MusicalScaleType scale = engine::dsp::MusicalScaleType::major;
            if (res == 2) { root = 57; scale = engine::dsp::MusicalScaleType::naturalMinor; } // A3
            else if (res == 3) { root = 62; scale = engine::dsp::MusicalScaleType::dorian; } // D4
            else if (res == 4) { root = 65; scale = engine::dsp::MusicalScaleType::major; }  // F4
            else if (res == 5) { root = 67; scale = engine::dsp::MusicalScaleType::mixolydian; } // G4

            const auto chords = engine::dsp::PolymetricSequencerEngine::generateMarkovChords(root, scale, 4, 0.2f);
            const double bpm = std::max(20.0, m_model.project.tempo.bpm);
            constexpr double sr = 48000.0;
            const double barSamples = (60.0 / bpm) * sr * 4.0; // 1 bar per chord

            for (std::size_t ci = 0; ci < chords.size(); ++ci)
            {
                const auto& chord = chords[ci];
                const auto chordStart = static_cast<std::uint64_t>(static_cast<double>(ci) * barSamples);
                const auto chordLen = static_cast<std::uint64_t>(barSamples * 0.95);

                for (std::size_t vi = 0; vi < chord.voiceCount; ++vi)
                {
                    project::MidiNote n;
                    n.id = static_cast<project::NoteId>(m_model.project.midiClips.size() * 2000 + ci * 10 + vi + 1);
                    n.pitch = chord.notes[vi];
                    n.velocity = 95;
                    n.startSample = chordStart;
                    n.lengthSamples = chordLen;
                    m_handler(project::AddMidiNote { clipId, n });
                }
            }
            repaint();
        });
    };
    addAndMakeVisible(m_btnMarkov);

    m_btnHumanize.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(180, 110, 50));
    m_btnHumanize.onClick = [this] {
        const auto* clip = findActiveClip();
        if (clip == nullptr || ! m_handler)
            return;

        std::uint32_t prng = 999983u;
        for (const auto& note : clip->notes)
        {
            if (! m_selectedNoteIds.empty() && ! isNoteSelected(note.id))
                continue;

            prng = prng * 1664525u + 1013904223u;
            const float timingJitterMs = (static_cast<float>(prng >> 16) / 65536.0f) * 16.0f - 8.0f; // +/- 8ms
            constexpr double sr = 48000.0;
            const auto jitterSamples = static_cast<std::int64_t>((timingJitterMs * 0.001f) * sr);

            prng = prng * 1664525u + 1013904223u;
            const int velJitter = static_cast<int>((static_cast<float>(prng >> 16) / 65536.0f) * 16.0f) - 8; // +/- 8

            const auto newStart = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(note.startSample) + jitterSamples));
            const auto newVel = static_cast<std::uint8_t>(std::clamp(static_cast<int>(note.velocity) + velJitter, 1, 127));

            if (newStart != note.startSample)
                m_handler(project::MoveMidiNote { clip->id, note.id, newStart, note.pitch });
            if (newVel != note.velocity)
                m_handler(project::SetMidiNoteVelocity { clip->id, note.id, newVel });
        }
        repaint();
    };
    addAndMakeVisible(m_btnHumanize);

    m_btnStepRecord.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(50, 55, 68));
    m_btnStepRecord.onClick = [this] { toggleStepRecord(); };
    addAndMakeVisible(m_btnStepRecord);

    auto updateModeButtons = [this] {
        m_btnVelocity.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::velocity ? juce::Colour::fromRGB(0, 160, 210) : juce::Colour::fromRGB(35, 38, 48));
        m_btnPitch.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::mpePitch ? juce::Colour::fromRGB(180, 70, 200) : juce::Colour::fromRGB(35, 38, 48));
        m_btnPressure.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::mpePressure ? juce::Colour::fromRGB(220, 120, 40) : juce::Colour::fromRGB(35, 38, 48));
        m_btnTimbre.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::mpeTimbre ? juce::Colour::fromRGB(50, 180, 100) : juce::Colour::fromRGB(35, 38, 48));
        m_btnVibrato.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::mpeVibrato ? juce::Colour::fromRGB(0, 220, 240) : juce::Colour::fromRGB(35, 38, 48));
        m_btnSlide.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::mpeSlide ? juce::Colour::fromRGB(255, 140, 20) : juce::Colour::fromRGB(35, 38, 48));
        m_btnVocalTune.setColour(juce::TextButton::buttonColourId, m_expressionMode == ExpressionMode::vocalPitchTune ? juce::Colour::fromRGB(240, 60, 100) : juce::Colour::fromRGB(35, 38, 48));
        repaint();
    };

    m_btnVelocity.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::velocity; updateModeButtons(); };
    m_btnPitch.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::mpePitch; updateModeButtons(); };
    m_btnPressure.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::mpePressure; updateModeButtons(); };
    m_btnTimbre.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::mpeTimbre; updateModeButtons(); };
    m_btnVibrato.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::mpeVibrato; updateModeButtons(); };
    m_btnSlide.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::mpeSlide; updateModeButtons(); };
    m_btnVocalTune.onClick = [this, updateModeButtons] { m_expressionMode = ExpressionMode::vocalPitchTune; updateModeButtons(); };

    addAndMakeVisible(m_btnVelocity);
    addAndMakeVisible(m_btnPitch);
    addAndMakeVisible(m_btnPressure);
    addAndMakeVisible(m_btnTimbre);
    addAndMakeVisible(m_btnVibrato);
    addAndMakeVisible(m_btnSlide);
    addAndMakeVisible(m_btnVocalTune);
    updateModeButtons();
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

void PianoRollView::toggleStepRecord()
{
    m_stepRecordEnabled = ! m_stepRecordEnabled;
    if (m_stepRecordEnabled)
    {
        m_stepCursorSample = static_cast<std::uint64_t>(std::max<std::int64_t>(0, m_model.playbackPosition));
    }
    m_btnStepRecord.setColour(juce::TextButton::buttonColourId,
        m_stepRecordEnabled ? juce::Colour::fromRGB(220, 50, 50) : juce::Colour::fromRGB(50, 55, 68));
    repaint();
}

void PianoRollView::handleIncomingStepNote(const std::uint8_t pitch, const std::uint8_t velocity)
{
    if (! m_stepRecordEnabled || ! m_handler)
        return;

    const auto* clip = findActiveClip();
    if (clip == nullptr)
        return;

    const double stepSamples = gridIntervalSamples();
    const auto noteStart = m_stepCursorSample;
    const auto noteLen = static_cast<std::uint64_t>(std::max(1.0, stepSamples));

    project::NoteId maxId = 0;
    for (const auto& n : clip->notes)
        maxId = std::max(maxId, n.id);

    project::MidiNote n;
    n.id = maxId + 1;
    n.pitch = pitch;
    n.velocity = velocity > 0 ? velocity : 100;
    n.startSample = noteStart;
    n.lengthSamples = noteLen;

    m_handler(project::AddMidiNote { clip->id, n });

    if (m_auditionCallback)
    {
        m_auditionCallback(pitch, velocity > 0 ? velocity : 100, true);
    }

    m_stepCursorSample += static_cast<std::uint64_t>(stepSamples);
    repaint();
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

        // Real-time MPE 5D visual expression ribbons
        if (m_expressionMode == ExpressionMode::mpePitch && noteW > 6)
        {
            // Per-note pitch bend trajectory ribbon (Electric Cyan)
            juce::Path pitchRibbon;
            const float midY = noteRect.getCentreY();
            const float startX = noteRect.getX() + 2.0f;
            const float endX = noteRect.getRight() - 2.0f;
            pitchRibbon.startNewSubPath(startX, midY);
            pitchRibbon.cubicTo(
                startX + (endX - startX) * 0.35f, midY - 3.5f,
                startX + (endX - startX) * 0.70f, midY + 2.5f,
                endX, midY - 1.0f);
            g.setColour(juce::Colour(0xff00e5ff));
            g.strokePath(pitchRibbon, juce::PathStrokeType(1.5f));
        }
        else if (m_expressionMode == ExpressionMode::mpePressure && noteW > 6)
        {
            // Per-note polyphonic pressure envelope ribbon (Neon Magenta)
            const float pH = std::clamp((static_cast<float>(m_noteRowHeight) - 4.0f) * velNorm, 2.0f, static_cast<float>(m_noteRowHeight - 4));
            const juce::Rectangle<float> pRibbon {
                noteRect.getX() + 2.0f,
                noteRect.getBottom() - pH - 2.0f,
                noteRect.getWidth() - 4.0f,
                pH
            };
            g.setColour(juce::Colour(0xffe040fb).withAlpha(0.6f));
            g.fillRoundedRectangle(pRibbon, 1.5f);
            g.setColour(juce::Colour(0xffea80fc));
            g.drawHorizontalLine(static_cast<int>(pRibbon.getY()), pRibbon.getX(), pRibbon.getRight());
        }
        else if (m_expressionMode == ExpressionMode::mpeTimbre && noteW > 6)
        {
            // Per-note CC74 Timbre / Brightness ribbon (Emerald Mint)
            const float tY = noteRect.getY() + 2.0f;
            juce::Path timbreRibbon;
            timbreRibbon.startNewSubPath(noteRect.getX() + 2.0f, tY + 4.0f);
            timbreRibbon.quadraticTo(noteRect.getCentreX(), tY + 1.0f, noteRect.getRight() - 2.0f, tY + 3.0f);
            g.setColour(juce::Colour(0xff00e676));
            g.strokePath(timbreRibbon, juce::PathStrokeType(1.5f));
        }
        else if (m_expressionMode == ExpressionMode::mpeVibrato && noteW > 6)
        {
            // Per-note Micro-Pitch Vibrato LFO Envelope (Electric Turquoise)
            juce::Path vibPath;
            const float startX = noteRect.getX() + 2.0f;
            const float endX = noteRect.getRight() - 2.0f;
            const float midY = noteRect.getCentreY();
            const float amp = std::clamp(static_cast<float>(m_noteRowHeight) * 0.28f, 2.0f, 6.0f);
            const float cycles = std::clamp(static_cast<float>(noteW) / 16.0f, 1.5f, 8.0f);

            vibPath.startNewSubPath(startX, midY);
            constexpr int steps = 32;
            for (int s = 1; s <= steps; ++s)
            {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                const float x = startX + t * (endX - startX);
                // Vibrato onset delay: starts after first 20% of duration
                const float env = (t > 0.2f) ? std::min(1.0f, (t - 0.2f) * 3.0f) : 0.0f;
                const float y = midY + std::sin(t * cycles * 6.2831853f) * (amp * env);
                vibPath.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xff00e5ff));
            g.strokePath(vibPath, juce::PathStrokeType(1.5f));
        }
        else if (m_expressionMode == ExpressionMode::mpeSlide && noteW > 6)
        {
            // Per-note Micro-Pitch Slide Curve (Sunset Orange)
            juce::Path slidePath;
            const float startX = noteRect.getX() + 2.0f;
            const float endX = noteRect.getRight() - 2.0f;
            const float midY = noteRect.getCentreY();
            const float slideH = static_cast<float>(m_noteRowHeight) * 0.35f;

            slidePath.startNewSubPath(startX, midY + slideH * 0.5f);
            slidePath.cubicTo(
                startX + (endX - startX) * 0.4f, midY + slideH * 0.5f,
                startX + (endX - startX) * 0.6f, midY - slideH,
                endX, midY - slideH);

            g.setColour(juce::Colour(0xffff9100));
            g.strokePath(slidePath, juce::PathStrokeType(2.0f));
            g.fillEllipse(endX - 2.5f, midY - slideH - 2.5f, 5.0f, 5.0f);
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

    const char* title = "VELOCITY";
    if (m_expressionMode == ExpressionMode::mpePitch)
        title = "MPE PITCH (+/-24 ST)";
    else if (m_expressionMode == ExpressionMode::mpePressure)
        title = "MPE PRESSURE";
    else if (m_expressionMode == ExpressionMode::mpeTimbre)
        title = "TIMBRE CC74";
    else if (m_expressionMode == ExpressionMode::vocalPitchTune)
        title = "VOCAL PITCH TUNING (MELODYNE / AUTO-TUNE)";

    g.setColour(juce::Colour::fromRGB(120, 125, 140));
    g.setFont(juce::FontOptions {10.0f}.withStyle("Bold"));
    g.drawText(title, juce::Rectangle<int> {4, bounds.getY() + 4, m_keyWidth - 8, 16},
               juce::Justification::centredLeft, false);

    const auto* clip = findActiveClip();
    if (clip != nullptr)
    {
        if (m_expressionMode == ExpressionMode::velocity)
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
        else if (m_expressionMode == ExpressionMode::mpePitch)
        {
            // Center zero-bend line
            const float midY = static_cast<float>(bounds.getY() + bounds.getHeight() / 2);
            g.setColour(juce::Colour::fromRGB(45, 50, 62));
            g.drawHorizontalLine(static_cast<int>(midY), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));

            for (const auto& note : clip->notes)
            {
                const int noteX1 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
                const int noteX2 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample + note.lengthSamples));
                const bool isSelected = isNoteSelected(note.id);

                g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 0) : juce::Colour::fromRGB(180, 80, 220));
                // Draw per-note MPE pitch glide curve
                juce::Path p;
                p.startNewSubPath(static_cast<float>(noteX1), midY);
                p.quadraticTo(static_cast<float>((noteX1 + noteX2) / 2), midY - 12.0f,
                              static_cast<float>(noteX2), midY);
                g.strokePath(p, juce::PathStrokeType(1.8f));
                g.fillEllipse(static_cast<float>(noteX1 - 2), midY - 2.0f, 5.0f, 5.0f);
                g.fillEllipse(static_cast<float>(noteX2 - 2), midY - 2.0f, 5.0f, 5.0f);
            }
        }
        else if (m_expressionMode == ExpressionMode::mpePressure)
        {
            for (const auto& note : clip->notes)
            {
                const int noteX1 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
                const int noteX2 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample + note.lengthSamples));
                const bool isSelected = isNoteSelected(note.id);

                g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 0) : juce::Colour::fromRGB(230, 130, 40));
                // Draw pressure swell curve
                const float baseY = static_cast<float>(bounds.getBottom() - 4);
                const float peakY = static_cast<float>(bounds.getY() + 8);
                juce::Path p;
                p.startNewSubPath(static_cast<float>(noteX1), baseY);
                p.lineTo(static_cast<float>(noteX1 + (noteX2 - noteX1) / 3), peakY);
                p.lineTo(static_cast<float>(noteX2), baseY);
                g.strokePath(p, juce::PathStrokeType(1.8f));
            }
        }
        else if (m_expressionMode == ExpressionMode::mpeTimbre)
        {
            for (const auto& note : clip->notes)
            {
                const int noteX1 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
                const int noteX2 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample + note.lengthSamples));
                const bool isSelected = isNoteSelected(note.id);

                g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 0) : juce::Colour::fromRGB(50, 200, 120));
                const float baseY = static_cast<float>(bounds.getBottom() - 4);
                const float sweepY = static_cast<float>(bounds.getY() + 10);
                juce::Path p;
                p.startNewSubPath(static_cast<float>(noteX1), baseY);
                p.lineTo(static_cast<float>(noteX2), sweepY);
                g.strokePath(p, juce::PathStrokeType(1.8f));
                g.fillEllipse(static_cast<float>(noteX2 - 2), sweepY - 2.0f, 5.0f, 5.0f);
            }
        }
        else if (m_expressionMode == ExpressionMode::vocalPitchTune)
        {
            // Center pitch zero-drift line
            const float midY = static_cast<float>(bounds.getY() + bounds.getHeight() / 2);
            g.setColour(juce::Colour::fromRGB(55, 60, 75));
            g.drawHorizontalLine(static_cast<int>(midY), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));

            // Retuned pitch target lines (+/-1 semitone grid)
            g.setColour(juce::Colour::fromRGB(40, 45, 55));
            g.drawHorizontalLine(static_cast<int>(midY - 18.0f), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
            g.drawHorizontalLine(static_cast<int>(midY + 18.0f), static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));

            for (const auto& note : clip->notes)
            {
                const int noteX1 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample));
                const int noteX2 = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(note.startSample + note.lengthSamples));
                const bool isSelected = isNoteSelected(note.id);

                // Draw pitch blob trajectory with vibrato wave and transitions
                juce::Path p;
                const float startY = midY + 10.0f;
                p.startNewSubPath(static_cast<float>(noteX1), startY);

                const int steps = std::max(6, (noteX2 - noteX1) / 4);
                for (int s = 1; s <= steps; ++s)
                {
                    const float t = static_cast<float>(s) / static_cast<float>(steps);
                    const float currX = static_cast<float>(noteX1) + t * static_cast<float>(noteX2 - noteX1);
                    // Natural vocal drift + vibrato curve converging to target pitch
                    const float drift = std::sin(t * 3.14159f) * 6.0f * (1.0f - t);
                    const float vibrato = (t > 0.35f) ? (std::sin(t * 24.0f) * 4.5f) : 0.0f;
                    const float currY = midY + drift + vibrato;
                    p.lineTo(currX, currY);
                }

                // Pitch Blob Halo & Trace
                g.setColour(isSelected ? juce::Colour::fromRGB(255, 170, 0) : juce::Colour::fromRGB(240, 60, 100));
                g.strokePath(p, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                // Note boundary nodules
                g.setColour(juce::Colours::white);
                g.fillEllipse(static_cast<float>(noteX1 - 3), startY - 3.0f, 6.0f, 6.0f);
                g.fillEllipse(static_cast<float>(noteX2 - 3), midY - 3.0f, 6.0f, 6.0f);
            }
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

    if (m_stepRecordEnabled)
    {
        const int stepPx = m_keyWidth + sampleToPixel(static_cast<std::int64_t>(m_stepCursorSample));
        if (stepPx >= bounds.getX() && stepPx <= bounds.getRight())
        {
            g.setColour(juce::Colour::fromRGB(255, 60, 60));
            g.drawVerticalLine(stepPx, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()));
            g.fillRect(stepPx - 3, bounds.getY(), 7, 7);
        }
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
    m_quantizeButton.setBounds(toolbarArea.removeFromLeft(100));
    toolbarArea.removeFromLeft(4);
    m_btnEuclidean.setBounds(toolbarArea.removeFromLeft(80));
    toolbarArea.removeFromLeft(4);
    m_btnMarkov.setBounds(toolbarArea.removeFromLeft(75));
    toolbarArea.removeFromLeft(4);
    m_btnHumanize.setBounds(toolbarArea.removeFromLeft(75));
    toolbarArea.removeFromLeft(4);
    m_btnStepRecord.setBounds(toolbarArea.removeFromLeft(75));

    // MPE & Tuning sub-lane buttons on the right side
    m_btnVocalTune.setBounds(toolbarArea.removeFromRight(80).reduced(2, 0));
    m_btnSlide.setBounds(toolbarArea.removeFromRight(50).reduced(2, 0));
    m_btnVibrato.setBounds(toolbarArea.removeFromRight(60).reduced(2, 0));
    m_btnTimbre.setBounds(toolbarArea.removeFromRight(75).reduced(2, 0));
    m_btnPressure.setBounds(toolbarArea.removeFromRight(65).reduced(2, 0));
    m_btnPitch.setBounds(toolbarArea.removeFromRight(70).reduced(2, 0));
    m_btnVelocity.setBounds(toolbarArea.removeFromRight(40).reduced(2, 0));
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
        const auto pitch = pixelToPitch(e.y);
        m_auditioningPitch = pitch;
        if (m_auditionCallback)
            m_auditionCallback(pitch, 100, true);
        return;
    }

    bool nearRightEdge = false;
    const auto* note = findNoteAt(e.getPosition(), nearRightEdge);

    if (note != nullptr)
    {
        if (m_auditionCallback)
            m_auditionCallback(note->pitch, note->velocity, true);
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
        if (m_auditionCallback)
            m_auditionCallback(pitch, 100, true);
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
    if (m_auditioningPitch.has_value())
    {
        if (m_auditionCallback)
            m_auditionCallback(*m_auditioningPitch, 0, false);
        m_auditioningPitch.reset();
    }
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
    const int code = key.getKeyCode();

    // Octave controls
    if (code == 'Z' || code == 'z')
    {
        m_virtualKeyboardOctave = std::max(0, m_virtualKeyboardOctave - 1);
        return true;
    }
    if (code == 'X' || code == 'x')
    {
        m_virtualKeyboardOctave = std::min(8, m_virtualKeyboardOctave + 1);
        return true;
    }

    // Musical typing keys mapping:
    // A=C, W=C#, S=D, E=D#, D=E, F=F, T=F#, G=G, Y=G#, H=A, U=A#, J=B, K=C+1, O=C#+1, L=D+1
    int noteOffset = -1;
    switch (code)
    {
    case 'A': case 'a': noteOffset = 0; break;
    case 'W': case 'w': noteOffset = 1; break;
    case 'S': case 's': noteOffset = 2; break;
    case 'E': case 'e': noteOffset = 3; break;
    case 'D': case 'd': noteOffset = 4; break;
    case 'F': case 'f': noteOffset = 5; break;
    case 'T': case 't': noteOffset = 6; break;
    case 'G': case 'g': noteOffset = 7; break;
    case 'Y': case 'y': noteOffset = 8; break;
    case 'H': case 'h': noteOffset = 9; break;
    case 'U': case 'u': noteOffset = 10; break;
    case 'J': case 'j': noteOffset = 11; break;
    case 'K': case 'k': noteOffset = 12; break;
    case 'O': case 'o': noteOffset = 13; break;
    case 'L': case 'l': noteOffset = 14; break;
    default: break;
    }

    if (noteOffset >= 0)
    {
        const auto pitch = static_cast<std::uint8_t>(std::clamp(m_virtualKeyboardOctave * 12 + noteOffset, 0, 127));
        if (m_auditionCallback)
            m_auditionCallback(pitch, 100, true);
        return true;
    }

    const auto* clip = findActiveClip();
    if (clip == nullptr || ! m_handler)
        return false;

    if (code == 'Q' || code == 'q')
    {
        quantizeNotes();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && (code == 'A' || code == 'a'))
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
