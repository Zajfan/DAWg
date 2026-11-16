#include <dawg/engine/dsp/SpectralMorphCrossSynthNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

void bitReverse(std::array<float, SpectralMorphCrossSynthNode::FftSize>& r,
                std::array<float, SpectralMorphCrossSynthNode::FftSize>& im) noexcept
{
    constexpr std::size_t N = SpectralMorphCrossSynthNode::FftSize;
    std::size_t j = 0;
    for (std::size_t i = 0; i < N - 1; ++i)
    {
        if (i < j)
        {
            std::swap(r[i], r[j]);
            std::swap(im[i], im[j]);
        }
        std::size_t k = N / 2;
        while (k <= j)
        {
            j -= k;
            k /= 2;
        }
        j += k;
    }
}

void fftRadix2(std::array<float, SpectralMorphCrossSynthNode::FftSize>& r,
               std::array<float, SpectralMorphCrossSynthNode::FftSize>& im,
               bool inverse) noexcept
{
    constexpr std::size_t N = SpectralMorphCrossSynthNode::FftSize;
    bitReverse(r, im);

    for (std::size_t len = 2; len <= N; len <<= 1)
    {
        const double angle = (inverse ? 2.0 : -2.0) * std::numbers::pi_v<double> / static_cast<double>(len);
        const double wlenReal = std::cos(angle);
        const double wlenImag = std::sin(angle);

        for (std::size_t i = 0; i < N; i += len)
        {
            double wReal = 1.0;
            double wImag = 0.0;
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto uR = r[i + j];
                const auto uI = im[i + j];
                const auto vR = static_cast<float>(r[i + j + len / 2] * wReal - im[i + j + len / 2] * wImag);
                const auto vI = static_cast<float>(r[i + j + len / 2] * wImag + im[i + j + len / 2] * wReal);

                r[i + j] = uR + vR;
                im[i + j] = uI + vI;
                r[i + j + len / 2] = uR - vR;
                im[i + j + len / 2] = uI - vI;

                const double nextWReal = wReal * wlenReal - wImag * wlenImag;
                const double nextWImag = wReal * wlenImag + wImag * wlenReal;
                wReal = nextWReal;
                wImag = nextWImag;
            }
        }
    }

    if (inverse)
    {
        constexpr float invN = 1.0f / static_cast<float>(N);
        for (std::size_t i = 0; i < N; ++i)
        {
            r[i] *= invN;
            im[i] *= invN;
        }
    }
}

} // namespace

SpectralMorphCrossSynthNode::SpectralMorphCrossSynthNode() noexcept
{
    // Generate periodic Hann analysis window
    for (std::size_t i = 0; i < FftSize; ++i)
    {
        const float phase = static_cast<float>(i) / static_cast<float>(FftSize);
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * phase));
    }
    reset();
}

bool SpectralMorphCrossSynthNode::prepare(
    double sampleRate,
    std::size_t /*maxBlockSize*/,
    std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void SpectralMorphCrossSynthNode::reset() noexcept
{
    m_inputBufferA.fill(0.0f);
    m_inputBufferB.fill(0.0f);
    m_inputPos = 0;

    m_outputOlaBufferL.fill(0.0f);
    m_outputOlaBufferR.fill(0.0f);
    m_outputReadPos = 0;

    m_magA.fill(0.0f);
    m_phaseA.fill(0.0f);
    m_magB.fill(0.0f);
    m_phaseB.fill(0.0f);
    m_morphMag.fill(0.0f);
    m_morphPhase.fill(0.0f);
    m_synthFrameL.fill(0.0f);
}

void SpectralMorphCrossSynthNode::computeFft(
    const std::array<float, FftSize>& input,
    std::array<float, NumBins>& mag,
    std::array<float, NumBins>& phase) noexcept
{
    std::array<float, FftSize> realBuf {};
    std::array<float, FftSize> imagBuf {};

    for (std::size_t i = 0; i < FftSize; ++i)
    {
        realBuf[i] = input[i] * m_window[i];
        imagBuf[i] = 0.0f;
    }

    fftRadix2(realBuf, imagBuf, false);

    for (std::size_t k = 0; k < NumBins; ++k)
    {
        const float r = realBuf[k];
        const float im = imagBuf[k];
        mag[k] = std::sqrt(r * r + im * im);
        phase[k] = std::atan2(im, r);
    }
}

void SpectralMorphCrossSynthNode::computeIfft(
    const std::array<float, NumBins>& mag,
    const std::array<float, NumBins>& phase,
    std::array<float, FftSize>& output) noexcept
{
    std::array<float, FftSize> realBuf {};
    std::array<float, FftSize> imagBuf {};

    for (std::size_t k = 0; k < NumBins; ++k)
    {
        realBuf[k] = mag[k] * std::cos(phase[k]);
        imagBuf[k] = mag[k] * std::sin(phase[k]);
    }
    // Hermitian symmetry for real inverse FFT
    for (std::size_t k = NumBins; k < FftSize; ++k)
    {
        realBuf[k] = realBuf[FftSize - k];
        imagBuf[k] = -imagBuf[FftSize - k];
    }

    fftRadix2(realBuf, imagBuf, true);

    // Apply synthesis window and 75% overlap-add normalization (4x overlap -> sum of squared Hann is 1.5)
    constexpr float olaNorm = 1.0f / 1.5f;
    for (std::size_t i = 0; i < FftSize; ++i)
    {
        output[i] = realBuf[i] * m_window[i] * olaNorm;
    }
}

void SpectralMorphCrossSynthNode::processStftFrame() noexcept
{
    // 1. Forward FFT on Stream A and Stream B
    computeFft(m_inputBufferA, m_magA, m_phaseA);
    computeFft(m_inputBufferB, m_magB, m_phaseB);

    // 2. Perform Spectral Domain Morphing
    for (std::size_t k = 0; k < NumBins; ++k)
    {
        switch (m_mode)
        {
        case SpectralMorphMode::linearBlend:
            // Linear magnitude blend
            m_morphMag[k] = (1.0f - m_morphX) * m_magA[k] + m_morphX * m_magB[k];
            m_morphPhase[k] = (m_morphX < 0.5f) ? m_phaseA[k] : m_phaseB[k];
            break;

        case SpectralMorphMode::crossMultiply:
        {
            // Zero-phase harmonic envelope cross-multiplication (Vocoder / cross-synth)
            const float cross = std::sqrt(m_magA[k] * m_magB[k]) * 1.4142f;
            m_morphMag[k] = (1.0f - m_morphX) * m_magA[k] + m_morphX * cross;
            m_morphPhase[k] = m_phaseA[k]; // Preserve carrier harmonic phase
            break;
        }

        case SpectralMorphMode::formantWarp:
        {
            // Spectral envelope stretch / formant shift
            const float warpScale = 0.5f + m_formantWarpY; // 0.5 to 1.5
            const auto srcBin = static_cast<std::size_t>(std::clamp(static_cast<float>(k) * warpScale, 0.0f, static_cast<float>(NumBins - 1)));
            const float warpedMagA = m_magA[srcBin];
            m_morphMag[k] = (1.0f - m_morphX) * warpedMagA + m_morphX * m_magB[k];
            m_morphPhase[k] = m_phaseA[k];
            break;
        }
        }
    }

    // 3. Inverse FFT Reconstruction
    computeIfft(m_morphMag, m_morphPhase, m_synthFrameL);

    // 4. Overlap-Add accumulation into circular buffer
    constexpr std::size_t olaCap = FftSize * 2;
    for (std::size_t i = 0; i < FftSize; ++i)
    {
        const auto writeIdx = (m_outputReadPos + i) % olaCap;
        m_outputOlaBufferL[writeIdx] += m_synthFrameL[i];
        m_outputOlaBufferR[writeIdx] += m_synthFrameL[i];
    }
}

void SpectralMorphCrossSynthNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.frameCount == 0 || audio.channelCount == 0)
    {
        return;
    }

    const std::size_t numSamples = audio.frameCount;
    const std::size_t numChannels = audio.channelCount;
    float* outL = audio.channels[0];
    float* outR = (numChannels > 1) ? audio.channels[1] : nullptr;

    const float* sc0 = nullptr;
    const float* sc1 = nullptr;
    if (context.sidechain.channels != nullptr && context.sidechain.channelCount > 0)
    {
        sc0 = context.sidechain.channels[0];
        sc1 = (context.sidechain.channelCount >= 2) ? context.sidechain.channels[1] : sc0;
    }

    constexpr std::size_t olaCap = FftSize * 2;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inL = outL[i];
        const float inR = (outR != nullptr) ? outR[i] : inL;

        const float sideL = (sc0 != nullptr) ? sc0[i] : inL;
        const float sideR = (sc1 != nullptr) ? sc1[i] : inR;

        // Feed sliding STFT input buffers
        m_inputBufferA[m_inputPos] = 0.5f * (inL + inR);
        m_inputBufferB[m_inputPos] = 0.5f * (sideL + sideR);
        ++m_inputPos;

        if (m_inputPos >= FftSize)
        {
            processStftFrame();

            // Shift input buffer by HopSize (75% overlap)
            std::copy(m_inputBufferA.begin() + HopSize, m_inputBufferA.end(), m_inputBufferA.begin());
            std::copy(m_inputBufferB.begin() + HopSize, m_inputBufferB.end(), m_inputBufferB.begin());
            m_inputPos = FftSize - HopSize;
        }

        // Read overlap-added reconstructed audio
        const float morphL = m_outputOlaBufferL[m_outputReadPos];
        const float morphR = m_outputOlaBufferR[m_outputReadPos];

        m_outputOlaBufferL[m_outputReadPos] = 0.0f;
        m_outputOlaBufferR[m_outputReadPos] = 0.0f;
        m_outputReadPos = (m_outputReadPos + 1) % olaCap;

        // Dry/Wet blend
        outL[i] = m_mix * morphL + (1.0f - m_mix) * inL;
        if (outR != nullptr)
        {
            outR[i] = m_mix * morphR + (1.0f - m_mix) * inR;
        }
    }
}

} // namespace dawg::engine::dsp
