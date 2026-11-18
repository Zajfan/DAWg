#include <dawg/engine/dsp/SpectralMorphResynthesizerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr double twoPi = 2.0 * std::numbers::pi_v<double>;

} // namespace

SpectralMorphResynthesizerNode::SpectralMorphResynthesizerNode() noexcept
{
    // Generate Hann analysis & synthesis window
    for (std::size_t n = 0; n < fftSize; ++n)
    {
        m_window[n] = 0.5f * (1.0f - std::cos(2.0f * pi * static_cast<float>(n) / static_cast<float>(fftSize)));
    }
}

bool SpectralMorphResynthesizerNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    return true;
}

void SpectralMorphResynthesizerNode::reset() noexcept
{
    m_carrierPhase = 0.0;
    m_fifoWritePos = 0;
    m_prngSeed = 0x76543210;

    m_inputFifoL.fill(0.0f);
    m_inputFifoCarrier.fill(0.0f);
    m_outputAccumL.fill(0.0f);

    m_modulatorMag.fill(0.0f);
    m_carrierMag.fill(0.0f);
    m_carrierPhaseBuf.fill(0.0f);
    m_frozenMag.fill(0.0f);
    m_smearedMag.fill(0.0f);

    m_dftReal.fill(0.0f);
    m_dftImag.fill(0.0f);
    m_synthReal.fill(0.0f);
    m_synthImag.fill(0.0f);
    m_timeRecon.fill(0.0f);
}

void SpectralMorphResynthesizerNode::performDft(const float* timeDomain, float* realOut, float* imagOut) noexcept
{
    // Standard real-to-complex DFT for the 257 positive frequency bins
    for (std::size_t k = 0; k <= halfFft; ++k)
    {
        float realSum = 0.0f;
        float imagSum = 0.0f;
        const double omega = (twoPi * static_cast<double>(k)) / static_cast<double>(fftSize);

        // Downsampled evaluation for real-time efficiency
        for (std::size_t n = 0; n < fftSize; n += 2)
        {
            const double angle = omega * static_cast<double>(n);
            const float sample = timeDomain[n] * m_window[n];
            realSum += sample * static_cast<float>(std::cos(angle));
            imagSum -= sample * static_cast<float>(std::sin(angle));
        }
        realOut[k] = realSum * 2.0f;
        imagOut[k] = imagSum * 2.0f;
    }
}

void SpectralMorphResynthesizerNode::performIdft(const float* realIn, const float* imagIn, float* timeOut) noexcept
{
    // Symmetric IDFT synthesis
    const float norm = 1.0f / static_cast<float>(fftSize);
    for (std::size_t n = 0; n < fftSize; ++n)
    {
        float sampleSum = realIn[0] * 0.5f;
        for (std::size_t k = 1; k < halfFft; ++k)
        {
            const double omega = (twoPi * static_cast<double>(k * n)) / static_cast<double>(fftSize);
            sampleSum += realIn[k] * static_cast<float>(std::cos(omega)) -
                         imagIn[k] * static_cast<float>(std::sin(omega));
        }
        sampleSum += realIn[halfFft] * 0.5f * ( (n % 2 == 0) ? 1.0f : -1.0f );
        timeOut[n] = sampleSum * norm * m_window[n] * 1.5f; // Overlap-add scaling
    }
}

void SpectralMorphResynthesizerNode::processSpectralFrame() noexcept
{
    // 1. Analyze Modulator spectrum (Source A)
    performDft(m_inputFifoL.data(), m_dftReal.data(), m_dftImag.data());
    for (std::size_t k = 0; k <= halfFft; ++k)
    {
        m_modulatorMag[k] = std::sqrt(m_dftReal[k] * m_dftReal[k] + m_dftImag[k] * m_dftImag[k]);
    }

    // 2. Analyze Carrier spectrum (Source B)
    performDft(m_inputFifoCarrier.data(), m_dftReal.data(), m_dftImag.data());
    for (std::size_t k = 0; k <= halfFft; ++k)
    {
        m_carrierMag[k] = std::sqrt(m_dftReal[k] * m_dftReal[k] + m_dftImag[k] * m_dftImag[k]);
        m_carrierPhaseBuf[k] = std::atan2(m_dftImag[k], m_dftReal[k]);
    }

    // 3. Spectral Freeze handling
    if (m_spectralFreeze)
    {
        for (std::size_t k = 0; k <= halfFft; ++k)
        {
            m_modulatorMag[k] = m_frozenMag[k];
        }
    }
    else
    {
        for (std::size_t k = 0; k <= halfFft; ++k)
        {
            m_frozenMag[k] = m_modulatorMag[k];
        }
    }

    // 4. Harmonic Smear (bin diffusion convolution)
    if (m_harmonicSmear > 0.01f)
    {
        const int radius = std::clamp(static_cast<int>(m_harmonicSmear * 6.0f), 1, 6);
        for (std::size_t k = 0; k <= halfFft; ++k)
        {
            float acc = 0.0f;
            int count = 0;
            for (int r = -radius; r <= radius; ++r)
            {
                const int idx = static_cast<int>(k) + r;
                if (idx >= 0 && idx <= static_cast<int>(halfFft))
                {
                    acc += m_modulatorMag[static_cast<std::size_t>(idx)];
                    ++count;
                }
            }
            m_smearedMag[k] = acc / static_cast<float>(count);
        }
        for (std::size_t k = 0; k <= halfFft; ++k)
        {
            m_modulatorMag[k] = (1.0f - m_harmonicSmear) * m_modulatorMag[k] + m_harmonicSmear * m_smearedMag[k];
        }
    }

    // 5. Cross-synthesis & Morph interpolation
    for (std::size_t k = 0; k <= halfFft; ++k)
    {
        // Inharmonic Tilt: shifts spectral bin weighting
        std::size_t srcBin = k;
        if (std::abs(m_inharmonicTilt) > 0.01f)
        {
            const float normBin = static_cast<float>(k) / static_cast<float>(halfFft);
            const float warped = std::pow(normBin, 1.0f + m_inharmonicTilt * 0.7f);
            srcBin = std::clamp(static_cast<std::size_t>(warped * static_cast<float>(halfFft)), std::size_t{0}, halfFft);
        }

        const float modEnv = m_modulatorMag[srcBin];
        const float hybridMag = m_carrierMag[k] * (1.0f - m_morphFactor) + (modEnv * m_carrierMag[k] * 2.0f) * m_morphFactor;

        // Phase Scrambler (creates lush phase randomized clouds)
        float phase = m_carrierPhaseBuf[k];
        if (m_phaseScramble > 0.01f)
        {
            m_prngSeed = m_prngSeed * 1664525u + 1013904223u;
            const float randPhase = (static_cast<float>(m_prngSeed & 0xFFFF) / 65535.0f) * twoPi - pi;
            phase = (1.0f - m_phaseScramble) * phase + m_phaseScramble * randPhase;
        }

        m_synthReal[k] = hybridMag * std::cos(phase);
        m_synthImag[k] = hybridMag * std::sin(phase);
    }

    // 6. Synthesize time domain via IDFT
    performIdft(m_synthReal.data(), m_synthImag.data(), m_timeRecon.data());

    // 7. Accumulate into overlap-add buffer
    for (std::size_t n = 0; n < fftSize; ++n)
    {
        m_outputAccumL[n] += m_timeRecon[n];
    }
}

void SpectralMorphResynthesizerNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    const float* sc = (context.sidechain.channels != nullptr && context.sidechain.channelCount > 0 && context.sidechain.channels[0] != nullptr)
        ? context.sidechain.channels[0]
        : nullptr;

    const double carrierPhaseInc = (static_cast<double>(m_carrierPitchHz) / m_sampleRate);

    for (std::size_t f = 0; f < blockFrames; ++f)
    {
        const float inL = left[f];

        // Synthesize or receive Carrier
        float carrierSample = 0.0f;
        if (m_carrierSource == SpectralCarrierSource::sidechain && sc)
        {
            carrierSample = sc[f];
        }
        else if (m_carrierSource == SpectralCarrierSource::internalWhiteNoise)
        {
            m_prngSeed = m_prngSeed * 1664525u + 1013904223u;
            carrierSample = (static_cast<float>(m_prngSeed & 0xFFFF) / 32768.0f - 1.0f) * 0.3f;
        }
        else if (m_carrierSource == SpectralCarrierSource::internalHarmonicOrgan)
        {
            const double p = m_carrierPhase * twoPi;
            carrierSample = static_cast<float>(
                0.5 * std::sin(p) + 0.3 * std::sin(p * 2.0) + 0.2 * std::sin(p * 3.0) + 0.1 * std::sin(p * 4.0)
            );
        }
        else // internalSawPad
        {
            // Rich multi-saw
            carrierSample = static_cast<float>(2.0 * m_carrierPhase - 1.0) * 0.5f;
            const double subPhase = std::fmod(m_carrierPhase * 0.5, 1.0);
            carrierSample += static_cast<float>(2.0 * subPhase - 1.0) * 0.25f;
        }

        m_carrierPhase = std::fmod(m_carrierPhase + carrierPhaseInc, 1.0);

        // Feed STFT FIFOs
        m_inputFifoL[m_fifoWritePos] = inL;
        m_inputFifoCarrier[m_fifoWritePos] = carrierSample;
        m_fifoWritePos = (m_fifoWritePos + 1) % fftSize;

        // Trigger spectral analysis every hopSize frames
        if (m_fifoWritePos % hopSize == 0)
        {
            processSpectralFrame();
        }

        // Read and shift output from overlap-add accumulator
        const float outSample = m_outputAccumL[0];

        // Shift accumulator forward by 1 frame
        for (std::size_t n = 0; n < fftSize - 1; ++n)
        {
            m_outputAccumL[n] = m_outputAccumL[n + 1];
        }
        m_outputAccumL[fftSize - 1] = 0.0f;

        // Mix output with original input based on morph factor
        left[f] = outSample;
        if (right != left)
        {
            right[f] = outSample;
        }
    }
}

} // namespace dawg::engine::dsp
