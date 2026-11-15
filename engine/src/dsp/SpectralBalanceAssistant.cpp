#include <dawg/engine/dsp/SpectralBalanceAssistant.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace dawg::engine::dsp {

namespace {

constexpr std::array<float, SpectralBalanceAssistant::bandCount> kIsoCenterFreqs = {
    20.0f,    25.0f,    31.5f,   40.0f,    50.0f,    63.0f,    80.0f,    100.0f,
    125.0f,   160.0f,   200.0f,  250.0f,   315.0f,   400.0f,   500.0f,   630.0f,
    800.0f,   1000.0f,  1250.0f, 1600.0f,  2000.0f,  2500.0f,  3150.0f,  4000.0f,
    5000.0f,  6300.0f,  8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f, 22000.0f
};

} // namespace

float SpectralBalanceAssistant::bandCenterFreq(std::size_t index) noexcept
{
    if (index >= bandCount) {
        return 1000.0f;
    }
    return kIsoCenterFreqs[index];
}

SpectralBalanceAssistant::SpectralBalanceAssistant() noexcept
{
    initBands();
    applyGenreTargetCurves();
    reset();
}

void SpectralBalanceAssistant::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void SpectralBalanceAssistant::reset() noexcept
{
    for (std::size_t i = 0; i < bandCount; ++i) {
        m_bands[i].measuredDb = -60.0f;
        m_bands[i].inTolerance = true;
        m_smoothedEnergies[i] = 1e-6f;
    }
}

void SpectralBalanceAssistant::initBands() noexcept
{
    for (std::size_t i = 0; i < bandCount; ++i) {
        m_bands[i].centerFreqHz = kIsoCenterFreqs[i];
    }
}

std::string_view SpectralBalanceAssistant::genreName() const noexcept
{
    switch (m_genre) {
    case GenreProfile::pop: return "Pop";
    case GenreProfile::electronic: return "Electronic / EDM";
    case GenreProfile::hipHop: return "Hip-Hop / Trap";
    case GenreProfile::modernRock: return "Modern Rock / Metal";
    case GenreProfile::orchestral: return "Acoustic / Orchestral";
    }
    return "Pop";
}

void SpectralBalanceAssistant::setGenreProfile(GenreProfile profile) noexcept
{
    m_genre = profile;
    applyGenreTargetCurves();
}

void SpectralBalanceAssistant::applyGenreTargetCurves() noexcept
{
    for (std::size_t i = 0; i < bandCount; ++i) {
        const float freq = kIsoCenterFreqs[i];
        float target = -18.0f;
        float tol = 3.5f;

        switch (m_genre) {
        case GenreProfile::pop:
            // Balanced pink-noise slope (-3 dB/oct) with warm low-mid and crisp 10k air
            if (freq < 40.0f) {
                target = -24.0f; // low cut
            } else if (freq <= 160.0f) {
                target = -16.0f; // punchy low end
            } else if (freq <= 2500.0f) {
                target = -18.0f - 1.5f * std::log2(freq / 250.0f);
            } else {
                target = -22.0f - 2.5f * std::log2(freq / 2500.0f);
            }
            tol = 3.5f;
            break;

        case GenreProfile::electronic:
            // Heavy sub bass (+4dB at 40-70 Hz), scooped 300Hz, sizzling highs
            if (freq < 30.0f) {
                target = -20.0f;
            } else if (freq <= 90.0f) {
                target = -13.0f; // Sub punch
            } else if (freq >= 250.0f && freq <= 600.0f) {
                target = -22.0f; // Scooped mud
            } else if (freq >= 6000.0f) {
                target = -19.0f; // Extended top end
            } else {
                target = -18.0f - 2.0f * std::log2(freq / 100.0f);
            }
            tol = 4.0f;
            break;

        case GenreProfile::hipHop:
            // Heavy 808 sub (+6dB at 30-60 Hz)
            if (freq >= 30.0f && freq <= 70.0f) {
                target = -12.0f;
            } else if (freq < 30.0f) {
                target = -18.0f;
            } else {
                target = -17.0f - 3.5f * std::log2(freq / 70.0f);
            }
            tol = 4.0f;
            break;

        case GenreProfile::modernRock:
            // Punchy bass & snare, aggressive 2-4 kHz guitar bite
            if (freq >= 70.0f && freq <= 200.0f) {
                target = -15.0f;
            } else if (freq >= 2000.0f && freq <= 4500.0f) {
                target = -16.0f; // Guitar presence
            } else {
                target = -18.0f - 2.8f * std::log2(freq / 200.0f);
            }
            tol = 3.5f;
            break;

        case GenreProfile::orchestral:
            // Wide dynamic range, natural acoustic slope, broader tolerances
            target = -18.0f - 3.0f * std::log2(std::max(freq, 40.0f) / 100.0f);
            tol = 5.5f;
            break;
        }

        m_bands[i].targetDb = target;
        m_bands[i].minToleranceDb = target - tol;
        m_bands[i].maxToleranceDb = target + tol;
    }
}

void SpectralBalanceAssistant::analyzeAudioBlock(const AudioBufferView& buffer) noexcept
{
    const std::size_t numSamples = buffer.frameCount;
    const std::size_t numChannels = buffer.channelCount;

    if (buffer.channels == nullptr || numChannels == 0 || numSamples == 0) {
        return;
    }

    const float* ch0 = buffer.channels[0];
    const float* ch1 = numChannels > 1 ? buffer.channels[1] : nullptr;

    // Filterbank energy measurement across 32 bands
    const float alpha = 0.05f; // Ballistic smoothing factor

    for (std::size_t b = 0; b < bandCount; ++b) {
        const float fc = kIsoCenterFreqs[b];
        const float w0 = 2.0f * 3.14159265358979323846f * fc / static_cast<float>(m_sampleRate);
        const float q = 2.0f; // 1/3-octave band Q
        const float alphaBand = std::sin(w0) / (2.0f * q);

        const float b0 = alphaBand;
        const float b2 = -alphaBand;
        const float a0 = 1.0f + alphaBand;
        const float a1 = -2.0f * std::cos(w0);
        const float a2 = 1.0f - alphaBand;

        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float bandEnergy = 0.0f;

        for (std::size_t s = 0; s < numSamples; ++s) {
            float inSample = ch0[s];
            if (ch1 != nullptr) {
                inSample = 0.5f * (inSample + ch1[s]);
            }

            const float outSample = (b0 * inSample + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1;
            x1 = inSample;
            y2 = y1;
            y1 = outSample;

            bandEnergy += outSample * outSample;
        }

        bandEnergy /= static_cast<float>(numSamples);
        m_smoothedEnergies[b] = (1.0f - alpha) * m_smoothedEnergies[b] + alpha * bandEnergy;

        const float measuredDb = 10.0f * std::log10(std::max(m_smoothedEnergies[b], 1e-7f));
        m_bands[b].measuredDb = measuredDb;
        m_bands[b].inTolerance = (measuredDb >= m_bands[b].minToleranceDb &&
                                  measuredDb <= m_bands[b].maxToleranceDb);
    }
}

float SpectralBalanceAssistant::mixBalanceScore() const noexcept
{
    std::size_t insideCount = 0;
    for (std::size_t i = 0; i < bandCount; ++i) {
        if (m_bands[i].inTolerance) {
            ++insideCount;
        }
    }
    return (static_cast<float>(insideCount) / static_cast<float>(bandCount)) * 100.0f;
}

std::vector<SpectralCollision> SpectralBalanceAssistant::detectCollisions(
    const std::array<float, bandCount>& spectrumTrackA,
    const std::array<float, bandCount>& spectrumTrackB,
    float thresholdDb) const
{
    std::vector<SpectralCollision> collisions;

    for (std::size_t i = 0; i < bandCount; ++i) {
        const float dbA = spectrumTrackA[i];
        const float dbB = spectrumTrackB[i];

        if (dbA > thresholdDb && dbB > thresholdDb) {
            const float diff = std::abs(dbA - dbB);
            if (diff < 8.0f) {
                // Severe collision if difference is small and energies are substantial
                const float closeness = 1.0f - (diff / 8.0f);
                const float energyFactor = std::min(1.0f, (std::min(dbA, dbB) - thresholdDb) / 20.0f);
                const float severity = std::clamp(closeness * energyFactor, 0.0f, 1.0f);

                if (severity >= 0.15f) {
                    collisions.push_back(SpectralCollision {
                        .bandIndex = i,
                        .centerFreqHz = kIsoCenterFreqs[i],
                        .trackADb = dbA,
                        .trackBDb = dbB,
                        .collisionSeverity = severity
                    });
                }
            }
        }
    }

    return collisions;
}

std::vector<UnmaskingRecommendation> SpectralBalanceAssistant::calculateSmartUnmasking(
    const std::vector<SpectralCollision>& collisions) const
{
    std::vector<UnmaskingRecommendation> recommendations;

    for (const auto& col : collisions) {
        if (col.collisionSeverity < 0.25f) {
            continue;
        }

        // Recommend dynamic surgical notch on the masking element
        const float cutDb = -std::clamp(col.collisionSeverity * 6.0f, 1.5f, 7.0f);
        const float q = col.centerFreqHz < 200.0f ? 3.5f : 2.5f;

        std::ostringstream desc;
        desc << "Dynamic unmasking notch at "
             << std::fixed << std::setprecision(0) << col.centerFreqHz
             << " Hz (" << std::setprecision(1) << cutDb << " dB, Q=" << q
             << ") to resolve " << static_cast<int>(col.collisionSeverity * 100.0f)
             << "% masking conflict.";

        recommendations.push_back(UnmaskingRecommendation {
            .centerFreqHz = col.centerFreqHz,
            .qFactor = q,
            .recommendedGainCutDb = cutDb,
            .description = desc.str()
        });
    }

    return recommendations;
}

} // namespace dawg::engine::dsp
