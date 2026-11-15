#include <dawg/engine/plugins/AraPluginBridge.h>

#include <cmath>
#include <numbers>

namespace dawg::engine::plugins {

AraPluginBridge::AraPluginBridge() noexcept
    : m_documentName("DAWg_ARA_Document")
{
}

AraPluginBridge::AraPluginBridge(std::string documentName) noexcept
    : m_documentName(std::move(documentName))
{
}

bool AraPluginBridge::addAudioSource(AraAudioSource source)
{
    if (source.id.empty() || source.sampleData.empty())
    {
        return false;
    }
    removeAudioSource(source.id);
    m_audioSources.push_back(std::move(source));
    return true;
}

bool AraPluginBridge::removeAudioSource(const std::string& sourceId)
{
    auto it = std::remove_if(m_audioSources.begin(), m_audioSources.end(), [&](const AraAudioSource& s) {
        return s.id == sourceId;
    });
    if (it != m_audioSources.end())
    {
        m_audioSources.erase(it, m_audioSources.end());
        return true;
    }
    return false;
}

const AraAudioSource* AraPluginBridge::getAudioSource(const std::string& sourceId) const noexcept
{
    for (const auto& s : m_audioSources)
    {
        if (s.id == sourceId)
        {
            return &s;
        }
    }
    return nullptr;
}

bool AraPluginBridge::addAudioModification(AraAudioModification modification)
{
    if (modification.id.empty() || modification.audioSourceId.empty())
    {
        return false;
    }
    m_modifications.push_back(std::move(modification));
    return true;
}

bool AraPluginBridge::updateModificationEdits(
    const std::string& modificationId,
    const float pitchShift,
    const float formantShift)
{
    for (auto& mod : m_modifications)
    {
        if (mod.id == modificationId)
        {
            mod.globalPitchShiftSemitones = pitchShift;
            mod.globalFormantShiftSemitones = formantShift;
            mod.isModified = (std::abs(pitchShift) > 0.001f || std::abs(formantShift) > 0.001f);
            return true;
        }
    }
    return false;
}

const AraAudioModification* AraPluginBridge::getAudioModification(const std::string& modificationId) const noexcept
{
    for (const auto& mod : m_modifications)
    {
        if (mod.id == modificationId)
        {
            return &mod;
        }
    }
    return nullptr;
}

bool AraPluginBridge::addPlaybackRegion(AraPlaybackRegion region)
{
    if (region.id.empty() || region.audioModificationId.empty())
    {
        return false;
    }
    m_playbackRegions.push_back(std::move(region));
    return true;
}

bool AraPluginBridge::removePlaybackRegion(const std::string& regionId)
{
    auto it = std::remove_if(m_playbackRegions.begin(), m_playbackRegions.end(), [&](const AraPlaybackRegion& r) {
        return r.id == regionId;
    });
    if (it != m_playbackRegions.end())
    {
        m_playbackRegions.erase(it, m_playbackRegions.end());
        return true;
    }
    return false;
}

const AraPlaybackRegion* AraPluginBridge::getPlaybackRegion(const std::string& regionId) const noexcept
{
    for (const auto& r : m_playbackRegions)
    {
        if (r.id == regionId)
        {
            return &r;
        }
    }
    return nullptr;
}

void AraPluginBridge::syncMusicalContext(const AraMusicalContext& context) noexcept
{
    m_musicalContext = context;
}

std::vector<AraDetectedNote> AraPluginBridge::analyzeAudioSource(const std::string& sourceId) const
{
    const auto* source = getAudioSource(sourceId);
    if (source == nullptr || source->sampleData.empty() || source->sampleData[0].empty())
    {
        return {};
    }

    const auto& channel = source->sampleData[0];
    const auto numSamples = channel.size();
    const double sampleRate = source->sampleRate > 0.0 ? source->sampleRate : 48000.0;

    std::vector<AraDetectedNote> notes;

    // Detect note segments via energy analysis and fundamental frequency tracking
    constexpr std::size_t hopSize = 512;
    constexpr std::size_t windowSize = 1024;

    bool inNote = false;
    std::size_t noteStartSample = 0;
    float accumulatedPitch = 0.0f;
    std::size_t pitchMeasurements = 0;

    for (std::size_t pos = 0; pos + windowSize <= numSamples; pos += hopSize)
    {
        float energy = 0.0f;
        for (std::size_t i = 0; i < windowSize; ++i)
        {
            const float s = channel[pos + i];
            energy += s * s;
        }
        energy = std::sqrt(energy / static_cast<float>(windowSize));

        if (energy > 0.02f)
        {
            if (!inNote)
            {
                inNote = true;
                noteStartSample = pos;
                accumulatedPitch = 0.0f;
                pitchMeasurements = 0;
            }

            // Estimate pitch via YIN normalized difference function
            const auto halfWin = windowSize / 2;
            std::vector<float> dPrime(halfWin, 1.0f);
            float runningSum = 0.0f;

            for (std::size_t tau = 1; tau < halfWin; ++tau)
            {
                float diffSum = 0.0f;
                for (std::size_t j = 0; j < halfWin; ++j)
                {
                    const float diff = channel[pos + j] - channel[pos + j + tau];
                    diffSum += diff * diff;
                }
                runningSum += diffSum;
                dPrime[tau] = (runningSum > 1e-9f) ? (diffSum * static_cast<float>(tau) / runningSum) : 1.0f;
            }

            constexpr float threshold = 0.15f;
            std::size_t bestTau = 0;
            const auto minTau = std::max<std::size_t>(2, static_cast<std::size_t>(sampleRate / 900.0));
            const auto maxTau = std::min(halfWin - 1, static_cast<std::size_t>(sampleRate / 70.0));

            for (std::size_t tau = minTau; tau <= maxTau; ++tau)
            {
                if (dPrime[tau] < threshold)
                {
                    while (tau + 1 <= maxTau && dPrime[tau + 1] < dPrime[tau])
                    {
                        ++tau;
                    }
                    bestTau = tau;
                    break;
                }
            }

            if (bestTau == 0)
            {
                float minVal = 999.0f;
                for (std::size_t tau = minTau; tau <= maxTau; ++tau)
                {
                    if (dPrime[tau] < minVal)
                    {
                        minVal = dPrime[tau];
                        bestTau = tau;
                    }
                }
            }

            if (bestTau >= minTau && bestTau <= maxTau)
            {
                const float f0 = static_cast<float>(sampleRate) / static_cast<float>(bestTau);
                const float midi = 69.0f + 12.0f * std::log2(f0 / 440.0f);
                accumulatedPitch += midi;
                ++pitchMeasurements;
            }
        }
        else
        {
            if (inNote)
            {
                inNote = false;
                const auto noteLen = pos - noteStartSample;
                if (noteLen >= 1024 && pitchMeasurements > 0)
                {
                    const float avgPitch = accumulatedPitch / static_cast<float>(pitchMeasurements);
                    const auto startSec = static_cast<double>(noteStartSample) / sampleRate;
                    const auto durSec = static_cast<double>(noteLen) / sampleRate;

                    notes.push_back(AraDetectedNote {
                        .startSeconds = startSec,
                        .durationSeconds = durSec,
                        .pitchMidi = std::round(avgPitch),
                        .pitchCentDeviation = (avgPitch - std::round(avgPitch)) * 100.0f,
                        .confidence = 0.92f
                    });
                }
            }
        }
    }

    return notes;
}

std::vector<std::vector<float>> AraPluginBridge::renderPlaybackRegion(
    const std::string& regionId,
    const double sampleRate) const
{
    const auto* region = getPlaybackRegion(regionId);
    if (region == nullptr || sampleRate <= 0.0)
    {
        return {};
    }

    const auto* mod = getAudioModification(region->audioModificationId);
    if (mod == nullptr)
    {
        return {};
    }

    const auto* source = getAudioSource(mod->audioSourceId);
    if (source == nullptr || source->sampleData.empty())
    {
        return {};
    }

    const auto numChannels = source->sampleData.size();
    const auto outFrames = static_cast<std::size_t>(std::round(region->durationInPlaybackSec * sampleRate));
    if (outFrames == 0)
    {
        return {};
    }

    std::vector<std::vector<float>> output(numChannels, std::vector<float>(outFrames, 0.0f));

    const double srcRate = source->sampleRate > 0.0 ? source->sampleRate : sampleRate;
    const double startSrcSample = region->startInModificationSec * srcRate;
    const double pitchFactor = std::pow(2.0, static_cast<double>(mod->globalPitchShiftSemitones) / 12.0);

    for (std::size_t f = 0; f < outFrames; ++f)
    {
        const double srcPos = startSrcSample + static_cast<double>(f) * (srcRate / sampleRate) * pitchFactor;
        const auto baseIdx = static_cast<std::int64_t>(std::floor(srcPos));
        const float frac = static_cast<float>(srcPos - static_cast<double>(baseIdx));

        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto& src = source->sampleData[ch];
            const auto srcSize = static_cast<std::int64_t>(src.size());

            if (baseIdx >= 0 && baseIdx + 1 < srcSize)
            {
                output[ch][f] = src[static_cast<std::size_t>(baseIdx)] * (1.0f - frac)
                              + src[static_cast<std::size_t>(baseIdx + 1)] * frac;
            }
            else if (baseIdx >= 0 && baseIdx < srcSize)
            {
                output[ch][f] = src[static_cast<std::size_t>(baseIdx)];
            }
        }
    }

    return output;
}

std::vector<std::vector<float>> AraPluginBridge::bakeEditsToBuffer(
    const std::string& modificationId,
    const double sampleRate) const
{
    const auto* mod = getAudioModification(modificationId);
    if (mod == nullptr)
    {
        return {};
    }

    const auto* source = getAudioSource(mod->audioSourceId);
    if (source == nullptr || source->sampleData.empty())
    {
        return {};
    }

    const auto numChannels = source->sampleData.size();
    const auto inFrames = source->sampleData[0].size();
    const double durationSec = static_cast<double>(inFrames) / source->sampleRate;
    const auto outFrames = static_cast<std::size_t>(std::round(durationSec * sampleRate));

    std::vector<std::vector<float>> output(numChannels, std::vector<float>(outFrames, 0.0f));
    const double pitchFactor = std::pow(2.0, static_cast<double>(mod->globalPitchShiftSemitones) / 12.0);

    for (std::size_t f = 0; f < outFrames; ++f)
    {
        const double srcPos = static_cast<double>(f) * (source->sampleRate / sampleRate) * pitchFactor;
        const auto baseIdx = static_cast<std::int64_t>(std::floor(srcPos));
        const float frac = static_cast<float>(srcPos - static_cast<double>(baseIdx));

        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            const auto& src = source->sampleData[ch];
            const auto srcSize = static_cast<std::int64_t>(src.size());

            if (baseIdx >= 0 && baseIdx + 1 < srcSize)
            {
                output[ch][f] = src[static_cast<std::size_t>(baseIdx)] * (1.0f - frac)
                              + src[static_cast<std::size_t>(baseIdx + 1)] * frac;
            }
            else if (baseIdx >= 0 && baseIdx < srcSize)
            {
                output[ch][f] = src[static_cast<std::size_t>(baseIdx)];
            }
        }
    }

    return output;
}

} // namespace dawg::engine::plugins
