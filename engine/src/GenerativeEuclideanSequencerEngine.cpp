#include "dawg/engine/GenerativeEuclideanSequencerEngine.h"

#include <algorithm>
#include <cmath>

namespace dawg::engine {

GenerativeEuclideanSequencerEngine::GenerativeEuclideanSequencerEngine() noexcept
{
    for (std::size_t i = 0; i < MaxLanes; ++i) {
        m_lanes[i].config.steps = 16;
        m_lanes[i].config.pulses = 4;
        m_lanes[i].config.rotation = 0;
        m_lanes[i].config.note = static_cast<uint8_t>(36 + i * 2); // default MIDI mapping
        m_lanes[i].config.channel = 0;
        m_lanes[i].config.velocity = 0.8f;
        m_lanes[i].config.probability = 1.0f;
        m_lanes[i].config.ratchet = 1;
        m_lanes[i].config.gateLength = 0.75f;
        m_lanes[i].config.active = true;
        refreshLanePattern(i);
    }
}

std::vector<bool> GenerativeEuclideanSequencerEngine::computeBjorklundPattern(std::size_t pulses, std::size_t steps) noexcept
{
    if (steps == 0) {
        return {};
    }
    std::vector<bool> pattern(steps, false);
    if (pulses == 0) {
        return pattern;
    }
    if (pulses >= steps) {
        std::fill(pattern.begin(), pattern.end(), true);
        return pattern;
    }

    // Bjorklund / Bresenham Euclidean distribution
    for (std::size_t i = 0; i < steps; ++i) {
        pattern[i] = ((i * pulses) % steps) < pulses;
    }
    return pattern;
}

void GenerativeEuclideanSequencerEngine::configureLane(std::size_t laneIndex, const EuclideanLaneConfig& config) noexcept
{
    if (laneIndex >= MaxLanes) {
        return;
    }
    m_lanes[laneIndex].config = config;
    m_lanes[laneIndex].config.steps = std::clamp<std::size_t>(config.steps, 1, MaxStepsPerLane);
    m_lanes[laneIndex].config.pulses = std::min(config.pulses, m_lanes[laneIndex].config.steps);
    m_lanes[laneIndex].config.ratchet = std::clamp<uint8_t>(config.ratchet, 1, 4);
    refreshLanePattern(laneIndex);
}

const GenerativeEuclideanSequencerEngine::LaneState& GenerativeEuclideanSequencerEngine::lane(std::size_t laneIndex) const noexcept
{
    const std::size_t safeIdx = std::min(laneIndex, MaxLanes - 1);
    return m_lanes[safeIdx];
}

void GenerativeEuclideanSequencerEngine::refreshLanePattern(std::size_t laneIndex) noexcept
{
    if (laneIndex >= MaxLanes) {
        return;
    }
    auto& l = m_lanes[laneIndex];
    const auto raw = computeBjorklundPattern(l.config.pulses, l.config.steps);
    l.pattern.fill(false);
    for (std::size_t i = 0; i < l.config.steps; ++i) {
        const std::size_t rotatedIdx = (i + l.config.rotation) % l.config.steps;
        l.pattern[i] = raw[rotatedIdx];
    }
}

void GenerativeEuclideanSequencerEngine::reset() noexcept
{
    for (auto& lane : m_lanes) {
        lane.currentStep = 0;
        lane.stepSampleAccumulator = 0.0;
        lane.noteOffCountdownSamples = 0;
        lane.isNoteActive = false;
        lane.firstStepPending = true;
    }
}

float GenerativeEuclideanSequencerEngine::randomFloat() noexcept
{
    m_prngState ^= (m_prngState << 13);
    m_prngState ^= (m_prngState >> 17);
    m_prngState ^= (m_prngState << 5);
    return static_cast<float>(m_prngState & 0x00FFFFFF) / static_cast<float>(0x01000000);
}

std::size_t GenerativeEuclideanSequencerEngine::process(
    std::size_t numSamples,
    double tempoBpm,
    double sampleRate,
    std::span<MidiEvent> outEvents) noexcept
{
    if (outEvents.empty() || numSamples == 0 || sampleRate <= 0.0) {
        return 0;
    }

    std::size_t eventsWritten = 0;
    const double validBpm = std::clamp(tempoBpm, 20.0, 400.0);
    const double samplesPerBeat = sampleRate * 60.0 / validBpm;
    const double samplesPer16th = samplesPerBeat / 4.0;

    for (auto& lane : m_lanes) {
        if (!lane.config.active) {
            continue;
        }

        // Process active note-off countdown
        if (lane.isNoteActive) {
            lane.noteOffCountdownSamples -= static_cast<int>(numSamples);
            if (lane.noteOffCountdownSamples <= 0) {
                if (eventsWritten < outEvents.size()) {
                    auto& ev = outEvents[eventsWritten++];
                    ev.frameOffset = 0;
                    ev.size = 3;
                    ev.data[0] = static_cast<std::byte>(0x80 | (lane.config.channel & 0x0F));
                    ev.data[1] = static_cast<std::byte>(lane.activeNote & 0x7F);
                    ev.data[2] = static_cast<std::byte>(0);
                }
                lane.isNoteActive = false;
            }
        }

        // Trigger step 0 immediately on transport start
        if (lane.firstStepPending) {
            lane.firstStepPending = false;
            if (lane.pattern[lane.currentStep]) {
                const float roll = randomFloat();
                if (roll <= lane.config.probability) {
                    const uint8_t quantizedPitch = quantizeToScale(lane.config.note);
                    const auto velByte = static_cast<uint8_t>(
                        std::clamp(std::round(lane.config.velocity * 127.0f), 1.0f, 127.0f));
                    const uint8_t ratchets = std::clamp<uint8_t>(lane.config.ratchet, 1, 4);

                    for (uint8_t r = 0; r < ratchets && eventsWritten < outEvents.size(); ++r) {
                        auto& ev = outEvents[eventsWritten++];
                        const double offsetFraction = static_cast<double>(r) / static_cast<double>(ratchets);
                        ev.frameOffset = static_cast<std::size_t>(offsetFraction * samplesPer16th);
                        ev.size = 3;
                        ev.data[0] = static_cast<std::byte>(0x90 | (lane.config.channel & 0x0F));
                        ev.data[1] = static_cast<std::byte>(quantizedPitch & 0x7F);
                        ev.data[2] = static_cast<std::byte>(velByte);
                    }

                    lane.isNoteActive = true;
                    lane.activeNote = quantizedPitch;
                    lane.noteOffCountdownSamples = static_cast<int>(samplesPer16th * lane.config.gateLength);
                }
            }
            lane.currentStep = (lane.currentStep + 1) % lane.config.steps;
        }

        // Advance 16th-note clock accumulator
        lane.stepSampleAccumulator += static_cast<double>(numSamples);
        while (lane.stepSampleAccumulator >= samplesPer16th) {
            lane.stepSampleAccumulator -= samplesPer16th;

            if (lane.pattern[lane.currentStep]) {
                const float roll = randomFloat();
                if (roll <= lane.config.probability) {
                    const uint8_t quantizedPitch = quantizeToScale(lane.config.note);
                    const auto velByte = static_cast<uint8_t>(
                        std::clamp(std::round(lane.config.velocity * 127.0f), 1.0f, 127.0f));
                    const uint8_t ratchets = std::clamp<uint8_t>(lane.config.ratchet, 1, 4);

                    for (uint8_t r = 0; r < ratchets && eventsWritten < outEvents.size(); ++r) {
                        auto& ev = outEvents[eventsWritten++];
                        const double offsetFraction = static_cast<double>(r) / static_cast<double>(ratchets);
                        ev.frameOffset = static_cast<std::size_t>(offsetFraction * samplesPer16th);
                        ev.size = 3;
                        ev.data[0] = static_cast<std::byte>(0x90 | (lane.config.channel & 0x0F));
                        ev.data[1] = static_cast<std::byte>(quantizedPitch & 0x7F);
                        ev.data[2] = static_cast<std::byte>(velByte);
                    }

                    lane.isNoteActive = true;
                    lane.activeNote = quantizedPitch;
                    lane.noteOffCountdownSamples = static_cast<int>(samplesPer16th * lane.config.gateLength);
                }
            }

            lane.currentStep = (lane.currentStep + 1) % lane.config.steps;
        }
    }

    return eventsWritten;
}

uint8_t GenerativeEuclideanSequencerEngine::nextMarkovChordDegree(uint8_t currentDegree) noexcept
{
    // Classical / Pop Functional Harmony Transition Matrix
    // Degrees: 0: I, 1: ii, 2: iii, 3: IV, 4: V, 5: vi, 6: vii°
    static constexpr std::array<std::array<float, 7>, 7> MarkovMatrix {{
        {0.05f, 0.15f, 0.10f, 0.35f, 0.25f, 0.10f, 0.00f}, // I -> IV, V, ii, vi, iii
        {0.10f, 0.05f, 0.00f, 0.05f, 0.60f, 0.10f, 0.10f}, // ii -> V (predominant to dominant)
        {0.20f, 0.00f, 0.05f, 0.25f, 0.00f, 0.50f, 0.00f}, // iii -> vi, IV, I
        {0.25f, 0.15f, 0.00f, 0.05f, 0.50f, 0.00f, 0.05f}, // IV -> V, I, ii
        {0.70f, 0.00f, 0.00f, 0.05f, 0.05f, 0.20f, 0.00f}, // V -> I, vi
        {0.10f, 0.35f, 0.05f, 0.30f, 0.15f, 0.05f, 0.00f}, // vi -> ii, IV, V
        {0.80f, 0.00f, 0.05f, 0.00f, 0.00f, 0.15f, 0.00f}  // vii° -> I, vi
    }};

    const std::size_t deg = currentDegree % 7;
    const float roll = randomFloat();
    float cumulative = 0.0f;

    for (std::size_t target = 0; target < 7; ++target) {
        cumulative += MarkovMatrix[deg][target];
        if (roll <= cumulative) {
            return static_cast<uint8_t>(target);
        }
    }
    return 0; // Default to tonic (I)
}

uint8_t GenerativeEuclideanSequencerEngine::quantizeToScale(uint8_t midiNote) const noexcept
{
    // Pitch class semitone intervals according to dsp::MusicalScale
    // chromatic, major, minor, harmonicMinor, dorian, mixolydian, pentatonicMinor, pentatonicMajor
    static constexpr std::array<std::array<uint8_t, 7>, 8> ScaleIntervals {{
        {0, 1, 2, 3, 4, 5, 6}, // chromatic (partial)
        {0, 2, 4, 5, 7, 9, 11}, // major
        {0, 2, 3, 5, 7, 8, 10}, // minor
        {0, 2, 3, 5, 7, 8, 11}, // harmonicMinor
        {0, 2, 3, 5, 7, 9, 10}, // dorian
        {0, 2, 4, 5, 7, 9, 10}, // mixolydian
        {0, 3, 5, 7, 10, 10, 10}, // pentatonicMinor (padded)
        {0, 2, 4, 7, 9, 9, 9}     // pentatonicMajor (padded)
    }};

    const auto scaleIdx = static_cast<std::size_t>(m_scale);
    const auto& intervals = ScaleIntervals[std::min(scaleIdx, ScaleIntervals.size() - 1)];

    const int noteClass = (static_cast<int>(midiNote) - static_cast<int>(m_rootNote)) % 12;
    const int positiveClass = (noteClass + 12) % 12;

    int minDiff = 99;
    int bestInterval = 0;
    for (uint8_t interval : intervals) {
        const int diff = std::abs(positiveClass - static_cast<int>(interval));
        if (diff < minDiff) {
            minDiff = diff;
            bestInterval = interval;
        }
    }

    const int octave = (static_cast<int>(midiNote) - positiveClass);
    const int quantized = octave + static_cast<int>(m_rootNote) + bestInterval;
    return static_cast<uint8_t>(std::clamp(quantized, 0, 127));
}

} // namespace dawg::engine
