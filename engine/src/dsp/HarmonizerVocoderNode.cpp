#include <dawg/engine/dsp/HarmonizerVocoderNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr std::array<float, HarmonizerVocoderNode::vocoderBandCount> kVocoderCenterFreqs = {
    150.0f,  190.0f,  240.0f,  300.0f,  380.0f,  475.0f,  590.0f,  730.0f,
    900.0f, 1100.0f, 1330.0f, 1600.0f, 1900.0f, 2250.0f, 2650.0f, 3100.0f,
    3600.0f, 4150.0f, 4750.0f, 5400.0f, 6100.0f, 6850.0f, 7650.0f, 8500.0f,
    9400.0f, 10350.0f, 11350.0f, 12400.0f, 13500.0f, 14650.0f, 15850.0f, 17100.0f
};

inline void computeBandpassCoeffs(
    float freqHz,
    float q,
    double sampleRate,
    HarmonizerVocoderNode::BiquadBand& band) noexcept
{
    const float clampedFreq = std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float w0 = 2.0f * std::numbers::pi_v<float> * clampedFreq / static_cast<float>(sampleRate);
    const float alpha = std::sin(w0) / (2.0f * q);

    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * std::cos(w0);
    const float a2 = 1.0f - alpha;

    band.b0 = b0 / a0;
    band.b1 = b1 / a0;
    band.b2 = b2 / a0;
    band.a1 = a1 / a0;
    band.a2 = a2 / a0;
}

} // namespace

HarmonizerVocoderNode::HarmonizerVocoderNode() noexcept
{
    // Default 4-part vocal harmony arrangement
    m_voices[0] = HarmonyVoice {.enabled = true, .semitones = 4.0f, .fineTuneCents = 0.0f, .gain = 0.7f, .pan = -0.4f, .formantMorph = 1.0f};
    m_voices[1] = HarmonyVoice {.enabled = true, .semitones = 7.0f, .fineTuneCents = 0.0f, .gain = 0.7f, .pan = 0.4f, .formantMorph = 1.0f};
    m_voices[2] = HarmonyVoice {.enabled = false, .semitones = -12.0f, .fineTuneCents = 0.0f, .gain = 0.5f, .pan = 0.0f, .formantMorph = 0.85f};
    m_voices[3] = HarmonyVoice {.enabled = false, .semitones = 12.0f, .fineTuneCents = 0.0f, .gain = 0.4f, .pan = 0.0f, .formantMorph = 1.15f};

    m_pitchRingL.fill(0.0f);
    m_pitchRingR.fill(0.0f);
    m_bandEnvelopes.fill(0.0f);
}

bool HarmonizerVocoderNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    updateVocoderFilters();
    updateFormantFilters();
    return true;
}

void HarmonizerVocoderNode::reset() noexcept
{
    m_pitchRingL.fill(0.0f);
    m_pitchRingR.fill(0.0f);
    m_pitchWritePos = 0;

    for (auto& st : m_voiceStates) {
        st.playheadA = 0.0;
        st.playheadB = 0.0;
        st.grainPhase = 0.0f;
        st.formantFilterL.reset();
        st.formantFilterR.reset();
    }

    for (auto& f : m_modulatorFilters) f.reset();
    for (auto& f : m_carrierFiltersL) f.reset();
    for (auto& f : m_carrierFiltersR) f.reset();
    m_bandEnvelopes.fill(0.0f);
    m_carrierPhase = 0.0f;
}

void HarmonizerVocoderNode::setVoice(std::size_t voiceIdx, const HarmonyVoice& voice) noexcept
{
    if (voiceIdx < maxHarmonyVoices) {
        m_voices[voiceIdx] = voice;
        updateFormantFilters();
    }
}

const HarmonyVoice& HarmonizerVocoderNode::voice(std::size_t voiceIdx) const noexcept
{
    static const HarmonyVoice kDefaultVoice {};
    if (voiceIdx < maxHarmonyVoices) {
        return m_voices[voiceIdx];
    }
    return kDefaultVoice;
}

void HarmonizerVocoderNode::setChordContext(std::uint8_t rootMidiNote, ChordQuality quality) noexcept
{
    m_chordRoot = rootMidiNote;
    m_chordQuality = quality;
}

float HarmonizerVocoderNode::snapIntervalToChord(float semitones) const noexcept
{
    if (!m_scaleSnapEnabled) {
        return semitones;
    }

    // Reference chord interval sets in semitones
    std::array<int, 6> chordTones {};
    std::size_t chordToneCount = 0;

    switch (m_chordQuality) {
    case ChordQuality::major:
        chordTones = {0, 4, 7, 12, 16, 19};
        chordToneCount = 6;
        break;
    case ChordQuality::minor:
        chordTones = {0, 3, 7, 12, 15, 19};
        chordToneCount = 6;
        break;
    case ChordQuality::dom7:
        chordTones = {0, 4, 7, 10, 12, 16};
        chordToneCount = 6;
        break;
    case ChordQuality::min7:
    case ChordQuality::min9:
        chordTones = {0, 3, 7, 10, 12, 15};
        chordToneCount = 6;
        break;
    case ChordQuality::maj7:
    case ChordQuality::maj9:
        chordTones = {0, 4, 7, 11, 12, 16};
        chordToneCount = 6;
        break;
    case ChordQuality::diminished:
    case ChordQuality::dim7:
    case ChordQuality::halfDim7:
        chordTones = {0, 3, 6, 9, 12, 15};
        chordToneCount = 6;
        break;
    case ChordQuality::augmented:
        chordTones = {0, 4, 8, 12, 16, 20};
        chordToneCount = 6;
        break;
    default:
        chordTones = {0, 4, 7, 12, 16, 19};
        chordToneCount = 6;
        break;
    }

    const int sign = semitones >= 0.0f ? 1 : -1;
    const float absSemi = std::abs(semitones);

    // Find closest chord interval
    float closest = chordTones[0];
    float minDiff = 1e6f;

    for (std::size_t i = 0; i < chordToneCount; ++i) {
        const float diff = std::abs(absSemi - static_cast<float>(chordTones[i]));
        if (diff < minDiff) {
            minDiff = diff;
            closest = static_cast<float>(chordTones[i]);
        }
    }

    return static_cast<float>(sign) * closest;
}

void HarmonizerVocoderNode::updateVocoderFilters() noexcept
{
    for (std::size_t b = 0; b < vocoderBandCount; ++b) {
        const float modFreq = kVocoderCenterFreqs[b];
        computeBandpassCoeffs(modFreq, 3.5f, m_sampleRate, m_modulatorFilters[b]);

        const float carrierFreq = modFreq * m_vocoderFormantShift;
        computeBandpassCoeffs(carrierFreq, 3.5f, m_sampleRate, m_carrierFiltersL[b]);
        computeBandpassCoeffs(carrierFreq, 3.5f, m_sampleRate, m_carrierFiltersR[b]);
    }
}

void HarmonizerVocoderNode::updateFormantFilters() noexcept
{
    for (std::size_t v = 0; v < maxHarmonyVoices; ++v) {
        const float morph = m_voices[v].formantMorph * m_globalFormantMorph;
        const float formantFreq = 1200.0f * morph;
        computeBandpassCoeffs(formantFreq, 2.0f, m_sampleRate, m_voiceStates[v].formantFilterL);
        computeBandpassCoeffs(formantFreq, 2.0f, m_sampleRate, m_voiceStates[v].formantFilterR);
    }
}

void HarmonizerVocoderNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0) {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = numChannels > 1 ? audio.channels[1] : nullptr;

    const float* scL = (context.sidechain.channels != nullptr && context.sidechain.channelCount > 0)
                       ? context.sidechain.channels[0] : nullptr;
    const float* scR = (context.sidechain.channels != nullptr && context.sidechain.channelCount > 1)
                       ? context.sidechain.channels[1] : scL;

    constexpr double grainSize = 1024.0; // Grain length in samples (~21 ms at 48 kHz)
    const float alphaEnv = 0.005f;      // Envelope follower smoothing
    const float carrierFreq = 130.81f;  // C3 synth carrier tone
    const float carrierPhaseInc = (2.0f * std::numbers::pi_v<float> * carrierFreq) / static_cast<float>(m_sampleRate);

    for (std::size_t s = 0; s < numSamples; ++s) {
        const float dryInL = outL[s];
        const float dryInR = outR != nullptr ? outR[s] : dryInL;

        // Write to ring buffer
        m_pitchRingL[m_pitchWritePos] = dryInL;
        m_pitchRingR[m_pitchWritePos] = dryInR;

        float harmonyMixL = 0.0f;
        float harmonyMixR = 0.0f;

        // 1. Polyphonic Pitch-Shifter Harmonizer
        if (m_harmonizerEnabled) {
            for (std::size_t v = 0; v < maxHarmonyVoices; ++v) {
                const auto& voiceCfg = m_voices[v];
                if (!voiceCfg.enabled) {
                    continue;
                }

                auto& vState = m_voiceStates[v];
                const float effectiveSemitones = snapIntervalToChord(voiceCfg.semitones)
                                               + (voiceCfg.fineTuneCents / 100.0f);
                const double pitchRatio = std::pow(2.0, effectiveSemitones / 12.0);
                const double rateDiff = 1.0 - pitchRatio;

                vState.grainPhase += static_cast<float>(std::abs(rateDiff) / grainSize);
                if (vState.grainPhase >= 1.0f) {
                    vState.grainPhase -= 1.0f;
                }

                const double delayA = std::fmod(vState.grainPhase * grainSize, grainSize);
                const double delayB = std::fmod((vState.grainPhase + 0.5f) * grainSize, grainSize);

                // Grain windowing (equal-power)
                const float winA = std::sin(vState.grainPhase * std::numbers::pi_v<float>);
                const float winB = std::cos(vState.grainPhase * std::numbers::pi_v<float>);

                // Read dual playheads with fractional interpolation
                auto readRing = [&](const std::array<float, delayBufferSize>& ring, double delay) -> float {
                    const double readPos = static_cast<double>(m_pitchWritePos) - delay - 32.0;
                    double wrappedPos = std::fmod(readPos, static_cast<double>(delayBufferSize));
                    if (wrappedPos < 0.0) wrappedPos += static_cast<double>(delayBufferSize);
                    const auto idx0 = static_cast<std::size_t>(wrappedPos) % delayBufferSize;
                    const auto idx1 = (idx0 + 1) % delayBufferSize;
                    const float frac = static_cast<float>(wrappedPos - std::floor(wrappedPos));
                    return ring[idx0] * (1.0f - frac) + ring[idx1] * frac;
                };

                const float sampAL = readRing(m_pitchRingL, delayA);
                const float sampBL = readRing(m_pitchRingL, delayB);
                float grainL = sampAL * winA + sampBL * winB;

                const float sampAR = readRing(m_pitchRingR, delayA);
                const float sampBR = readRing(m_pitchRingR, delayB);
                float grainR = sampAR * winA + sampBR * winB;

                // Formant envelope morphing
                grainL = vState.formantFilterL.process(grainL) * 0.7f + grainL * 0.3f;
                grainR = vState.formantFilterR.process(grainR) * 0.7f + grainR * 0.3f;

                // Voice panning & gain
                const float panL = std::clamp(0.5f * (1.0f - voiceCfg.pan), 0.0f, 1.0f);
                const float panR = std::clamp(0.5f * (1.0f + voiceCfg.pan), 0.0f, 1.0f);

                harmonyMixL += grainL * voiceCfg.gain * panL;
                harmonyMixR += grainR * voiceCfg.gain * panR;
            }
        }

        // 2. 32-Band Classic Analog Vocoder
        float vocoderMixL = 0.0f;
        float vocoderMixR = 0.0f;

        if (m_vocoderEnabled) {
            // Modulator input (lead vocal)
            const float modSample = 0.5f * (dryInL + dryInR);

            // Carrier synthesis
            float rawCarrierL = 0.0f;
            float rawCarrierR = 0.0f;

            if (m_carrierSource == VocoderCarrierSource::internalPulseSaw) {
                // Rich pulse/saw synth tone
                const float saw = (m_carrierPhase / std::numbers::pi_v<float>) - 1.0f;
                const float sub = std::sin(m_carrierPhase * 0.5f);
                rawCarrierL = 0.7f * saw + 0.3f * sub;
                rawCarrierR = rawCarrierL;
            } else if (m_carrierSource == VocoderCarrierSource::internalNoise) {
                m_noiseSeed = m_noiseSeed * 1664525u + 1013904223u;
                const float noise = static_cast<float>(static_cast<std::int32_t>(m_noiseSeed)) / 2147483648.0f;
                rawCarrierL = noise;
                rawCarrierR = noise;
            } else if (scL != nullptr) {
                rawCarrierL = scL[s];
                rawCarrierR = scR != nullptr ? scR[s] : rawCarrierL;
            }

            // Sibilance / unvoiced whisper detector
            const bool zeroCross = (modSample >= 0.0f && m_prevModSample < 0.0f) ||
                                   (modSample < 0.0f && m_prevModSample >= 0.0f);
            m_prevModSample = modSample;
            const float sibilanceBoost = (zeroCross && std::abs(modSample) > m_sibilanceThreshold) ? 1.5f : 0.0f;

            // Process 32 filter bands
            for (std::size_t b = 0; b < vocoderBandCount; ++b) {
                const float modBand = m_modulatorFilters[b].process(modSample);
                const float modMag = std::abs(modBand);

                // Attack / decay envelope follower
                m_bandEnvelopes[b] = (1.0f - alphaEnv) * m_bandEnvelopes[b] + alphaEnv * modMag;

                const float carBandL = m_carrierFiltersL[b].process(rawCarrierL);
                const float carBandR = m_carrierFiltersR[b].process(rawCarrierR);

                vocoderMixL += carBandL * m_bandEnvelopes[b];
                vocoderMixR += carBandR * m_bandEnvelopes[b];
            }

            // Blend sibilance gate noise
            if (sibilanceBoost > 0.0f) {
                m_noiseSeed = m_noiseSeed * 1664525u + 1013904223u;
                const float noise = static_cast<float>(static_cast<std::int32_t>(m_noiseSeed)) / 2147483648.0f;
                vocoderMixL += noise * 0.15f * sibilanceBoost;
                vocoderMixR += noise * 0.15f * sibilanceBoost;
            }

            m_carrierPhase += carrierPhaseInc;
            if (m_carrierPhase >= 2.0f * std::numbers::pi_v<float>) {
                m_carrierPhase -= 2.0f * std::numbers::pi_v<float>;
            }
        }

        m_pitchWritePos = (m_pitchWritePos + 1) % delayBufferSize;

        // Final output summing
        outL[s] = dryInL * m_dryMix
                + harmonyMixL * m_harmonizerWetMix
                + vocoderMixL * m_vocoderWetMix;

        if (outR != nullptr) {
            outR[s] = dryInR * m_dryMix
                    + harmonyMixR * m_harmonizerWetMix
                    + vocoderMixR * m_vocoderWetMix;
        }
    }
}

} // namespace dawg::engine::dsp
