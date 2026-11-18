#include "dawg/engine/dsp/AdaptiveResonanceSuppressorNode.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

AdaptiveResonanceSuppressorNode::AdaptiveResonanceSuppressorNode() noexcept
{
    initWindowAndErb();
    reset();
}

void AdaptiveResonanceSuppressorNode::initWindowAndErb() noexcept
{
    // Periodic Hann window
    for (std::size_t i = 0; i < FftSize; ++i) {
        m_window[i] = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(FftSize)));
    }

    const float binWidth = static_cast<float>(m_sampleRate) / static_cast<float>(FftSize);
    for (std::size_t k = 0; k < HalfFft; ++k) {
        const float freq = static_cast<float>(k) * binWidth;
        m_binFrequencies[k] = freq;
        // Glasberg & Moore (1990) Equivalent Rectangular Bandwidth (ERB)
        m_erbBandwidths[k] = 24.7f * (4.37e-3f * freq + 1.0f);
    }
}

bool AdaptiveResonanceSuppressorNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    initWindowAndErb();
    reset();
    return true;
}

void AdaptiveResonanceSuppressorNode::reset() noexcept
{
    for (std::size_t c = 0; c < 2; ++c) {
        m_inputBuffer[c].fill(0.0f);
        m_outputBuffer[c].fill(0.0f);
        m_inWritePos[c] = 0;
        m_outReadPos[c] = 0;
        m_hopCounter[c] = 0;
        m_smoothedMagnitudes[c].fill(0.0f);
    }
}

void AdaptiveResonanceSuppressorNode::fft(std::array<float, FftSize>& real, std::array<float, FftSize>& imag) noexcept
{
    // Bit-reversal permutation
    std::size_t j = 0;
    for (std::size_t i = 0; i < FftSize - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        std::size_t k = FftSize / 2;
        while (k <= j) {
            j -= k;
            k /= 2;
        }
        j += k;
    }

    // Cooley-Tukey butterflies
    for (std::size_t len = 2; len <= FftSize; len <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float> / static_cast<float>(len);
        const float wstepR = std::cos(angle);
        const float wstepI = std::sin(angle);

        for (std::size_t i = 0; i < FftSize; i += len) {
            float wR = 1.0f;
            float wI = 0.0f;
            const std::size_t halfLen = len / 2;

            for (std::size_t step = 0; step < halfLen; ++step) {
                const std::size_t u = i + step;
                const std::size_t v = u + halfLen;

                const float tr = wR * real[v] - wI * imag[v];
                const float ti = wR * imag[v] + wI * real[v];

                real[v] = real[u] - tr;
                imag[v] = imag[u] - ti;
                real[u] += tr;
                imag[u] += ti;

                const float nextWR = wR * wstepR - wI * wstepI;
                wI = wR * wstepI + wI * wstepR;
                wR = nextWR;
            }
        }
    }
}

void AdaptiveResonanceSuppressorNode::ifft(std::array<float, FftSize>& real, std::array<float, FftSize>& imag) noexcept
{
    for (std::size_t i = 0; i < FftSize; ++i) {
        imag[i] = -imag[i];
    }
    fft(real, imag);
    constexpr float invN = 1.0f / static_cast<float>(FftSize);
    for (std::size_t i = 0; i < FftSize; ++i) {
        real[i] *= invN;
        imag[i] = -imag[i] * invN;
    }
}

void AdaptiveResonanceSuppressorNode::analyzeAndSuppress(
    std::array<float, FftSize>& real,
    std::array<float, FftSize>& imag,
    std::size_t channelIdx) noexcept
{
    std::array<float, HalfFft> mag {};
    for (std::size_t k = 0; k < HalfFft; ++k) {
        mag[k] = std::sqrt(real[k] * real[k] + imag[k] * imag[k]);
    }

    // Temporal smoothing of spectral envelope
    constexpr float alpha = 0.35f;
    for (std::size_t k = 0; k < HalfFft; ++k) {
        m_smoothedMagnitudes[channelIdx][k] = (1.0f - alpha) * m_smoothedMagnitudes[channelIdx][k] + alpha * mag[k];
    }

    const float binWidth = static_cast<float>(m_sampleRate) / static_cast<float>(FftSize);

    // Dynamic Resonance and Gullfoss-style Spectral Balancing
    for (std::size_t k = 0; k < HalfFft; ++k) {
        const float erb = m_erbBandwidths[k];
        const auto binSpan = static_cast<int>(std::max(1.0f, std::round(erb / binWidth)));

        // Compute local ERB background spectral floor
        float localFloor = 0.0f;
        int count = 0;
        const auto startBin = std::max(0, static_cast<int>(k) - binSpan);
        const auto endBin = std::min(static_cast<int>(HalfFft - 1), static_cast<int>(k) + binSpan);

        for (int b = startBin; b <= endBin; ++b) {
            localFloor += m_smoothedMagnitudes[channelIdx][static_cast<std::size_t>(b)];
            ++count;
        }
        localFloor = count > 0 ? (localFloor / static_cast<float>(count)) : mag[k];

        // Resonance suppression factor
        float gain = 1.0f;
        if (localFloor > 1e-9f) {
            const float prominence = mag[k] / localFloor;
            if (prominence > 1.05f) {
                const float excess = (prominence - 1.0f) * m_sharpness;
                const float reduction = 1.0f / (1.0f + m_depth * excess * (1.0f + m_tame));
                gain = reduction;
            } else if (prominence < 0.95f && m_recover > 0.0f) {
                // Recover buried low-level details below masking floor
                const float deficit = (1.0f - prominence);
                gain = 1.0f + m_recover * deficit * 0.5f;
            }
        }

        // Spectral Tilt (-6dB to +6dB per decade)
        if (std::abs(m_spectralTilt) > 1e-4f && m_binFrequencies[k] > 20.0f) {
            const float octavesFrom1k = std::log2(m_binFrequencies[k] / 1000.0f);
            const float tiltGain = std::pow(10.0f, (m_spectralTilt * octavesFrom1k * 0.1f) * 0.05f);
            gain *= tiltGain;
        }

        gain = std::clamp(gain, 0.01f, 4.0f);

        // Apply gain or solo delta
        if (m_auditionDelta) {
            const float deltaGain = std::max(0.0f, 1.0f - gain);
            real[k] *= deltaGain;
            imag[k] *= deltaGain;
        } else {
            real[k] *= gain;
            imag[k] *= gain;
        }

        // Mirror conjugate symmetric bins for real IFFT
        if (k > 0 && k < HalfFft - 1) {
            real[FftSize - k] = real[k];
            imag[FftSize - k] = -imag[k];
        }
    }
}

void AdaptiveResonanceSuppressorNode::processStftChannel(
    const float* input,
    float* output,
    std::size_t frameCount,
    std::size_t channelIdx) noexcept
{
    auto& inBuf = m_inputBuffer[channelIdx];
    auto& outBuf = m_outputBuffer[channelIdx];
    auto& inPos = m_inWritePos[channelIdx];
    auto& outPos = m_outReadPos[channelIdx];
    auto& hopCount = m_hopCounter[channelIdx];

    // Synthesis scaling constant for 75% overlap Hann window: 1 / (3/8 * 4) = 2/3
    constexpr float olaNorm = 2.0f / 3.0f;

    for (std::size_t i = 0; i < frameCount; ++i) {
        inBuf[inPos] = input[i];
        inPos = (inPos + 1) % RingBufferSize;

        output[i] = outBuf[outPos];
        outBuf[outPos] = 0.0f; // Clear after read
        outPos = (outPos + 1) % RingBufferSize;

        ++hopCount;
        if (hopCount >= HopSize) {
            hopCount = 0;

            // Extract windowed analysis frame
            std::array<float, FftSize> real {};
            std::array<float, FftSize> imag {};

            for (std::size_t n = 0; n < FftSize; ++n) {
                const std::size_t rIdx = (inPos + RingBufferSize - FftSize + n) % RingBufferSize;
                real[n] = inBuf[rIdx] * m_window[n];
                imag[n] = 0.0f;
            }

            fft(real, imag);
            analyzeAndSuppress(real, imag, channelIdx);
            ifft(real, imag);

            // Overlap-add into output ring buffer
            for (std::size_t n = 0; n < FftSize; ++n) {
                const std::size_t wIdx = (outPos + n) % RingBufferSize;
                outBuf[wIdx] += real[n] * m_window[n] * olaNorm;
            }
        }
    }
}

void AdaptiveResonanceSuppressorNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channelCount == 0 || audio.frameCount == 0 || audio.channels[0] == nullptr) {
        return;
    }

    processStftChannel(audio.channels[0], audio.channels[0], audio.frameCount, 0);

    if (audio.channelCount > 1 && audio.channels[1] != nullptr) {
        processStftChannel(audio.channels[1], audio.channels[1], audio.frameCount, 1);
    }
}

} // namespace dawg::engine::dsp
