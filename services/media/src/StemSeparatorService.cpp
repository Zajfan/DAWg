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
    SeparatedStems stems;
    if (left == nullptr || numSamples == 0)
        return stems;

    const float* rData = (right != nullptr) ? right : left;

    stems.drumsL.resize(numSamples, 0.0f);
    stems.drumsR.resize(numSamples, 0.0f);
    stems.bassL.resize(numSamples, 0.0f);
    stems.bassR.resize(numSamples, 0.0f);
    stems.vocalsL.resize(numSamples, 0.0f);
    stems.vocalsR.resize(numSamples, 0.0f);
    stems.otherL.resize(numSamples, 0.0f);
    stems.otherR.resize(numSamples, 0.0f);

    const double sr = (sampleRate > 1000.0) ? sampleRate : 48000.0;

    // Filter coefficients:
    // Bass LP at 160 Hz
    const float bassAlpha = std::clamp(static_cast<float>(2.0 * 3.14159265 * 160.0 / sr), 0.001f, 0.2f);
    // Vocal band: HP 250 Hz, LP 3500 Hz
    const float vocHpAlpha = std::clamp(static_cast<float>(2.0 * 3.14159265 * 250.0 / sr), 0.001f, 0.3f);
    const float vocLpAlpha = std::clamp(static_cast<float>(2.0 * 3.14159265 * 3500.0 / sr), 0.01f, 0.8f);

    float lpBassL = 0.0f, lpBassR = 0.0f;
    float hpVocL = 0.0f, hpVocR = 0.0f;
    float lpVocL = 0.0f;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = rData[i];

        // 1. Bass Extraction (Low frequencies)
        lpBassL += bassAlpha * (inL - lpBassL);
        lpBassR += bassAlpha * (inR - lpBassR);
        stems.bassL[i] = lpBassL;
        stems.bassR[i] = lpBassR;

        // 2. Mid/Side & Vocal Extraction (Center component within 250Hz - 3.5kHz)
        const float mid = (inL + inR) * 0.5f;
        const float side = (inL - inR) * 0.5f;

        hpVocL += vocHpAlpha * (mid - hpVocL);
        const float midHp = mid - hpVocL;
        lpVocL += vocLpAlpha * (midHp - lpVocL);

        // Vocal stem (Center focused vocal formant)
        stems.vocalsL[i] = lpVocL * 0.9f;
        stems.vocalsR[i] = lpVocL * 0.9f;

        // 3. Drums Extraction (High transients & remaining low punch)
        const float remainingL = inL - stems.bassL[i] - stems.vocalsL[i];
        const float remainingR = inR - stems.bassR[i] - stems.vocalsR[i];

        // High frequency transient components
        hpVocR += 0.35f * (remainingL - hpVocR);
        const float drumHighL = remainingL - hpVocR;
        const float drumHighR = remainingR - hpVocR;

        stems.drumsL[i] = drumHighL * 0.85f;
        stems.drumsR[i] = drumHighR * 0.85f;

        // 4. Other (Stereo side content & melodic ambiance)
        stems.otherL[i] = side + remainingL * 0.35f;
        stems.otherR[i] = -side + remainingR * 0.35f;
    }

    return stems;
}

std::vector<ExtractedGrooveNote> GrooveExtractor::extractGroove(
    const float* audio,
    std::size_t numSamples,
    double sampleRate,
    double bpm)
{
    std::vector<ExtractedGrooveNote> notes;
    if (audio == nullptr || numSamples < 1000)
        return notes;

    const double validBpm = (bpm > 10.0) ? bpm : 120.0;
    const double samplesPerBeat = (sampleRate * 60.0) / validBpm;
    const auto minNoteDist = static_cast<std::size_t>(samplesPerBeat / 8.0); // 32nd note distance

    constexpr std::size_t windowSize = 256;
    std::vector<float> energy;
    energy.reserve(numSamples / windowSize);

    for (std::size_t i = 0; i + windowSize < numSamples; i += windowSize)
    {
        float rms = 0.0f;
        for (std::size_t j = 0; j < windowSize; ++j)
        {
            const float s = audio[i + j];
            rms += s * s;
        }
        energy.push_back(std::sqrt(rms / static_cast<float>(windowSize)));
    }

    std::size_t lastOnsetSample = 0;
    for (std::size_t i = 1; i + 1 < energy.size(); ++i)
    {
        const float diff = energy[i] - energy[i - 1];
        if (diff > 0.04f && energy[i] > energy[i + 1])
        {
            const std::size_t samplePos = i * windowSize;
            if (samplePos >= lastOnsetSample + minNoteDist)
            {
                lastOnsetSample = samplePos;

                // Simple spectral zero-crossing rate estimator for frequency classification
                std::size_t zeroCrossings = 0;
                for (std::size_t j = 1; j < windowSize && (samplePos + j) < numSamples; ++j)
                {
                    if ((audio[samplePos + j] >= 0.0f) != (audio[samplePos + j - 1] >= 0.0f))
                        ++zeroCrossings;
                }

                std::uint8_t pitch = 36; // Kick default
                if (zeroCrossings > 50)
                    pitch = 42; // Hi-Hat (high frequency sizzle)
                else if (zeroCrossings > 15)
                    pitch = 38; // Snare (mid frequency crack)

                const auto velocity = static_cast<std::uint8_t>(std::clamp(static_cast<int>(energy[i] * 220.0f), 40, 127));

                notes.push_back(ExtractedGrooveNote {
                    .pitch = pitch,
                    .velocity = velocity,
                    .startSample = samplePos,
                    .lengthSamples = static_cast<std::uint64_t>(samplesPerBeat * 0.25)
                });
            }
        }
    }

    return notes;
}

} // namespace dawg::media
