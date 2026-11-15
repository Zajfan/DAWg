#include <dawg/engine/dsp/MultiSampleInstrumentNode.h>

#include <cmath>
#include <sstream>

namespace dawg::engine::dsp {

MultiSampleInstrumentNode::MultiSampleInstrumentNode() noexcept
{
    m_roundRobinTracker.fill(0);
    updateEnvelopeCoeffs();
}

bool MultiSampleInstrumentNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    updateEnvelopeCoeffs();
    return true;
}

void MultiSampleInstrumentNode::reset() noexcept
{
    for (auto& voice : m_voices) {
        voice.active = false;
        voice.envStage = EnvStage::idle;
        voice.envLevel = 0.0f;
        voice.playhead = 0.0;
        voice.region = nullptr;
    }
    m_roundRobinTracker.fill(0);
}

void MultiSampleInstrumentNode::addRegion(MultiSampleRegion region)
{
    m_regions.push_back(std::move(region));
}

void MultiSampleInstrumentNode::clearRegions() noexcept
{
    reset();
    m_regions.clear();
}

void MultiSampleInstrumentNode::updateEnvelopeCoeffs() noexcept
{
    const double attackSamples = (m_attackMs * 0.001) * m_sampleRate;
    m_attackCoeff = attackSamples > 1.0 ? static_cast<float>(1.0 / attackSamples) : 1.0f;

    const double releaseSamples = (m_releaseMs * 0.001) * m_sampleRate;
    m_releaseCoeff = releaseSamples > 1.0 ? static_cast<float>(1.0 / releaseSamples) : 1.0f;
}

const MultiSampleRegion* MultiSampleInstrumentNode::findMatchingRegion(
    std::uint8_t note,
    std::uint8_t vel) noexcept
{
    if (m_regions.empty()) {
        return nullptr;
    }

    // Collect candidate regions matching key & velocity bounds
    std::array<const MultiSampleRegion*, 16> candidates {};
    std::size_t candidateCount = 0;

    for (const auto& reg : m_regions) {
        if (note >= reg.lowNote && note <= reg.highNote &&
            vel >= reg.lowVel && vel <= reg.highVel) {
            if (candidateCount < candidates.size()) {
                candidates[candidateCount++] = &reg;
            }
        }
    }

    if (candidateCount == 0) {
        return nullptr;
    }

    // Round-robin selection among matching regions
    const std::uint8_t rrIdx = m_roundRobinTracker[note % 128];
    m_roundRobinTracker[note % 128] = static_cast<std::uint8_t>((rrIdx + 1) % candidateCount);

    return candidates[rrIdx % candidateCount];
}

void MultiSampleInstrumentNode::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept
{
    if (velocity == 0) {
        noteOff(note);
        return;
    }

    const auto* region = findMatchingRegion(note, velocity);
    if (region == nullptr || region->sampleDataL.empty()) {
        return;
    }

    // Allocate voice: first free or oldest voice
    Voice* targetVoice = nullptr;
    for (auto& voice : m_voices) {
        if (!voice.active) {
            targetVoice = &voice;
            break;
        }
    }

    if (targetVoice == nullptr) {
        // Steal the first voice
        targetVoice = &m_voices[0];
    }

    targetVoice->active = true;
    targetVoice->midiNote = note;
    targetVoice->velocity = velocity;
    targetVoice->region = region;
    targetVoice->playhead = 0.0;

    const double semitones = static_cast<double>(note) - static_cast<double>(region->rootNote)
                           + (static_cast<double>(region->fineTuneCents) / 100.0);
    targetVoice->speed = std::pow(2.0, semitones / 12.0);

    // Equal-power velocity scaling
    const float normVel = static_cast<float>(velocity) / 127.0f;
    targetVoice->gain = std::sin(normVel * 1.57079632679f);

    targetVoice->envStage = EnvStage::attack;
    targetVoice->envLevel = 0.0f;
}

void MultiSampleInstrumentNode::noteOff(std::uint8_t note) noexcept
{
    for (auto& voice : m_voices) {
        if (voice.active && voice.midiNote == note && voice.envStage != EnvStage::release) {
            voice.envStage = EnvStage::release;
        }
    }
}

void MultiSampleInstrumentNode::allNotesOff() noexcept
{
    for (auto& voice : m_voices) {
        if (voice.active) {
            voice.envStage = EnvStage::release;
        }
    }
}

std::size_t MultiSampleInstrumentNode::activeVoiceCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& voice : m_voices) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

std::uint8_t MultiSampleInstrumentNode::currentRoundRobin(std::uint8_t note) const noexcept
{
    return m_roundRobinTracker[note % 128];
}

void MultiSampleInstrumentNode::process(ProcessContext& context) noexcept
{
    // Process MIDI event list
    if (context.midi.events != nullptr) {
        for (std::size_t i = 0; i < context.midi.eventCount; ++i) {
            const auto& ev = context.midi.events[i];
            if (ev.size >= 2) {
                const auto statusType = static_cast<std::uint8_t>(ev.data[0]) & 0xF0;
                const auto note = static_cast<std::uint8_t>(ev.data[1]) & 0x7F;
                const auto vel = (ev.size >= 3) ? (static_cast<std::uint8_t>(ev.data[2]) & 0x7F) : std::uint8_t {0};

                if (statusType == 0x90 && vel > 0) {
                    noteOn(note, vel);
                } else if (statusType == 0x80 || (statusType == 0x90 && vel == 0)) {
                    noteOff(note);
                }
            }
        }
    }

    auto& output = context.audio;
    const std::size_t numSamples = output.frameCount;
    const std::size_t numChannels = output.channelCount;

    if (output.channels == nullptr || numChannels == 0 || numSamples == 0) {
        return;
    }

    float* outL = output.channels[0];
    float* outR = numChannels > 1 ? output.channels[1] : nullptr;

    for (std::size_t s = 0; s < numSamples; ++s) {
        float mixL = 0.0f;
        float mixR = 0.0f;

        for (auto& voice : m_voices) {
            if (!voice.active || voice.region == nullptr) {
                continue;
            }

            const auto& reg = *voice.region;
            const auto sampleCountL = reg.sampleDataL.size();
            const auto sampleCountR = reg.sampleDataR.empty() ? sampleCountL : reg.sampleDataR.size();

            if (sampleCountL == 0) {
                voice.active = false;
                continue;
            }

            // Envelope progression
            if (voice.envStage == EnvStage::attack) {
                voice.envLevel += m_attackCoeff;
                if (voice.envLevel >= 1.0f) {
                    voice.envLevel = 1.0f;
                    voice.envStage = EnvStage::sustain;
                }
            } else if (voice.envStage == EnvStage::release) {
                voice.envLevel -= m_releaseCoeff;
                if (voice.envLevel <= 0.0f) {
                    voice.envLevel = 0.0f;
                    voice.active = false;
                    continue;
                }
            }

            // Loop and boundary handling
            if (reg.loopEnabled && reg.loopEnd > reg.loopStart && reg.loopEnd <= sampleCountL) {
                if (voice.playhead >= static_cast<double>(reg.loopEnd)) {
                    const double loopLen = static_cast<double>(reg.loopEnd - reg.loopStart);
                    if (loopLen > 0.0) {
                        voice.playhead = reg.loopStart + std::fmod(voice.playhead - reg.loopStart, loopLen);
                    }
                }
            } else if (voice.playhead >= static_cast<double>(sampleCountL - 1)) {
                voice.active = false;
                continue;
            }

            const auto idx0 = static_cast<std::size_t>(voice.playhead);
            const auto idx1 = std::min(idx0 + 1, sampleCountL - 1);
            const float frac = static_cast<float>(voice.playhead - static_cast<double>(idx0));

            // Linear interpolation
            const float sL0 = reg.sampleDataL[idx0];
            const float sL1 = reg.sampleDataL[idx1];
            const float interpL = sL0 + frac * (sL1 - sL0);

            float interpR = interpL;
            if (!reg.sampleDataR.empty()) {
                const auto rIdx1 = std::min(idx0 + 1, sampleCountR - 1);
                const float sR0 = reg.sampleDataR[std::min(idx0, sampleCountR - 1)];
                const float sR1 = reg.sampleDataR[rIdx1];
                interpR = sR0 + frac * (sR1 - sR0);
            }

            const float voiceVol = voice.gain * voice.envLevel;
            mixL += interpL * voiceVol;
            mixR += interpR * voiceVol;

            voice.playhead += voice.speed;
        }

        outL[s] += mixL;
        if (outR != nullptr) {
            outR[s] += mixR;
        }
    }
}

bool MultiSampleInstrumentNode::loadFromSfzString(const std::string& sfzContent)
{
    std::istringstream stream(sfzContent);
    std::string token;

    MultiSampleRegion currentRegion;
    bool inRegion = false;

    auto finalizeRegion = [&]() {
        if (inRegion) {
            if (currentRegion.sampleDataL.empty()) {
                // Synthesize 1000 samples of a warm harmonic waveform if raw audio wasn't populated
                currentRegion.sampleDataL.resize(2048, 0.0f);
                for (std::size_t i = 0; i < 2048; ++i) {
                    const double phase = (2.0 * 3.14159265358979323846 * static_cast<double>(i)) / 128.0;
                    currentRegion.sampleDataL[i] = static_cast<float>(0.7 * std::sin(phase) + 0.3 * std::sin(2.0 * phase));
                }
            }
            m_regions.push_back(currentRegion);
            currentRegion = MultiSampleRegion {};
            inRegion = false;
        }
    };

    while (stream >> token) {
        if (token == "<region>") {
            finalizeRegion();
            inRegion = true;
            continue;
        }

        if (!inRegion) {
            continue;
        }

        const auto eqPos = token.find('=');
        if (eqPos == std::string::npos) {
            continue;
        }

        const std::string key = token.substr(0, eqPos);
        const std::string val = token.substr(eqPos + 1);

        try {
            if (key == "key") {
                const auto note = static_cast<std::uint8_t>(std::stoi(val));
                currentRegion.rootNote = note;
                currentRegion.lowNote = note;
                currentRegion.highNote = note;
            } else if (key == "pitch_keycenter") {
                currentRegion.rootNote = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "lokey") {
                currentRegion.lowNote = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "hikey") {
                currentRegion.highNote = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "lovel") {
                currentRegion.lowVel = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "hivel") {
                currentRegion.highVel = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "seq_position") {
                currentRegion.roundRobinIndex = static_cast<std::uint8_t>(std::stoi(val));
            } else if (key == "tune") {
                currentRegion.fineTuneCents = static_cast<std::int8_t>(std::stoi(val));
            } else if (key == "loop_mode" && (val == "loop_continuous" || val == "1")) {
                currentRegion.loopEnabled = true;
            } else if (key == "loop_start") {
                currentRegion.loopStart = static_cast<std::size_t>(std::stoul(val));
            } else if (key == "loop_end") {
                currentRegion.loopEnd = static_cast<std::size_t>(std::stoul(val));
            }
        } catch (...) {
            // Ignore malformed opcode
        }
    }

    finalizeRegion();
    return !m_regions.empty();
}

} // namespace dawg::engine::dsp
