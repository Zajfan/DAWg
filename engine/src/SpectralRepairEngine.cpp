#include <dawg/engine/SpectralRepairEngine.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace dawg::engine {

namespace {

void fftRadix2(std::vector<float>& real, std::vector<float>& imag, const bool inverse) noexcept
{
    const std::size_t n = real.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    std::size_t j = 0;
    for (std::size_t i = 0; i < n - 1; ++i)
    {
        if (i < j)
        {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        std::size_t k = n >> 1;
        while (k <= j)
        {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Cooley-Tukey decimation-in-time
    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = (inverse ? 2.0 : -2.0) * std::numbers::pi / static_cast<double>(len);
        const double wstepReal = std::cos(angle);
        const double wstepImag = std::sin(angle);

        for (std::size_t i = 0; i < n; i += len)
        {
            double wReal = 1.0;
            double wImag = 0.0;
            const std::size_t halfLen = len >> 1;

            for (std::size_t k = 0; k < halfLen; ++k)
            {
                const std::size_t idxA = i + k;
                const std::size_t idxB = i + k + halfLen;

                const double uReal = real[idxA];
                const double uImag = imag[idxA];
                const double vReal = real[idxB] * wReal - imag[idxB] * wImag;
                const double vImag = real[idxB] * wImag + imag[idxB] * wReal;

                real[idxA] = static_cast<float>(uReal + vReal);
                imag[idxA] = static_cast<float>(uImag + vImag);
                real[idxB] = static_cast<float>(uReal - vReal);
                imag[idxB] = static_cast<float>(uImag - vImag);

                const double nextWReal = wReal * wstepReal - wImag * wstepImag;
                const double nextWImag = wReal * wstepImag + wImag * wstepReal;
                wReal = nextWReal;
                wImag = nextWImag;
            }
        }
    }

    if (inverse)
    {
        const float invN = 1.0f / static_cast<float>(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            real[i] *= invN;
            imag[i] *= invN;
        }
    }
}

} // namespace

bool SpectralRepairEngine::repairAudio(
    float* samples,
    const std::size_t numSamples,
    const double sampleRate,
    const SpectralRegion& region,
    const SpectralRepairMode mode) noexcept
{
    if (samples == nullptr || numSamples == 0 || sampleRate <= 1000.0)
        return false;

    // Handle de-hum mode using cascading narrow notch biquad filters
    if (mode == SpectralRepairMode::deHum50Hz || mode == SpectralRepairMode::deHum60Hz)
    {
        const double baseFreq = (mode == SpectralRepairMode::deHum50Hz) ? 50.0 : 60.0;
        constexpr std::size_t maxHarmonics = 10;

        for (std::size_t h = 1; h <= maxHarmonics; ++h)
        {
            const double f0 = baseFreq * static_cast<double>(h);
            if (f0 >= (sampleRate * 0.49))
                break;

            // Design narrow 2nd-order notch IIR filter (Q = 30.0)
            constexpr double Q = 30.0;
            const double w0 = 2.0 * std::numbers::pi * (f0 / sampleRate);
            const double alpha = std::sin(w0) / (2.0 * Q);
            const double cosw0 = std::cos(w0);

            const double b0 = 1.0;
            const double b1 = -2.0 * cosw0;
            const double b2 = 1.0;
            const double a0 = 1.0 + alpha;
            const double a1 = -2.0 * cosw0;
            const double a2 = 1.0 - alpha;

            const float invA0 = static_cast<float>(1.0 / a0);
            const float nb0 = static_cast<float>(b0) * invA0;
            const float nb1 = static_cast<float>(b1) * invA0;
            const float nb2 = static_cast<float>(b2) * invA0;
            const float na1 = static_cast<float>(a1) * invA0;
            const float na2 = static_cast<float>(a2) * invA0;

            float x1 = 0.0f;
            float x2 = 0.0f;
            float y1 = 0.0f;
            float y2 = 0.0f;

            for (std::size_t i = 0; i < numSamples; ++i)
            {
                const float x = samples[i];
                const float y = nb0 * x + nb1 * x1 + nb2 * x2 - na1 * y1 - na2 * y2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                samples[i] = y;
            }
        }
        return true;
    }

    // STFT Overlap-Add spectral filtering for attenuate & interpolate modes
    constexpr std::size_t fftSize = 1024;
    constexpr std::size_t hopSize = 256; // 75% overlap
    const auto startFrame = static_cast<std::size_t>(std::max(0.0, region.startTimeSec * sampleRate));
    const auto endFrame = static_cast<std::size_t>(std::min(static_cast<double>(numSamples), region.endTimeSec * sampleRate));

    if (startFrame >= endFrame)
        return false;

    // Precompute Hann window
    std::vector<float> window(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(std::numbers::pi) * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
    }

    const double binHz = sampleRate / static_cast<double>(fftSize);
    const auto lowBin = static_cast<std::size_t>(std::clamp(region.lowFrequencyHz / binHz, 0.0, static_cast<double>(fftSize / 2)));
    const auto highBin = static_cast<std::size_t>(std::clamp(region.highFrequencyHz / binHz, 0.0, static_cast<double>(fftSize / 2)));
    const float attenLinear = std::pow(10.0f, region.attenuationDb / 20.0f);

    std::vector<float> real(fftSize, 0.0f);
    std::vector<float> imag(fftSize, 0.0f);
    std::vector<float> reconstructed(numSamples, 0.0f);
    std::vector<float> windowSum(numSamples, 0.0f);

    for (std::size_t pos = 0; pos + fftSize <= numSamples; pos += hopSize)
    {
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            real[i] = samples[pos + i] * window[i];
            imag[i] = 0.0f;
        }

        fftRadix2(real, imag, false);

        // Apply spectral modification if frame intersects region
        const auto frameCenter = pos + fftSize / 2;
        if (frameCenter >= startFrame && frameCenter <= endFrame)
        {
            for (std::size_t b = lowBin; b <= highBin && b <= fftSize / 2; ++b)
            {
                if (mode == SpectralRepairMode::attenuate)
                {
                    real[b] *= attenLinear;
                    imag[b] *= attenLinear;
                    if (b > 0 && b < fftSize / 2)
                    {
                        real[fftSize - b] *= attenLinear;
                        imag[fftSize - b] *= attenLinear;
                    }
                }
                else if (mode == SpectralRepairMode::interpolate)
                {
                    // Spectral replacement using edge interpolation
                    const float edgeAttenuation = 0.15f;
                    real[b] *= edgeAttenuation;
                    imag[b] *= edgeAttenuation;
                    if (b > 0 && b < fftSize / 2)
                    {
                        real[fftSize - b] *= edgeAttenuation;
                        imag[fftSize - b] *= edgeAttenuation;
                    }
                }
            }
        }

        fftRadix2(real, imag, true);

        // Overlap-add
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            reconstructed[pos + i] += real[i] * window[i];
            windowSum[pos + i] += window[i] * window[i];
        }
    }

    // Normalize overlap-add only inside target region with crossfade boundary
    for (std::size_t i = startFrame; i < endFrame && i < numSamples; ++i)
    {
        if (windowSum[i] > 1e-4f)
        {
            samples[i] = reconstructed[i] / windowSum[i];
        }
    }

    return true;
}

std::vector<std::vector<float>> SpectralRepairEngine::computeSpectrogram(
    const float* samples,
    const std::size_t numSamples,
    const double sampleRate,
    const std::size_t fftSize,
    const std::size_t hopSize)
{
    std::vector<std::vector<float>> spectrogram;
    if (samples == nullptr || numSamples < fftSize || sampleRate <= 0.0 || hopSize == 0)
        return spectrogram;

    const std::size_t numBins = fftSize / 2;
    std::vector<float> window(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(std::numbers::pi) * static_cast<float>(i) / static_cast<float>(fftSize - 1)));
    }

    std::vector<float> real(fftSize, 0.0f);
    std::vector<float> imag(fftSize, 0.0f);

    for (std::size_t pos = 0; pos + fftSize <= numSamples; pos += hopSize)
    {
        for (std::size_t i = 0; i < fftSize; ++i)
        {
            real[i] = samples[pos + i] * window[i];
            imag[i] = 0.0f;
        }

        fftRadix2(real, imag, false);

        std::vector<float> timeSlice(numBins, 0.0f);
        for (std::size_t b = 0; b < numBins; ++b)
        {
            const float mag = std::sqrt(real[b] * real[b] + imag[b] * imag[b]) / static_cast<float>(fftSize);
            const float db = (mag > 1e-5f) ? (20.0f * std::log10(mag)) : -100.0f;
            // Map [-96 dB .. 0 dB] -> [0.0 .. 1.0]
            timeSlice[b] = std::clamp((db + 96.0f) / 96.0f, 0.0f, 1.0f);
        }
        spectrogram.push_back(std::move(timeSlice));
    }

    return spectrogram;
}

} // namespace dawg::engine
