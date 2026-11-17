#include <dawg/engine/dsp/MultiMicDrumDebleedEngine.h>

#include <numbers>

namespace dawg::engine::dsp {

MultiMicDrumDebleedEngine::MultiMicDrumDebleedEngine() noexcept
{
    // Precompute Hann window
    for (std::size_t i = 0; i < FftSize; ++i)
    {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(FftSize)));
    }
    reset();
}

void MultiMicDrumDebleedEngine::setConfig(const Config& config) noexcept
{
    m_config = config;
    if (m_sampleRate > 0.0)
    {
        m_lookaheadSamples = std::clamp(
            static_cast<std::size_t>(m_config.lookaheadMs * 0.001 * m_sampleRate),
            std::size_t{1},
            MaxLookaheadSamples);
    }
}

void MultiMicDrumDebleedEngine::prepare(double sampleRate, std::size_t /*maxBlockSize*/) noexcept
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    m_lookaheadSamples = std::clamp(
        static_cast<std::size_t>(m_config.lookaheadMs * 0.001 * m_sampleRate),
        std::size_t{1},
        MaxLookaheadSamples);
    reset();
}

void MultiMicDrumDebleedEngine::reset() noexcept
{
    m_inputTarget.fill(0.0f);
    m_inputBleed.fill(0.0f);
    m_inputPos = 0;

    m_outputOlaBuffer.fill(0.0f);
    m_outputReadPos = 0;

    m_targetMag.fill(0.0f);
    m_targetPhase.fill(0.0f);
    m_bleedMag.fill(0.0f);
    m_bleedPhase.fill(0.0f);
    m_cleanedMag.fill(0.0f);

    m_lookaheadRingTarget.fill(0.0f);
    m_lookaheadRingPos = 0;

    m_bleedEnergyPrev = 0.0f;
    m_targetEnergyPrev = 0.0f;
    m_duckingGain = 1.0f;
    m_holdCounter = 0.0f;
    m_currentAttenuationDb = 0.0f;
}

void MultiMicDrumDebleedEngine::fftRadix2(
    std::array<float, FftSize>& real,
    std::array<float, FftSize>& imag,
    bool inverse) noexcept
{
    // Bit-reversal permutation
    std::size_t j = 0;
    for (std::size_t i = 0; i < FftSize - 1; ++i)
    {
        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        std::size_t k = FftSize / 2;
        while (k <= j)
        {
            j -= k;
            k /= 2;
        }
        j += k;
    }

    // Cooley-Tukey butterfly
    for (std::size_t len = 2; len <= FftSize; len <<= 1)
    {
        const float angle = (inverse ? 2.0f : -2.0f) * std::numbers::pi_v<float> / static_cast<float>(len);
        const float wlenCos = std::cos(angle);
        const float wlenSin = std::sin(angle);

        for (std::size_t i = 0; i < FftSize; i += len)
        {
            float wCos = 1.0f;
            float wSin = 0.0f;

            for (std::size_t k = 0; k < len / 2; ++k)
            {
                const std::size_t uIdx = i + k;
                const std::size_t vIdx = i + k + len / 2;

                const float uR = real[uIdx];
                const float uI = imag[uIdx];
                const float vR = real[vIdx] * wCos - imag[vIdx] * wSin;
                const float vI = real[vIdx] * wSin + imag[vIdx] * wCos;

                real[uIdx] = uR + vR;
                imag[uIdx] = uI + vI;
                real[vIdx] = uR - vR;
                imag[vIdx] = uI - vI;

                const float nextWCos = wCos * wlenCos - wSin * wlenSin;
                const float nextWSin = wCos * wlenSin + wSin * wlenCos;
                wCos = nextWCos;
                wSin = nextWSin;
            }
        }
    }

    if (inverse)
    {
        const float invN = 1.0f / static_cast<float>(FftSize);
        for (std::size_t i = 0; i < FftSize; ++i)
        {
            real[i] *= invN;
            imag[i] *= invN;
        }
    }
}

void MultiMicDrumDebleedEngine::processStftFrame() noexcept
{
    // 1. Forward FFT on Target Frame and Bleed Reference Frame
    std::array<float, FftSize> realT {};
    std::array<float, FftSize> imagT {};
    std::array<float, FftSize> realB {};
    std::array<float, FftSize> imagB {};

    for (std::size_t i = 0; i < FftSize; ++i)
    {
        realT[i] = m_inputTarget[i] * m_window[i];
        imagT[i] = 0.0f;
        realB[i] = m_inputBleed[i] * m_window[i];
        imagB[i] = 0.0f;
    }

    fftRadix2(realT, imagT, false);
    fftRadix2(realB, imagB, false);

    for (std::size_t k = 0; k < NumBins; ++k)
    {
        const float rt = realT[k];
        const float it = imagT[k];
        m_targetMag[k] = std::sqrt(rt * rt + it * it);
        m_targetPhase[k] = std::atan2(it, rt);

        const float rb = realB[k];
        const float ib = imagB[k];
        m_bleedMag[k] = std::sqrt(rb * rb + ib * ib);
        m_bleedPhase[k] = std::atan2(ib, rb);
    }

    // 2. Classical Spectral Subtraction with Spectral Floor
    // |Y(k)| = max( |X_t(k)| - alpha * oversub * |X_b(k)|, beta * |X_t(k)| )
    const float alpha = m_config.couplingFactor;
    const float oversub = m_config.oversubtraction;
    const float floorLinear = std::pow(10.0f, m_config.spectralFloorDb / 20.0f);

    for (std::size_t k = 0; k < NumBins; ++k)
    {
        const float sub = m_targetMag[k] - (alpha * oversub * m_bleedMag[k]);
        const float minBound = floorLinear * m_targetMag[k];
        m_cleanedMag[k] = std::max(sub, minBound);
    }

    // 3. Inverse FFT Reconstruction with Preserved Phase
    std::array<float, FftSize> realOut {};
    std::array<float, FftSize> imagOut {};

    for (std::size_t k = 0; k < NumBins; ++k)
    {
        realOut[k] = m_cleanedMag[k] * std::cos(m_targetPhase[k]);
        imagOut[k] = m_cleanedMag[k] * std::sin(m_targetPhase[k]);
    }
    // Hermitian symmetry
    for (std::size_t k = NumBins; k < FftSize; ++k)
    {
        realOut[k] = realOut[FftSize - k];
        imagOut[k] = -imagOut[FftSize - k];
    }

    fftRadix2(realOut, imagOut, true);

    // Overlap-add with 75% overlap synthesis normalization (1 / 1.5)
    constexpr float olaNorm = 1.0f / 1.5f;
    const std::size_t olaCap = m_outputOlaBuffer.size();
    for (std::size_t i = 0; i < FftSize; ++i)
    {
        const std::size_t bufIdx = (m_outputReadPos + i) % olaCap;
        m_outputOlaBuffer[bufIdx] += realOut[i] * m_window[i] * olaNorm;
    }
}

void MultiMicDrumDebleedEngine::processBlock(
    const float* targetIn,
    const float* bleedRefIn,
    float* targetOut,
    std::size_t numSamples) noexcept
{
    if (targetIn == nullptr || targetOut == nullptr || numSamples == 0)
    {
        return;
    }

    const float duckMinLinear = std::pow(10.0f, m_config.duckingDepthDb / 20.0f);
    const float holdSamples = static_cast<float>(m_config.gateHoldMs * 0.001 * m_sampleRate);
    const float releaseCoeff = std::exp(-1.0f / (static_cast<float>(m_config.gateReleaseMs * 0.001 * m_sampleRate) + 1.0f));

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inT = targetIn[i];
        const float inB = (bleedRefIn != nullptr) ? bleedRefIn[i] : 0.0f;

        // Push into lookahead ring buffer
        m_lookaheadRingTarget[m_lookaheadRingPos] = inT;

        // Transient energy detection on bleed reference
        const float absB = std::abs(inB);
        const float bleedDelta = std::max(0.0f, absB - m_bleedEnergyPrev);
        m_bleedEnergyPrev = absB * 0.2f + m_bleedEnergyPrev * 0.8f;

        // Target energy
        const float absT = std::abs(inT);
        const float targetDelta = std::max(0.0f, absT - m_targetEnergyPrev);
        m_targetEnergyPrev = absT * 0.2f + m_targetEnergyPrev * 0.8f;

        // Check if bleed transient triggers sidechain ducking
        const bool bleedTransient = (bleedDelta > (m_config.bleedSensitivity * 0.15f));
        const bool targetTransient = (targetDelta > (m_config.targetSensitivity * 0.2f));

        if (bleedTransient && ! targetTransient)
        {
            m_duckingGain = duckMinLinear;
            m_holdCounter = holdSamples;
        }
        else if (m_holdCounter > 0.0f)
        {
            m_holdCounter -= 1.0f;
        }
        else
        {
            m_duckingGain = 1.0f - (1.0f - m_duckingGain) * releaseCoeff;
        }

        // STFT input shift
        m_inputTarget[m_inputPos] = inT;
        m_inputBleed[m_inputPos] = inB;
        m_inputPos++;

        if (m_inputPos >= FftSize)
        {
            processStftFrame();
            // Hop slide (75% overlap)
            for (std::size_t s = 0; s < FftSize - HopSize; ++s)
            {
                m_inputTarget[s] = m_inputTarget[s + HopSize];
                m_inputBleed[s] = m_inputBleed[s + HopSize];
            }
            m_inputPos = FftSize - HopSize;
        }

        // Read out reconstructed STFT sample (or dry lookahead if buffer warming)
        float cleaned = m_outputOlaBuffer[m_outputReadPos];
        m_outputOlaBuffer[m_outputReadPos] = 0.0f;
        m_outputReadPos = (m_outputReadPos + 1) % m_outputOlaBuffer.size();

        // Apply phase-coherent transient gating ducking
        const float finalSample = cleaned * m_duckingGain;
        targetOut[i] = finalSample;

        // Increment ring buffer
        m_lookaheadRingPos = (m_lookaheadRingPos + 1) % m_lookaheadRingTarget.size();
    }

    m_currentAttenuationDb = (m_duckingGain > 0.00001f)
        ? 20.0f * std::log10(m_duckingGain)
        : m_config.duckingDepthDb;
}

void MultiMicDrumDebleedEngine::processOffline(
    const float* targetTrack,
    const float* bleedRefTrack,
    float* outputTrack,
    std::size_t numSamples,
    double sampleRate,
    const Config& config)
{
    if (targetTrack == nullptr || outputTrack == nullptr || numSamples == 0)
    {
        return;
    }

    MultiMicDrumDebleedEngine engine;
    engine.setConfig(config);
    engine.prepare(sampleRate, 1024);

    constexpr std::size_t BlockSize = 256;
    std::size_t processed = 0;

    while (processed < numSamples)
    {
        const std::size_t toProcess = std::min(BlockSize, numSamples - processed);
        const float* refPtr = (bleedRefTrack != nullptr) ? (bleedRefTrack + processed) : nullptr;
        engine.processBlock(
            targetTrack + processed,
            refPtr,
            outputTrack + processed,
            toProcess);
        processed += toProcess;
    }
}

} // namespace dawg::engine::dsp
