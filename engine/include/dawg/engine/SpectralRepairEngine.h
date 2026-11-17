#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace dawg::engine {

enum class SpectralRepairMode : std::uint8_t
{
    attenuate,   // Gain reduction in target time-frequency bounds
    interpolate, // Replace corrupted bin magnitudes with median of neighboring clean time slices
    deHum50Hz,   // Notch 50 Hz European electrical hum and harmonics
    deHum60Hz    // Notch 60 Hz US electrical hum and harmonics
};

struct SpectralRegion
{
    double startTimeSec {0.0};
    double endTimeSec {0.0};
    double lowFrequencyHz {20.0};
    double highFrequencyHz {20000.0};
    float attenuationDb {-18.0f}; // For attenuate mode
};

class SpectralRepairEngine final
{
public:
    SpectralRepairEngine() = default;
    ~SpectralRepairEngine() = default;

    /// In-place spectral repair of audio channel
    static bool repairAudio(
        float* samples,
        std::size_t numSamples,
        double sampleRate,
        const SpectralRegion& region,
        SpectralRepairMode mode) noexcept;

    /// Computes 2D power spectrogram magnitudes (frequency bins x time frames)
    /// for UI heat-map visualization. Normalized to [0.0, 1.0] in dB scale [-96dB .. 0dB].
    [[nodiscard]] static std::vector<std::vector<float>> computeSpectrogram(
        const float* samples,
        std::size_t numSamples,
        double sampleRate,
        std::size_t fftSize = 1024,
        std::size_t hopSize = 256);
};

} // namespace dawg::engine
