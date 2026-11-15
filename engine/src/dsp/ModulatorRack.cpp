#include <dawg/engine/dsp/ModulatorRack.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

ModulatorRack::ModulatorRack() noexcept
{
    for (std::size_t i = 0; i < kNumMacros; ++i)
    {
        m_macros[i].name = "Macro " + std::to_string(i + 1);
        m_macros[i].value = 0.5f;
    }
}

void ModulatorRack::prepare(double sampleRate) noexcept
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
}

void ModulatorRack::reset() noexcept
{
    for (auto& lfo : m_lfos)
    {
        lfo.phase = 0.0;
        lfo.lastOutput = 0.0f;
        lfo.shValue = 0.0f;
    }
    m_envFollowerLevel = 0.0f;
    m_currentStepIdx = 0;
    m_stepModCurrentValue = m_stepValues[0];
}

void ModulatorRack::setMacroValue(std::size_t index, float value) noexcept
{
    if (index < kNumMacros)
    {
        m_macros[index].value = std::clamp(value, 0.0f, 1.0f);
    }
}

void ModulatorRack::setLfoWaveform(std::size_t lfoIdx, LfoWaveform wave) noexcept
{
    if (lfoIdx < kNumLfos)
        m_lfos[lfoIdx].wave = wave;
}

void ModulatorRack::setLfoSyncDivision(std::size_t lfoIdx, ModSyncDivision div) noexcept
{
    if (lfoIdx < kNumLfos)
        m_lfos[lfoIdx].division = div;
}

void ModulatorRack::setLfoFreeRateHz(std::size_t lfoIdx, double rateHz) noexcept
{
    if (lfoIdx < kNumLfos)
        m_lfos[lfoIdx].freeRateHz = std::clamp(rateHz, 0.01, 50.0);
}

void ModulatorRack::setLfoTempoSynced(std::size_t lfoIdx, bool synced) noexcept
{
    if (lfoIdx < kNumLfos)
        m_lfos[lfoIdx].tempoSynced = synced;
}

void ModulatorRack::setLfoDepth(std::size_t lfoIdx, float depth) noexcept
{
    if (lfoIdx < kNumLfos)
        m_lfos[lfoIdx].depth = std::clamp(depth, 0.0f, 1.0f);
}

float ModulatorRack::lfoValue(std::size_t lfoIdx) const noexcept
{
    if (lfoIdx < kNumLfos)
        return m_lfos[lfoIdx].lastOutput;
    return 0.0f;
}

void ModulatorRack::setEnvFollowerAttack(float ms) noexcept
{
    m_envAttackMs = std::clamp(ms, 0.1f, 500.0f);
}

void ModulatorRack::setEnvFollowerRelease(float ms) noexcept
{
    m_envReleaseMs = std::clamp(ms, 1.0f, 2000.0f);
}

void ModulatorRack::setEnvFollowerGain(float gain) noexcept
{
    m_envGain = std::clamp(gain, 0.0f, 10.0f);
}

void ModulatorRack::setStepValue(std::size_t stepIdx, float val) noexcept
{
    if (stepIdx < 16)
        m_stepValues[stepIdx] = std::clamp(val, 0.0f, 1.0f);
}

float ModulatorRack::stepValue(std::size_t stepIdx) const noexcept
{
    if (stepIdx < 16)
        return m_stepValues[stepIdx];
    return 0.0f;
}

double ModulatorRack::syncDivisionToBeats(ModSyncDivision div) const noexcept
{
    switch (div)
    {
    case ModSyncDivision::bars4: return 16.0;
    case ModSyncDivision::bars2: return 8.0;
    case ModSyncDivision::bar1: return 4.0;
    case ModSyncDivision::half: return 2.0;
    case ModSyncDivision::quarter: return 1.0;
    case ModSyncDivision::eighth: return 0.5;
    case ModSyncDivision::sixteenth: return 0.25;
    case ModSyncDivision::thirtySecond: return 0.125;
    case ModSyncDivision::eighthTriplet: return 1.0 / 3.0;
    case ModSyncDivision::sixteenthTriplet: return 0.5 / 3.0;
    }
    return 1.0;
}

float ModulatorRack::evaluateLfoWave(LfoWaveform wave, double phase, std::size_t lfoIdx) noexcept
{
    constexpr double twoPi = 2.0 * 3.14159265358979323846;
    const float p = static_cast<float>(phase);

    switch (wave)
    {
    case LfoWaveform::sine:
        return static_cast<float>(std::sin(phase * twoPi)) * 0.5f + 0.5f;

    case LfoWaveform::triangle:
        return 1.0f - 2.0f * std::abs(2.0f * (p - std::floor(p + 0.5f)));

    case LfoWaveform::sawUp:
        return p;

    case LfoWaveform::sawDown:
        return 1.0f - p;

    case LfoWaveform::square:
        return (p < 0.5f) ? 1.0f : 0.0f;

    case LfoWaveform::randomSampleHold:
        return m_lfos[lfoIdx].shValue;
    }
    return 0.5f;
}

void ModulatorRack::process(const float* const* inputAudio, std::size_t channelCount,
                           std::size_t frameCount, double bpm, std::int64_t timelineSample) noexcept
{
    const double validBpm = (bpm > 10.0) ? bpm : 120.0;
    const double samplesPerBeat = (m_sampleRate * 60.0) / validBpm;
    const double dt = 1.0 / m_sampleRate;

    // 1. Process LFOs
    for (std::size_t i = 0; i < kNumLfos; ++i)
    {
        auto& lfo = m_lfos[i];
        double freqHz = lfo.freeRateHz;

        if (lfo.tempoSynced)
        {
            const double beats = syncDivisionToBeats(lfo.division);
            freqHz = validBpm / (60.0 * beats);
        }

        const double phaseIncrement = (freqHz * static_cast<double>(frameCount)) / m_sampleRate;
        const double oldPhase = lfo.phase;
        lfo.phase = std::fmod(lfo.phase + phaseIncrement, 1.0);

        // Update Sample & Hold if cycle wrapped
        if (lfo.phase < oldPhase)
        {
            lfo.rngSeed = lfo.rngSeed * 1664525u + 1013904223u;
            lfo.shValue = static_cast<float>(lfo.rngSeed & 0xFFFF) / 65535.0f;
        }

        const float rawWave = evaluateLfoWave(lfo.wave, lfo.phase, i);
        lfo.lastOutput = 0.5f + (rawWave - 0.5f) * lfo.depth;
    }

    // 2. Process Envelope Follower over incoming block
    if (inputAudio != nullptr && frameCount > 0 && channelCount > 0)
    {
        float blockPeak = 0.0f;
        for (std::size_t ch = 0; ch < channelCount; ++ch)
        {
            if (inputAudio[ch] == nullptr)
                continue;
            for (std::size_t f = 0; f < frameCount; ++f)
            {
                blockPeak = std::max(blockPeak, std::abs(inputAudio[ch][f]));
            }
        }
        blockPeak *= m_envGain;

        const float attackCoeff = std::exp(static_cast<float>(-dt * static_cast<double>(frameCount) / (static_cast<double>(m_envAttackMs) * 0.001)));
        const float releaseCoeff = std::exp(static_cast<float>(-dt * static_cast<double>(frameCount) / (static_cast<double>(m_envReleaseMs) * 0.001)));

        if (blockPeak > m_envFollowerLevel)
            m_envFollowerLevel = blockPeak + attackCoeff * (m_envFollowerLevel - blockPeak);
        else
            m_envFollowerLevel = blockPeak + releaseCoeff * (m_envFollowerLevel - blockPeak);

        m_envFollowerLevel = std::clamp(m_envFollowerLevel, 0.0f, 1.0f);
    }

    // 3. Process Step Modulator synced to timeline position
    const double stepBeats = syncDivisionToBeats(m_stepDivision);
    const double samplesPerStep = samplesPerBeat * stepBeats;
    if (samplesPerStep > 1.0)
    {
        const auto stepIdx = static_cast<int>(std::floor(static_cast<double>(std::max<std::int64_t>(0, timelineSample)) / samplesPerStep)) % 16;
        m_currentStepIdx = stepIdx;
        m_stepModCurrentValue = m_stepValues[static_cast<std::size_t>(stepIdx)];
    }
}

} // namespace dawg::engine
