#include <dawg/engine/SimdMath.h>

#include <algorithm>
#include <cmath>

#if defined(__x86_64__) || defined(_M_X64)
    #if defined(__AVX2__)
        #include <immintrin.h>
        #define DAWG_HAS_AVX2 1
    #endif
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define DAWG_HAS_NEON 1
#endif

namespace dawg::engine {

void SimdMath::sumChannels(float* dest, const float* src, std::size_t numSamples) noexcept
{
    if (dest == nullptr || src == nullptr || numSamples == 0)
        return;

    std::size_t i = 0;

#if defined(DAWG_HAS_AVX2)
    const std::size_t simdEnd = numSamples & ~std::size_t(7);
    for (; i < simdEnd; i += 8)
    {
        __m256 d = _mm256_loadu_ps(dest + i);
        __m256 s = _mm256_loadu_ps(src + i);
        _mm256_storeu_ps(dest + i, _mm256_add_ps(d, s));
    }
#elif defined(DAWG_HAS_NEON)
    const std::size_t simdEnd = numSamples & ~std::size_t(3);
    for (; i < simdEnd; i += 4)
    {
        float32x4_t d = vld1q_f32(dest + i);
        float32x4_t s = vld1q_f32(src + i);
        vst1q_f32(dest + i, vaddq_f32(d, s));
    }
#endif

    // Scalar unrolled tail
    for (; i < numSamples; ++i)
    {
        dest[i] += src[i];
    }
}

void SimdMath::applyGain(float* data, float gain, std::size_t numSamples) noexcept
{
    if (data == nullptr || numSamples == 0)
        return;

    if (std::abs(gain - 1.0f) < 1e-7f)
        return;

    std::size_t i = 0;

#if defined(DAWG_HAS_AVX2)
    const __m256 g = _mm256_set1_ps(gain);
    const std::size_t simdEnd = numSamples & ~std::size_t(7);
    for (; i < simdEnd; i += 8)
    {
        __m256 d = _mm256_loadu_ps(data + i);
        _mm256_storeu_ps(data + i, _mm256_mul_ps(d, g));
    }
#elif defined(DAWG_HAS_NEON)
    const float32x4_t g = vdupq_n_f32(gain);
    const std::size_t simdEnd = numSamples & ~std::size_t(3);
    for (; i < simdEnd; i += 4)
    {
        float32x4_t d = vld1q_f32(data + i);
        vst1q_f32(data + i, vmulq_f32(d, g));
    }
#endif

    for (; i < numSamples; ++i)
    {
        data[i] *= gain;
    }
}

void SimdMath::scaleAndAccumulate(float* dest, const float* src, float gain, std::size_t numSamples) noexcept
{
    if (dest == nullptr || src == nullptr || numSamples == 0)
        return;

    std::size_t i = 0;

#if defined(DAWG_HAS_AVX2)
    const __m256 g = _mm256_set1_ps(gain);
    const std::size_t simdEnd = numSamples & ~std::size_t(7);
    for (; i < simdEnd; i += 8)
    {
        __m256 d = _mm256_loadu_ps(dest + i);
        __m256 s = _mm256_loadu_ps(src + i);
        #if defined(__FMA__)
            d = _mm256_fmadd_ps(s, g, d);
        #else
            d = _mm256_add_ps(d, _mm256_mul_ps(s, g));
        #endif
        _mm256_storeu_ps(dest + i, d);
    }
#elif defined(DAWG_HAS_NEON)
    const float32x4_t g = vdupq_n_f32(gain);
    const std::size_t simdEnd = numSamples & ~std::size_t(3);
    for (; i < simdEnd; i += 4)
    {
        float32x4_t d = vld1q_f32(dest + i);
        float32x4_t s = vld1q_f32(src + i);
        vst1q_f32(dest + i, vmlaq_f32(d, s, g));
    }
#endif

    for (; i < numSamples; ++i)
    {
        dest[i] += src[i] * gain;
    }
}

void SimdMath::applyPanStereo(float* left, float* right, float pan, std::size_t numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples == 0)
        return;

    const float clampedPan = std::clamp(pan, -1.0f, 1.0f);
    // Constant power panning curve: sqrt(2)/2 at center (-3 dB equal power)
    constexpr float piOver2 = 1.5707963267948966f;
    const float angle = (clampedPan + 1.0f) * 0.5f * piOver2;
    const float lGain = std::cos(angle);
    const float rGain = std::sin(angle);

    applyGain(left, lGain, numSamples);
    applyGain(right, rGain, numSamples);
}

void SimdMath::processBiquad(float* data, const float coeffs[5], float state[2], std::size_t numSamples) noexcept
{
    if (data == nullptr || coeffs == nullptr || state == nullptr || numSamples == 0)
        return;

    const float b0 = coeffs[0];
    const float b1 = coeffs[1];
    const float b2 = coeffs[2];
    const float a1 = coeffs[3];
    const float a2 = coeffs[4];

    float s1 = state[0];
    float s2 = state[1];

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float x = data[i];
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        data[i] = y;
    }

    // Denormal protection
    if (std::abs(s1) < 1e-15f) s1 = 0.0f;
    if (std::abs(s2) < 1e-15f) s2 = 0.0f;

    state[0] = s1;
    state[1] = s2;
}

} // namespace dawg::engine
