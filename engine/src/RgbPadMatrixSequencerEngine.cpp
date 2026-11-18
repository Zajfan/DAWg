#include <dawg/engine/RgbPadMatrixSequencerEngine.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

namespace dawg::engine {

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi_v<double>;

// Scale intervals (semitones from root)
const std::vector<int>& getScaleIntervals(MelodicScaleType type) noexcept
{
    static const std::vector<int> majorIntervals           = {0, 2, 4, 5, 7, 9, 11};
    static const std::vector<int> naturalMinorIntervals    = {0, 2, 3, 5, 7, 8, 10};
    static const std::vector<int> dorianIntervals          = {0, 2, 3, 5, 7, 9, 10};
    static const std::vector<int> phrygianIntervals        = {0, 1, 3, 5, 7, 8, 10};
    static const std::vector<int> lydianIntervals          = {0, 2, 4, 6, 7, 9, 11};
    static const std::vector<int> mixolydianIntervals      = {0, 2, 4, 5, 7, 9, 10};
    static const std::vector<int> harmonicMinorIntervals   = {0, 2, 3, 5, 7, 8, 11};
    static const std::vector<int> pentatonicMajorIntervals = {0, 2, 4, 7, 9};
    static const std::vector<int> pentatonicMinorIntervals = {0, 3, 5, 7, 10};
    static const std::vector<int> bluesIntervals           = {0, 3, 5, 6, 7, 10};

    switch (type)
    {
        case MelodicScaleType::major:           return majorIntervals;
        case MelodicScaleType::naturalMinor:    return naturalMinorIntervals;
        case MelodicScaleType::dorian:          return dorianIntervals;
        case MelodicScaleType::phrygian:        return phrygianIntervals;
        case MelodicScaleType::lydian:          return lydianIntervals;
        case MelodicScaleType::mixolydian:      return mixolydianIntervals;
        case MelodicScaleType::harmonicMinor:   return harmonicMinorIntervals;
        case MelodicScaleType::pentatonicMajor: return pentatonicMajorIntervals;
        case MelodicScaleType::pentatonicMinor: return pentatonicMinorIntervals;
        case MelodicScaleType::blues:           return bluesIntervals;
    }
    return naturalMinorIntervals;
}

} // namespace

RgbPadMatrixSequencerEngine::RgbPadMatrixSequencerEngine() noexcept
{
    reset();
}

void RgbPadMatrixSequencerEngine::reset() noexcept
{
    for (std::size_t r = 0; r < matrixRows; ++r)
    {
        for (std::size_t c = 0; c < matrixCols; ++c)
        {
            m_padStates[r][c] = PadLedState::off;
            m_padBaseColors[r][c] = 0;
            m_lastSentVelocity[r][c] = 255; // Force update on first render
        }
    }

    for (std::size_t i = 0; i < maxSteps; ++i)
    {
        m_steps[i].active = false;
        m_steps[i].noteNumber = 60;
        m_steps[i].velocity = 100;
        m_steps[i].probability = 1.0f;
        m_steps[i].ratchetCount = 1;
    }
    m_currentPlayheadStep = 0;
}

void RgbPadMatrixSequencerEngine::setStepSequencerLength(std::size_t steps) noexcept
{
    m_stepSequencerLength = std::clamp(steps, std::size_t{1}, maxSteps);
    if (m_currentPlayheadStep >= m_stepSequencerLength)
    {
        m_currentPlayheadStep = 0;
    }
}

void RgbPadMatrixSequencerEngine::setStep(std::size_t index, const HardwareStep& step) noexcept
{
    if (index < maxSteps)
    {
        m_steps[index] = step;
    }
}

const HardwareStep& RgbPadMatrixSequencerEngine::step(std::size_t index) const noexcept
{
    static const HardwareStep defaultStep {};
    if (index < maxSteps)
    {
        return m_steps[index];
    }
    return defaultStep;
}

void RgbPadMatrixSequencerEngine::setPadState(std::size_t row, std::size_t col, PadLedState state, std::uint8_t baseVelocityColor) noexcept
{
    if (row < matrixRows && col < matrixCols)
    {
        m_padStates[row][col] = state;
        m_padBaseColors[row][col] = baseVelocityColor;
    }
}

PadLedState RgbPadMatrixSequencerEngine::padState(std::size_t row, std::size_t col) const noexcept
{
    if (row < matrixRows && col < matrixCols)
    {
        return m_padStates[row][col];
    }
    return PadLedState::off;
}

std::uint8_t RgbPadMatrixSequencerEngine::padToMidiNote(std::size_t row, std::size_t col) const noexcept
{
    switch (m_controllerType)
    {
        case ControllerGridType::launchpadProX:
            // Programmer mode: row 0 (bottom) = notes 11..18, row 7 (top) = notes 81..88
            return static_cast<std::uint8_t>((row + 1) * 10 + (col + 1));
        case ControllerGridType::push2:
            // Push layout: row 0 (bottom) = 36..43, row 7 (top) = 92..99
            return static_cast<std::uint8_t>(36 + row * 8 + col);
        case ControllerGridType::generic8x8:
            return static_cast<std::uint8_t>(row * 8 + col);
    }
    return 0;
}

bool RgbPadMatrixSequencerEngine::midiNoteToPad(std::uint8_t note, std::size_t& row, std::size_t& col) const noexcept
{
    switch (m_controllerType)
    {
        case ControllerGridType::launchpadProX:
        {
            const int r = (static_cast<int>(note) / 10) - 1;
            const int c = (static_cast<int>(note) % 10) - 1;
            if (r >= 0 && r < static_cast<int>(matrixRows) && c >= 0 && c < static_cast<int>(matrixCols))
            {
                row = static_cast<std::size_t>(r);
                col = static_cast<std::size_t>(c);
                return true;
            }
            return false;
        }
        case ControllerGridType::push2:
        {
            if (note >= 36 && note < 100)
            {
                const auto offset = static_cast<std::size_t>(note - 36);
                row = offset / 8;
                col = offset % 8;
                return true;
            }
            return false;
        }
        case ControllerGridType::generic8x8:
        {
            if (note < totalPads)
            {
                row = static_cast<std::size_t>(note) / 8;
                col = static_cast<std::size_t>(note) % 8;
                return true;
            }
            return false;
        }
    }
    return false;
}

int RgbPadMatrixSequencerEngine::padToMelodicNote(std::size_t row, std::size_t col) const noexcept
{
    const auto& intervals = getScaleIntervals(m_scaleType);
    const int scaleSize = static_cast<int>(intervals.size());
    if (scaleSize == 0) return 60;

    // Standard 4th offset (3 scale degrees per row up)
    const int scaleDegreeIndex = static_cast<int>(row) * 3 + static_cast<int>(col);
    const int octaves = scaleDegreeIndex / scaleSize;
    const int degInScale = scaleDegreeIndex % scaleSize;

    const int note = (m_baseOctave + octaves) * 12 + m_rootNote + intervals[static_cast<std::size_t>(degInScale)];
    return std::clamp(note, 0, 127);
}

bool RgbPadMatrixSequencerEngine::isNoteInScale(int midiNote) const noexcept
{
    const auto& intervals = getScaleIntervals(m_scaleType);
    const int pitchClass = ((midiNote % 12) - m_rootNote + 12) % 12;
    return std::find(intervals.begin(), intervals.end(), pitchClass) != intervals.end();
}

bool RgbPadMatrixSequencerEngine::isRootNote(int midiNote) const noexcept
{
    const int pitchClass = (midiNote % 12 + 12) % 12;
    return pitchClass == m_rootNote;
}

PadEventResult RgbPadMatrixSequencerEngine::processMidiIn(const MidiEvent& event) noexcept
{
    PadEventResult result {};
    result.viewMode = m_viewMode;

    if (event.size < 3)
    {
        return result;
    }

    const auto status = static_cast<std::uint8_t>(event.data[0]) & 0xF0;
    const auto note = static_cast<std::uint8_t>(event.data[1]);
    const auto vel = static_cast<std::uint8_t>(event.data[2]);

    std::size_t row = 0;
    std::size_t col = 0;
    if (!midiNoteToPad(note, row, col))
    {
        return result;
    }

    result.padRow = static_cast<int>(row);
    result.padCol = static_cast<int>(col);
    result.velocity = static_cast<float>(vel) / 127.0f;
    result.isNoteOn = (status == 0x90 && vel > 0);
    result.isNoteOff = (status == 0x80 || (status == 0x90 && vel == 0));
    result.handled = true;

    switch (m_viewMode)
    {
        case GridPadViewMode::sessionClips:
        {
            if (result.isNoteOn)
            {
                result.launchedClipTrack = col;
                // In session mode, row 7 is scene 0 (top) or row 0 is scene 0
                result.launchedClipScene = (matrixRows - 1) - row;
            }
            break;
        }
        case GridPadViewMode::inKeyMelody:
        {
            result.noteNumber = padToMelodicNote(row, col);
            break;
        }
        case GridPadViewMode::chromaticMelody:
        {
            // 4ths chromatic layout (5 semitones per row)
            result.noteNumber = std::clamp(static_cast<int>(m_baseOctave * 12 + row * 5 + col), 0, 127);
            break;
        }
        case GridPadViewMode::stepSequencer:
        {
            // Rows 0 and 1 represent 16 sequencer steps
            if (row < 2 && result.isNoteOn)
            {
                const std::size_t stepIdx = row * 8 + col;
                if (stepIdx < m_stepSequencerLength)
                {
                    m_steps[stepIdx].active = !m_steps[stepIdx].active;
                    m_steps[stepIdx].velocity = vel;
                }
            }
            break;
        }
    }

    return result;
}

std::uint8_t RgbPadMatrixSequencerEngine::computeLedVelocity(
    std::size_t row,
    std::size_t col,
    double timeSeconds,
    double beatPosition) const noexcept
{
    switch (m_viewMode)
    {
        case GridPadViewMode::sessionClips:
        {
            const auto state = m_padStates[row][col];
            const auto baseCol = m_padBaseColors[row][col];

            switch (state)
            {
                case PadLedState::off:
                    return 0;
                case PadLedState::dimColor:
                    return (baseCol != 0) ? baseCol : 1; // Dim color
                case PadLedState::brightColor:
                    return (baseCol != 0) ? static_cast<std::uint8_t>(baseCol + 2) : 3; // Bright
                case PadLedState::playingPulse:
                {
                    // Pulse between velocity 21 and 23
                    const double phase = std::fmod(beatPosition, 1.0);
                    const bool bright = (phase < 0.5);
                    return bright ? 21 : 23; // Green pulsing
                }
                case PadLedState::queuedFlash:
                {
                    // Fast flash (4 Hz)
                    const bool on = (std::fmod(timeSeconds * 4.0, 1.0) < 0.5);
                    return on ? 3 : 0; // White flash
                }
                case PadLedState::recordingRed:
                {
                    // Red solid or slow pulse
                    const bool on = (std::fmod(beatPosition, 1.0) < 0.8);
                    return on ? 72 : 5; // Red bright vs dim
                }
                case PadLedState::mutedAmber:
                    return 13; // Amber
            }
            break;
        }
        case GridPadViewMode::inKeyMelody:
        {
            const int note = padToMelodicNote(row, col);
            if (isRootNote(note))
            {
                return 37; // Cyan / Blue for Root Notes
            }
            return 3; // Soft White for In-Key Notes
        }
        case GridPadViewMode::chromaticMelody:
        {
            const int note = m_baseOctave * 12 + static_cast<int>(row) * 5 + static_cast<int>(col);
            if (isRootNote(note))
            {
                return 37; // Cyan / Blue for Root Notes
            }
            if (isNoteInScale(note))
            {
                return 3; // Soft White for In-Scale Notes
            }
            return 1; // Dim Dark Gray for Out-of-Scale Notes
        }
        case GridPadViewMode::stepSequencer:
        {
            if (row < 2)
            {
                const std::size_t stepIdx = row * 8 + col;
                if (stepIdx < m_stepSequencerLength)
                {
                    if (stepIdx == m_currentPlayheadStep)
                    {
                        return 3; // White Playhead Cursor
                    }
                    if (m_steps[stepIdx].active)
                    {
                        return (m_steps[stepIdx].probability < 0.99f) ? 13 : 21; // Yellow if probability, else Green
                    }
                    return 1; // Dim Dark Gray for Inactive Step
                }
                return 0; // Out of sequence length
            }
            return 0; // Unused rows
        }
    }
    return 0;
}

std::size_t RgbPadMatrixSequencerEngine::renderLedUpdates(
    MidiEvent* outEvents,
    std::size_t maxOutEvents,
    double timeSeconds,
    double beatPosition) noexcept
{
    if (!outEvents || maxOutEvents == 0)
    {
        return 0;
    }

    std::size_t eventCount = 0;

    for (std::size_t r = 0; r < matrixRows; ++r)
    {
        for (std::size_t c = 0; c < matrixCols; ++c)
        {
            if (eventCount >= maxOutEvents)
            {
                return eventCount;
            }

            const std::uint8_t targetVel = computeLedVelocity(r, c, timeSeconds, beatPosition);
            if (targetVel != m_lastSentVelocity[r][c])
            {
                m_lastSentVelocity[r][c] = targetVel;

                const std::uint8_t note = padToMidiNote(r, c);
                auto& ev = outEvents[eventCount++];
                ev.frameOffset = 0;
                ev.size = 3;
                ev.data[0] = static_cast<std::byte>(0x90); // Note On ch 0
                ev.data[1] = static_cast<std::byte>(note);
                ev.data[2] = static_cast<std::byte>(targetVel);
            }
        }
    }

    return eventCount;
}

} // namespace dawg::engine
