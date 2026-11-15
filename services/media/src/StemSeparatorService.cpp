#include <dawg/media/StemSeparatorService.h>

#include <algorithm>
#include <cmath>

namespace dawg::media {

SeparatedStems StemSeparatorService::separate4Stems(
    const float* left,
    const float* right,
    std::size_t numSamples,
    double sampleRate)
{
    return separate4StemsConfigurable(left, right, numSamples, sampleRate, StemSeparationConfig {});
}

SeparatedStems StemSeparatorService::separate4StemsConfigurable(
    const float* left,
    const float* right,
    std::size_t numSamples,
    double sampleRate,
    const StemSeparationConfig& config)
{
    SeparatedStems stems;
    if (left == nullptr || numSamples == 0) {
        return stems;
    }

    const float* rData = (right != nullptr) ? right : left;

    stems.drumsL.resize(numSamples, 0.0f);
    stems.drumsR.resize(numSamples, 0.0f);
    stems.bassL.resize(numSamples, 0.0f);
    stems.bassR.resize(numSamples, 0.0f);
    stems.vocalsL.resize(numSamples, 0.0f);
    stems.vocalsR.resize(numSamples, 0.0f);
    stems.otherL.resize(numSamples, 0.0f);
    stems.otherR.resize(numSamples, 0.0f);

    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const float dt = 1.0f / static_cast<float>(sr);

    // Two-pole lowpass filter coefficients for Bass crossover (~180 Hz)
    const float wBass = 2.0f * 3.14159265f * config.bassCutoffHz * dt;
    const float alphaBass = std::clamp(wBass, 0.001f, 0.35f);

    // Fast transient envelope (~1.0 ms) vs slow sustained envelope (~25 ms) for HPSS
    const float alphaFast = 1.0f - std::exp(-dt / 0.001f);
    const float alphaSlow = 1.0f - std::exp(-dt / 0.025f);

    float envFastL = 0.0f, envSlowL = 0.0f;
    float envFastR = 0.0f, envSlowR = 0.0f;
    float lpBassL = 0.0f, lpBassR = 0.0f;

    for (std::size_t i = 0; i < numSamples; ++i) {
        const float inL = left[i];
        const float inR = rData[i];

        // 1. Transient / Percussive Envelope Tracking (HPSS)
        const float absL = std::abs(inL);
        const float absR = std::abs(inR);

        envFastL += alphaFast * (absL - envFastL);
        envSlowL += alphaSlow * (absL - envSlowL);

        envFastR += alphaFast * (absR - envFastR);
        envSlowR += alphaSlow * (absR - envSlowR);

        const float transL = std::max(0.0f, envFastL - envSlowL);
        const float transR = std::max(0.0f, envFastR - envSlowR);
        const float drumWeightL = std::clamp((transL / (envFastL + 1e-5f)) * config.percussiveSensitivity, 0.0f, 1.0f);
        const float drumWeightR = std::clamp((transR / (envFastR + 1e-5f)) * config.percussiveSensitivity, 0.0f, 1.0f);
        const float drumWeight = 0.5f * (drumWeightL + drumWeightR);

        // 2. Harmonic / Sustained Component
        const float harmonicWeight = 1.0f - drumWeight;

        // 3. Stereo Azimuth Spatial Decomposition (Phantom Center vs Panned Sides)
        const float mid = 0.5f * (inL + inR);
        const float side = 0.5f * (inL - inR);
        const float centerRatio = std::abs(mid) / (std::abs(mid) + std::abs(side) + 1e-5f);
        const float centerWeight = std::clamp(centerRatio / std::max(0.1f, config.centerAzimuthWidth), 0.0f, 1.0f);
        const float sideWeight = 1.0f - centerWeight;

        // 4. Low-End Spectral Crossover for Bass vs Vocals
        lpBassL += alphaBass * (inL - lpBassL);
        lpBassR += alphaBass * (inR - lpBassR);
        const float bassEnergy = 0.5f * (std::abs(lpBassL) + std::abs(lpBassR));
        const float totalEnergy = 0.5f * (absL + absR) + 1e-5f;
        const float bassFreqRatio = std::clamp(bassEnergy / totalEnergy, 0.0f, 1.0f);

        // Sub-stem unnormalized weights
        const float wDrums = drumWeight;
        const float wBass = harmonicWeight * centerWeight * bassFreqRatio;
        const float wVocals = harmonicWeight * centerWeight * (1.0f - bassFreqRatio);
        const float wOther = harmonicWeight * sideWeight;

        // 5. Zero-Phase Soft Wiener Mask Reconstruction (Guaranteed perfect sum)
        const float wTotal = wDrums + wBass + wVocals + wOther + 1e-6f;
        const float maskDrums = wDrums / wTotal;
        const float maskBass = wBass / wTotal;
        const float maskVocals = wVocals / wTotal;
        const float maskOther = wOther / wTotal;

        stems.drumsL[i] = inL * maskDrums;
        stems.drumsR[i] = inR * maskDrums;

        stems.bassL[i] = inL * maskBass;
        stems.bassR[i] = inR * maskBass;

        stems.vocalsL[i] = inL * maskVocals;
        stems.vocalsR[i] = inR * maskVocals;

        stems.otherL[i] = inL * maskOther;
        stems.otherR[i] = inR * maskOther;
    }

    return stems;
}

std::pair<std::vector<float>, std::vector<float>> StemSeparatorService::extractVocals(
    const float* left,
    const float* right,
    std::size_t numSamples,
    double sampleRate,
    float centerFocus)
{
    StemSeparationConfig cfg;
    cfg.centerAzimuthWidth = std::clamp(1.0f - centerFocus * 0.7f, 0.15f, 0.8f);
    auto stems = separate4StemsConfigurable(left, right, numSamples, sampleRate, cfg);
    return {std::move(stems.vocalsL), std::move(stems.vocalsR)};
}

std::pair<std::vector<float>, std::vector<float>> StemSeparatorService::removeVocals(
    const float* left,
    const float* right,
    std::size_t numSamples,
    double sampleRate,
    float centerFocus)
{
    const float* rData = (right != nullptr) ? right : left;
    auto [vocL, vocR] = extractVocals(left, right, numSamples, sampleRate, centerFocus);

    std::vector<float> instL(numSamples, 0.0f);
    std::vector<float> instR(numSamples, 0.0f);

    for (std::size_t i = 0; i < numSamples; ++i) {
        instL[i] = left[i] - vocL[i];
        instR[i] = rData[i] - vocR[i];
    }

    return {std::move(instL), std::move(instR)};
}

std::vector<ExtractedGrooveNote> GrooveExtractor::extractGroove(
    const float* audio,
    std::size_t numSamples,
    double sampleRate,
    double bpm)
{
    std::vector<ExtractedGrooveNote> groove;
    if (audio == nullptr || numSamples < 128 || sampleRate <= 0.0 || bpm <= 0.0) {
        return groove;
    }

    const double samplesPer16th = (15.0 / bpm) * sampleRate;
    const auto totalSteps = static_cast<std::size_t>(static_cast<double>(numSamples) / samplesPer16th);

    float prevEnergy = 0.0f;
    for (std::size_t s = 0; s < totalSteps; ++s) {
        const auto start = static_cast<std::size_t>(static_cast<double>(s) * samplesPer16th);
        const auto end = std::min(start + static_cast<std::size_t>(samplesPer16th), numSamples);

        float blockEnergy = 0.0f;
        for (std::size_t i = start; i < end; ++i) {
            blockEnergy += audio[i] * audio[i];
        }
        blockEnergy = std::sqrt(blockEnergy / static_cast<float>(end - start));

        // Detect onset
        if (blockEnergy > prevEnergy * 1.6f && blockEnergy > 0.05f) {
            const auto vel = static_cast<std::uint8_t>(std::clamp(blockEnergy * 127.0f, 40.0f, 127.0f));
            groove.push_back(ExtractedGrooveNote {
                .pitch = 36, // Kick
                .velocity = vel,
                .startSample = start,
                .lengthSamples = static_cast<std::uint64_t>(samplesPer16th * 0.75)
            });
        }
        prevEnergy = blockEnergy;
    }

    return groove;
}

} // namespace dawg::media
