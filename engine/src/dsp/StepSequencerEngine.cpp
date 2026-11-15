#include <dawg/engine/dsp/StepSequencerEngine.h>

#include <cmath>
#include <numeric>

namespace dawg::engine::dsp {

StepSequencerEngine::StepSequencerEngine() noexcept
{
    reset();
}

void StepSequencerEngine::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void StepSequencerEngine::reset() noexcept
{
    m_sampleAccumulator = 0.0;
    m_loopIteration = 0;

    for (std::size_t t = 0; t < maxTracks; ++t) {
        auto& trk = m_tracks[t];
        trk.stepCount = 16;
        trk.pulseCount = 4;
        trk.rotation = 0;
        trk.midiNote = static_cast<std::uint8_t>(36 + t); // 36 = C1 Kick, 37 = SideStick, 38 = Snare...
        trk.midiChannel = 9; // General MIDI Drum Channel 10 (0-indexed = 9)
        trk.currentStep = 0;

        for (auto& st : trk.steps) {
            st = StepData {};
        }
    }
}

void StepSequencerEngine::setTrackMidiNote(std::size_t trackIdx, std::uint8_t note, std::uint8_t channel) noexcept
{
    if (trackIdx < maxTracks) {
        m_tracks[trackIdx].midiNote = note;
        m_tracks[trackIdx].midiChannel = channel;
    }
}

void StepSequencerEngine::setTrackStepCount(std::size_t trackIdx, std::size_t stepCount) noexcept
{
    if (trackIdx < maxTracks) {
        m_tracks[trackIdx].stepCount = std::clamp(stepCount, std::size_t {1}, SequencerTrack::maxSteps);
        if (m_tracks[trackIdx].currentStep >= m_tracks[trackIdx].stepCount) {
            m_tracks[trackIdx].currentStep = 0;
        }
    }
}

std::size_t StepSequencerEngine::trackStepCount(std::size_t trackIdx) const noexcept
{
    if (trackIdx < maxTracks) {
        return m_tracks[trackIdx].stepCount;
    }
    return 16;
}

void StepSequencerEngine::setStep(std::size_t trackIdx, std::size_t stepIdx, const StepData& step) noexcept
{
    if (trackIdx < maxTracks && stepIdx < SequencerTrack::maxSteps) {
        m_tracks[trackIdx].steps[stepIdx] = step;
    }
}

const StepData& StepSequencerEngine::step(std::size_t trackIdx, std::size_t stepIdx) const noexcept
{
    static const StepData kDefaultStep {};
    if (trackIdx < maxTracks && stepIdx < SequencerTrack::maxSteps) {
        return m_tracks[trackIdx].steps[stepIdx];
    }
    return kDefaultStep;
}

void StepSequencerEngine::setStepActive(std::size_t trackIdx, std::size_t stepIdx, bool active) noexcept
{
    if (trackIdx < maxTracks && stepIdx < SequencerTrack::maxSteps) {
        m_tracks[trackIdx].steps[stepIdx].active = active;
    }
}

std::vector<bool> StepSequencerEngine::generateEuclideanPattern(
    std::size_t steps,
    std::size_t pulses,
    std::size_t rotation)
{
    if (steps == 0) {
        return {};
    }
    if (pulses == 0) {
        return std::vector<bool>(steps, false);
    }
    if (pulses >= steps) {
        return std::vector<bool>(steps, true);
    }

    std::vector<bool> pattern(steps, false);
    for (std::size_t i = 0; i < steps; ++i) {
        // Closed-form Bresenham-Bjorklund Euclidean distribution
        const bool active = ((i * pulses) % steps) < pulses;
        pattern[(i + rotation) % steps] = active;
    }

    return pattern;
}

void StepSequencerEngine::applyEuclideanPattern(
    std::size_t trackIdx,
    std::size_t steps,
    std::size_t pulses,
    std::size_t rotation,
    std::uint8_t velocity)
{
    if (trackIdx >= maxTracks) {
        return;
    }

    const auto pattern = generateEuclideanPattern(steps, pulses, rotation);
    auto& trk = m_tracks[trackIdx];
    trk.stepCount = std::min(steps, SequencerTrack::maxSteps);
    trk.pulseCount = pulses;
    trk.rotation = rotation;

    for (std::size_t i = 0; i < trk.stepCount; ++i) {
        trk.steps[i].active = pattern[i];
        trk.steps[i].velocity = pattern[i] ? velocity : static_cast<std::uint8_t>(0);
    }
}

bool StepSequencerEngine::shouldTriggerStep(const StepData& stepData) const noexcept
{
    if (!stepData.active) {
        return false;
    }

    // Probability evaluation (pseudo-random LCG for determinism and real-time safety)
    if (stepData.probability < 1.0f) {
        static std::uint32_t seed = 987654321;
        seed = seed * 1664525u + 1013904223u;
        const float rnd = static_cast<float>(seed & 0xFFFF) / 65536.0f;
        if (rnd > stepData.probability) {
            return false;
        }
    }

    // Condition evaluation
    switch (stepData.condition) {
    case StepCondition::none:
        return true;
    case StepCondition::pass1of2:
        return (m_loopIteration % 2) == 0;
    case StepCondition::pass2of2:
        return (m_loopIteration % 2) == 1;
    case StepCondition::pass1of4:
        return (m_loopIteration % 4) == 0;
    case StepCondition::pass2of4:
        return (m_loopIteration % 4) == 1;
    case StepCondition::pass3of4:
        return (m_loopIteration % 4) == 2;
    case StepCondition::pass4of4:
        return (m_loopIteration % 4) == 3;
    case StepCondition::firstLoop:
        return m_loopIteration == 0;
    case StepCondition::notFirstLoop:
        return m_loopIteration > 0;
    case StepCondition::fillOnly:
        return m_fillMode;
    case StepCondition::notFill:
        return !m_fillMode;
    }

    return true;
}

void StepSequencerEngine::advance(
    std::size_t sampleFrames,
    double bpm,
    std::vector<MidiEvent>& outputMidi)
{
    m_bpm = bpm > 0.0 ? bpm : m_bpm;
    const double samplesPer16th = (15.0 / m_bpm) * m_sampleRate;

    m_sampleAccumulator += static_cast<double>(sampleFrames);

    while (m_sampleAccumulator >= samplesPer16th) {
        m_sampleAccumulator -= samplesPer16th;

        for (std::size_t t = 0; t < maxTracks; ++t) {
            auto& trk = m_tracks[t];
            const std::size_t curStep = trk.currentStep;
            const auto& stepData = trk.steps[curStep];

            if (shouldTriggerStep(stepData)) {
                int noteNum = static_cast<int>(trk.midiNote) + static_cast<int>(std::round(stepData.pitchOffsetSemitones));
                noteNum = std::clamp(noteNum, 0, 127);
                const auto finalNote = static_cast<std::uint8_t>(noteNum);
                const std::uint8_t vel = stepData.velocity;

                // Micro-timing offset
                std::size_t offset = 0;
                if (stepData.microTimingPercent != 0.0f) {
                    const double nudgeSamples = (static_cast<double>(stepData.microTimingPercent) / 100.0) * samplesPer16th;
                    if (nudgeSamples > 0.0) {
                        offset = static_cast<std::size_t>(nudgeSamples);
                    }
                }
                if (offset >= sampleFrames) {
                    offset = sampleFrames > 0 ? sampleFrames - 1 : 0;
                }

                // Ratchets
                const std::uint8_t ratchets = std::clamp(stepData.ratchets, std::uint8_t {1}, std::uint8_t {4});
                for (std::uint8_t r = 0; r < ratchets; ++r) {
                    std::size_t rOffset = offset + static_cast<std::size_t>((r * samplesPer16th) / (ratchets * 4));
                    if (rOffset >= sampleFrames) {
                        rOffset = sampleFrames > 0 ? sampleFrames - 1 : 0;
                    }

                    MidiEvent ev {};
                    ev.frameOffset = rOffset;
                    ev.size = 3;
                    ev.data[0] = static_cast<std::byte>(0x90 | (trk.midiChannel & 0x0F));
                    ev.data[1] = static_cast<std::byte>(finalNote);
                    ev.data[2] = static_cast<std::byte>(vel);
                    outputMidi.push_back(ev);
                }
            }

            trk.currentStep = (trk.currentStep + 1) % trk.stepCount;
            if (t == 0 && trk.currentStep == 0) {
                ++m_loopIteration;
            }
        }
    }
}

} // namespace dawg::engine::dsp
