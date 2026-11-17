#pragma once

#include <cstddef>
#include <cstdint>

namespace dawg::engine {

class SimdMath final
{
public:
    SimdMath() = delete;

    /// Sums src into dest: dest[i] += src[i] for i = 0..numSamples-1
    static void sumChannels(float* dest, const float* src, std::size_t numSamples) noexcept;

    /// Multiplies data in-place by a constant gain factor
    static void applyGain(float* data, float gain, std::size_t numSamples) noexcept;

    /// Accumulates src with gain into dest: dest[i] += src[i] * gain
    static void scaleAndAccumulate(float* dest, const float* src, float gain, std::size_t numSamples) noexcept;

    /// Scales stereo channels by panning coefficients:
    /// pan in [-1.0, 1.0], leftGain and rightGain applied accordingly.
    static void applyPanStereo(float* left, float* right, float pan, std::size_t numSamples) noexcept;

    /// Vectorized Direct-Form II Transposed Biquad Filter
    /// y[n] = b0*x[n] + s1; s1 = b1*x[n] - a1*y[n] + s2; s2 = b2*x[n] - a2*y[n]
    static void processBiquad(float* data, const float coeffs[5], float state[2], std::size_t numSamples) noexcept;
};

} // namespace dawg::engine
