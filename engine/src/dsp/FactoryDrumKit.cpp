#include <dawg/engine/dsp/FactoryDrumKit.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

const char* FactoryDrumKit::hitName(const DrumHitType type) noexcept
{
    switch (type)
    {
    case DrumHitType::kick808:     return "808 Sub Boom Kick";
    case DrumHitType::kick909:     return "909 Punch Studio Kick";
    case DrumHitType::snare808:    return "808 Crisp Snare";
    case DrumHitType::clap808:     return "808 Triple Flam Clap";
    case DrumHitType::hihatClosed: return "Trap Tight Closed Hat";
    case DrumHitType::hihatOpen:   return "Trap Sizzle Open Hat";
    case DrumHitType::rimshot:     return "Analog Wood Rimshot";
    case DrumHitType::cowbell808:  return "Classic 808 Cowbell";
    }
    return "Drum Hit";
}

std::vector<float> FactoryDrumKit::generateHit(const DrumHitType type, const double sampleRate)
{
    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    std::vector<float> buffer;

    // Fast PRNG for noise
    std::uint32_t rng = 0x87654321;
    const auto whiteNoise = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return static_cast<float>(static_cast<std::int32_t>(rng)) * (1.0f / 2147483648.0f);
    };

    switch (type)
    {
    // 1. 808 Sub Boom Kick (Pitch drop 140 Hz -> 42 Hz with tube saturation)
    case DrumHitType::kick808:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.85); // 850 ms
        buffer.resize(numSamples, 0.0f);
        double phase = 0.0;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            // Pitch envelope: drops exponentially from 145 Hz to 42 Hz
            const double f = 42.0 + 103.0 * std::exp(-t * 24.0);
            phase += (f * 2.0 * std::numbers::pi) / sr;

            // Amplitude envelope: instant punch, long sub tail
            const double amp = std::exp(-t * 3.8);

            // Click transient in first 5 ms
            const double click = (t < 0.006) ? (std::sin(t * 1200.0 * 2.0 * std::numbers::pi) * std::exp(-t * 500.0) * 0.35) : 0.0;

            const double raw = (std::sin(phase) + click) * amp;
            // Soft tube saturation
            buffer[i] = static_cast<float>(std::tanh(raw * 1.35));
        }
        break;
    }

    // 2. 909 Punch Studio Kick (Punchy beater click + 55 Hz resonant thump)
    case DrumHitType::kick909:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.45); // 450 ms
        buffer.resize(numSamples, 0.0f);
        double phase = 0.0;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            const double f = 55.0 + 180.0 * std::exp(-t * 45.0);
            phase += (f * 2.0 * std::numbers::pi) / sr;

            const double amp = std::exp(-t * 7.5);
            const double beaterClick = (t < 0.015) ? (whiteNoise() * std::exp(-t * 300.0) * 0.45) : 0.0;

            const double raw = (std::sin(phase) * 0.9 + beaterClick) * amp;
            buffer[i] = static_cast<float>(std::tanh(raw * 1.2));
        }
        break;
    }

    // 3. 808 Crisp Snare (Dual body sines 180 Hz & 330 Hz + shaped noise burst)
    case DrumHitType::snare808:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.35); // 350 ms
        buffer.resize(numSamples, 0.0f);
        double p1 = 0.0;
        double p2 = 0.0;
        float noiseFilterState = 0.0f;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            p1 += (185.0 * std::exp(-t * 20.0) * 2.0 * std::numbers::pi) / sr;
            p2 += (330.0 * std::exp(-t * 15.0) * 2.0 * std::numbers::pi) / sr;

            const double bodyAmp = std::exp(-t * 18.0) * 0.60;
            const double body = (std::sin(p1) + std::sin(p2) * 0.5) * bodyAmp;

            // High pass filtered noise for snappy wires
            const float rawNoise = whiteNoise();
            noiseFilterState = 0.85f * (noiseFilterState + rawNoise - 0.85f * noiseFilterState);
            const double noiseAmp = std::exp(-t * 12.0) * 0.75;

            buffer[i] = static_cast<float>(std::tanh((body + noiseFilterState * noiseAmp) * 1.15));
        }
        break;
    }

    // 4. 808 Triple Flam Clap
    case DrumHitType::clap808:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.45); // 450 ms
        buffer.resize(numSamples, 0.0f);
        float bpState1 = 0.0f;
        float bpState2 = 0.0f;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;

            // 3 micro-flams at t=0, t=11ms, t=23ms, then final burst at t=35ms
            double env = 0.0;
            if (t < 0.011) env = std::exp(-t * 180.0) * 0.7;
            else if (t < 0.023) env = std::exp(-(t - 0.011) * 180.0) * 0.8;
            else if (t < 0.035) env = std::exp(-(t - 0.023) * 180.0) * 0.9;
            else env = std::exp(-(t - 0.035) * 16.0); // reverb tail

            // Bandpass filter around 1100 Hz
            const float inN = whiteNoise();
            bpState1 += 0.28f * (inN - bpState1);
            bpState2 += 0.28f * (bpState1 - bpState2);
            const float bpOut = bpState1 - bpState2;

            buffer[i] = static_cast<float>(std::tanh(bpOut * env * 2.1));
        }
        break;
    }

    // 5. Trap Tight Closed Hat (6 detuned square waves through 8 kHz highpass)
    case DrumHitType::hihatClosed:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.09); // 90 ms
        buffer.resize(numSamples, 0.0f);
        constexpr std::array<double, 6> freqs = {245.0, 306.0, 368.0, 412.0, 524.0, 810.0};
        std::array<double, 6> phases {};
        float hpState = 0.0f;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            double sqSum = 0.0;
            for (std::size_t k = 0; k < 6; ++k)
            {
                phases[k] += (freqs[k] * 2.0 * std::numbers::pi) / sr;
                sqSum += (std::sin(phases[k]) > 0.0) ? 0.35 : -0.35;
            }

            // Highpass filter above 7500 Hz
            hpState += 0.70f * (static_cast<float>(sqSum) + whiteNoise() * 0.4f - hpState);
            const float hpOut = (static_cast<float>(sqSum) - hpState);

            const double amp = std::exp(-t * 65.0);
            buffer[i] = static_cast<float>(hpOut * amp * 0.95);
        }
        break;
    }

    // 6. Trap Sizzle Open Hat
    case DrumHitType::hihatOpen:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.60); // 600 ms
        buffer.resize(numSamples, 0.0f);
        constexpr std::array<double, 6> freqs = {260.0, 320.0, 390.0, 440.0, 560.0, 840.0};
        std::array<double, 6> phases {};
        float hpState = 0.0f;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            double sqSum = 0.0;
            for (std::size_t k = 0; k < 6; ++k)
            {
                phases[k] += (freqs[k] * 2.0 * std::numbers::pi) / sr;
                sqSum += (std::sin(phases[k]) > 0.0) ? 0.3 : -0.3;
            }

            hpState += 0.65f * (static_cast<float>(sqSum) + whiteNoise() * 0.5f - hpState);
            const float hpOut = (static_cast<float>(sqSum) - hpState);

            const double amp = std::exp(-t * 6.5);
            buffer[i] = static_cast<float>(hpOut * amp * 0.85);
        }
        break;
    }

    // 7. Analog Wood Rimshot
    case DrumHitType::rimshot:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.12); // 120 ms
        buffer.resize(numSamples, 0.0f);
        double phase = 0.0;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            phase += (1450.0 * 2.0 * std::numbers::pi) / sr;
            const double ring = std::sin(phase) * std::exp(-t * 70.0);
            const double click = (t < 0.005) ? (whiteNoise() * std::exp(-t * 400.0)) : 0.0;
            buffer[i] = static_cast<float>((ring + click * 0.6) * 0.9);
        }
        break;
    }

    // 8. Classic 808 Cowbell
    case DrumHitType::cowbell808:
    {
        const std::size_t numSamples = static_cast<std::size_t>(sr * 0.35); // 350 ms
        buffer.resize(numSamples, 0.0f);
        double p1 = 0.0;
        double p2 = 0.0;
        float bp1 = 0.0f;

        for (std::size_t i = 0; i < numSamples; ++i)
        {
            const double t = static_cast<double>(i) / sr;
            p1 += (540.0 * 2.0 * std::numbers::pi) / sr;
            p2 += (800.0 * 2.0 * std::numbers::pi) / sr;

            const double sq1 = (std::sin(p1) > 0.0) ? 0.5 : -0.5;
            const double sq2 = (std::sin(p2) > 0.0) ? 0.5 : -0.5;
            const double mix = sq1 + sq2;

            // Bandpass filter around 800 Hz
            bp1 += 0.32f * (static_cast<float>(mix) - bp1);

            const double amp = std::exp(-t * 14.0);
            buffer[i] = static_cast<float>(bp1 * amp * 0.95);
        }
        break;
    }
    }

    return buffer;
}

} // namespace dawg::engine::dsp
