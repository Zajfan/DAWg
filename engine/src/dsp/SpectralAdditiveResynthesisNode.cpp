#include <dawg/engine/dsp/SpectralAdditiveResynthesisNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr double twoPi = 6.2831853071795864769;

inline float nextNoiseSample(std::uint32_t& rng) noexcept
{
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(rng)) / 2147483648.0f;
}

} // namespace

SpectralAdditiveResynthesisNode::SpectralAdditiveResynthesisNode() noexcept
{
    m_phases.fill(0.0);

    // Initialize Stream A with warm acoustic harmonic partials
    auto& streamA = m_streams[0];
    m_streamFrameCounts[0] = 64;
    for (std::size_t f = 0; f < 64; ++f)
    {
        auto& frame = streamA[f];
        frame.partialCount = 16;
        frame.noiseLevel = 0.05f;
        frame.spectralCentroid = 1200.0f;
        for (std::size_t k = 0; k < 16; ++k)
        {
            const float harm = static_cast<float>(k + 1);
            frame.partials[k].frequencyHz = 440.0f * harm;
            // 1/k roll-off
            frame.partials[k].amplitude = 0.5f / harm;
            frame.partials[k].phase = 0.0f;
        }
    }

    // Initialize Stream B with brighter metallic / vocal vowel partials
    auto& streamB = m_streams[1];
    m_streamFrameCounts[1] = 64;
    for (std::size_t f = 0; f < 64; ++f)
    {
        auto& frame = streamB[f];
        frame.partialCount = 16;
        frame.noiseLevel = 0.15f;
        frame.spectralCentroid = 2500.0f;
        for (std::size_t k = 0; k < 16; ++k)
        {
            const float harm = static_cast<float>(k + 1);
            frame.partials[k].frequencyHz = 440.0f * harm * 1.05f;
            // Odd harmonics boosted (clarinet / reed timbre)
            const float oddBoost = (k % 2 == 0) ? 1.0f : 0.2f;
            frame.partials[k].amplitude = (0.6f / harm) * oddBoost;
            frame.partials[k].phase = 0.0f;
        }
    }
}

bool SpectralAdditiveResynthesisNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void SpectralAdditiveResynthesisNode::reset() noexcept
{
    m_phases.fill(0.0);
    m_playbackPlayhead = 0.0f;
    m_noiseFilterState = 0.0f;
}

void SpectralAdditiveResynthesisNode::loadStreamFrames(const std::size_t streamIndex, const std::vector<SpectralFrame>& frames)
{
    if (streamIndex >= 2 || frames.empty())
        return;

    const std::size_t count = std::min(frames.size(), maxFrames);
    for (std::size_t i = 0; i < count; ++i)
    {
        m_streams[streamIndex][i] = frames[i];
    }
    m_streamFrameCounts[streamIndex] = count;
}

void SpectralAdditiveResynthesisNode::setMorphX(const float x0to1) noexcept
{
    m_morphX.store(std::clamp(x0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::morphX() const noexcept
{
    return m_morphX.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setFormantWarp(const float factor) noexcept
{
    m_formantWarp.store(std::clamp(factor, 0.25f, 4.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::formantWarp() const noexcept
{
    return m_formantWarp.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setInharmonicity(const float amount) noexcept
{
    m_inharmonicity.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::inharmonicity() const noexcept
{
    return m_inharmonicity.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setTimeFreeze(const bool freeze) noexcept
{
    m_timeFreeze.store(freeze, std::memory_order_release);
}

bool SpectralAdditiveResynthesisNode::isTimeFrozen() const noexcept
{
    return m_timeFreeze.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setScrubPosition(const float pos0to1) noexcept
{
    m_scrubPosition.store(std::clamp(pos0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::scrubPosition() const noexcept
{
    return m_scrubPosition.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setResidualNoiseMix(const float mix0to1) noexcept
{
    m_residualNoiseMix.store(std::clamp(mix0to1, 0.0f, 1.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::residualNoiseMix() const noexcept
{
    return m_residualNoiseMix.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::setFundamentalPitch(const float pitchHz) noexcept
{
    m_fundamentalPitch.store(std::clamp(pitchHz, 20.0f, 5000.0f), std::memory_order_release);
}

float SpectralAdditiveResynthesisNode::fundamentalPitch() const noexcept
{
    return m_fundamentalPitch.load(std::memory_order_acquire);
}

void SpectralAdditiveResynthesisNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const float morph = m_morphX.load(std::memory_order_relaxed);
    const float warp = m_formantWarp.load(std::memory_order_relaxed);
    const float inharm = m_inharmonicity.load(std::memory_order_relaxed);
    const bool freeze = m_timeFreeze.load(std::memory_order_relaxed);
    const float noiseMix = m_residualNoiseMix.load(std::memory_order_relaxed);
    const float f0 = m_fundamentalPitch.load(std::memory_order_relaxed);

    // Determine current frame indices for Stream A and Stream B
    const std::size_t countA = std::max<std::size_t>(1, m_streamFrameCounts[0]);
    const std::size_t countB = std::max<std::size_t>(1, m_streamFrameCounts[1]);

    float scrubPos = freeze ? m_scrubPosition.load(std::memory_order_relaxed) : m_playbackPlayhead;
    scrubPos = std::clamp(scrubPos, 0.0f, 1.0f);

    const auto frameIdxA = static_cast<std::size_t>(scrubPos * static_cast<float>(countA - 1));
    const auto frameIdxB = static_cast<std::size_t>(scrubPos * static_cast<float>(countB - 1));

    const auto& frameA = m_streams[0][std::min(frameIdxA, countA - 1)];
    const auto& frameB = m_streams[1][std::min(frameIdxB, countB - 1)];

    const std::size_t activePartials = std::min(maxPartials, std::max(frameA.partialCount, frameB.partialCount));

    // Interpolate partials between Stream A and Stream B
    std::array<float, maxPartials> targetFreqs {};
    std::array<float, maxPartials> targetAmps {};

    for (std::size_t k = 0; k < activePartials; ++k)
    {
        const float harmIndex = static_cast<float>(k + 1);
        const float fA = (k < frameA.partialCount) ? frameA.partials[k].frequencyHz : (f0 * harmIndex);
        const float fB = (k < frameB.partialCount) ? frameB.partials[k].frequencyHz : (f0 * harmIndex);
        const float aA = (k < frameA.partialCount) ? frameA.partials[k].amplitude : 0.0f;
        const float aB = (k < frameB.partialCount) ? frameB.partials[k].amplitude : 0.0f;

        // Morph frequency and amplitude
        float freq = (1.0f - morph) * fA + morph * fB;
        float amp = (1.0f - morph) * aA + morph * aB;

        // Apply fundamental pitch ratio
        freq *= (f0 / 440.0f);

        // Inharmonicity stretch: f_k = f * sqrt(1 + B * k^2)
        if (inharm > 0.001f)
        {
            const float stretch = std::sqrt(1.0f + (inharm * 0.05f) * harmIndex * harmIndex);
            freq *= stretch;
        }

        // Formant warp shifts higher envelope
        if (std::abs(warp - 1.0f) > 0.01f)
        {
            amp *= std::clamp(1.0f - 0.2f * std::abs(harmIndex * warp - harmIndex), 0.1f, 1.5f);
        }

        targetFreqs[k] = std::clamp(freq, 20.0f, static_cast<float>(m_sampleRate * 0.48));
        targetAmps[k] = amp;
    }

    const float targetNoise = ((1.0f - morph) * frameA.noiseLevel + morph * frameB.noiseLevel) * noiseMix;

    // Real-Time Additive Oscillator Bank Synthesis (Zero Allocations)
    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        float sampleAcc = 0.0f;

        for (std::size_t k = 0; k < activePartials; ++k)
        {
            sampleAcc += targetAmps[k] * static_cast<float>(std::sin(m_phases[k]));
            const double phaseInc = (targetFreqs[k] / m_sampleRate) * twoPi;
            m_phases[k] += phaseInc;
            if (m_phases[k] >= twoPi)
                m_phases[k] -= twoPi;
        }

        // Add stochastic residual noise bed (single-pole lowpass shaped)
        if (targetNoise > 0.001f)
        {
            const float rawNoise = nextNoiseSample(m_rngState);
            m_noiseFilterState += 0.2f * (rawNoise - m_noiseFilterState);
            sampleAcc += m_noiseFilterState * targetNoise;
        }

        for (std::size_t c = 0; c < audio.channelCount; ++c)
        {
            float* ch = audio.channels[c];
            if (ch != nullptr)
                ch[i] = sampleAcc;
        }
    }

    // Advance playback position if not frozen
    if (! freeze)
    {
        m_playbackPlayhead += static_cast<float>(audio.frameCount) / (static_cast<float>(m_sampleRate) * 2.0f); // 2 second loop
        if (m_playbackPlayhead >= 1.0f)
            m_playbackPlayhead -= 1.0f;
    }
}

} // namespace dawg::engine::dsp
