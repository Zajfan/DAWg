#include <dawg/project/TakeLaneComping.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace dawg::project {

namespace {

constexpr std::array<const char*, 5> kDefaultTakeColors = {
    "#4A90E2", // Electric Blue
    "#50E3C2", // Mint Cyan
    "#F5A623", // Sunset Amber
    "#E91E63", // Hot Pink
    "#9013FE"  // Violet
};

constexpr std::array<const char*, 12> kNoteNames = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

} // namespace

TakeCompManager::TakeCompManager(TrackId trackId) noexcept
    : m_trackId(trackId)
{
}

std::uint32_t TakeCompManager::addTake(AudioClip clip, const std::string& name, const std::string& colorHex)
{
    const std::uint32_t takeId = m_nextTakeId++;
    const std::string takeName = name.empty() ? ("Take " + std::to_string(takeId)) : name;
    const std::string color = colorHex.empty() ? kDefaultTakeColors[(takeId - 1) % kDefaultTakeColors.size()] : colorHex;

    TakeLane lane {
        .takeId = takeId,
        .trackId = m_trackId,
        .name = takeName,
        .colorHex = color,
        .clip = clip,
        .isAuditionSolo = false
    };

    m_takes.push_back(std::move(lane));

    // If first take and no slices yet, default entire clip as base comp
    if (m_takes.size() == 1 && m_slices.empty() && clip.lengthSamples > 0) {
        m_slices.push_back(CompSlice {
            .sliceId = m_nextSliceId++,
            .takeId = takeId,
            .startSample = clip.startSample,
            .endSample = clip.startSample + clip.lengthSamples,
            .crossfadeSamples = 192
        });
    }

    return takeId;
}

bool TakeCompManager::removeTake(std::uint32_t takeId)
{
    const auto it = std::remove_if(m_takes.begin(), m_takes.end(),
        [takeId](const TakeLane& t) { return t.takeId == takeId; });
    if (it == m_takes.end()) {
        return false;
    }
    m_takes.erase(it, m_takes.end());

    // Remove any comp slices referencing this take
    const auto sIt = std::remove_if(m_slices.begin(), m_slices.end(),
        [takeId](const CompSlice& s) { return s.takeId == takeId; });
    m_slices.erase(sIt, m_slices.end());

    if (m_soloedTakeId == takeId) {
        m_soloedTakeId = 0;
    }
    return true;
}

const TakeLane* TakeCompManager::findTake(std::uint32_t takeId) const noexcept
{
    for (const auto& t : m_takes) {
        if (t.takeId == takeId) {
            return &t;
        }
    }
    return nullptr;
}

void TakeCompManager::clearCompSlices() noexcept
{
    m_slices.clear();
}

std::uint64_t TakeCompManager::findNearestZeroCrossing(
    const float* buffer,
    const std::size_t bufferLength,
    const std::uint64_t targetSample,
    const std::size_t window) noexcept
{
    if (buffer == nullptr || bufferLength == 0 || targetSample >= bufferLength) {
        return targetSample;
    }

    const std::size_t start = (targetSample > window) ? (targetSample - window) : 0;
    const std::size_t end = std::min(bufferLength - 1, static_cast<std::size_t>(targetSample + window));

    std::size_t bestIdx = targetSample;
    float minAbs = std::abs(buffer[targetSample]);

    for (std::size_t i = start; i < end; ++i) {
        // True zero crossing check
        if ((buffer[i] <= 0.0f && buffer[i + 1] > 0.0f) || (buffer[i] >= 0.0f && buffer[i + 1] < 0.0f)) {
            return (std::abs(buffer[i]) < std::abs(buffer[i + 1])) ? i : (i + 1);
        }
        if (std::abs(buffer[i]) < minAbs) {
            minAbs = std::abs(buffer[i]);
            bestIdx = i;
        }
    }

    return bestIdx;
}

void TakeCompManager::selectCompSliceWithZeroCrossing(
    const std::uint32_t takeId,
    const std::uint64_t startSample,
    const std::uint64_t endSample,
    const float* audioBuffer,
    const std::size_t bufferLength,
    const std::uint32_t crossfadeSamples)
{
    const std::uint64_t snappedStart = findNearestZeroCrossing(audioBuffer, bufferLength, startSample);
    const std::uint64_t snappedEnd = findNearestZeroCrossing(audioBuffer, bufferLength, endSample);

    selectCompSlice(takeId, snappedStart, snappedEnd, crossfadeSamples);
}

void TakeCompManager::selectCompSlice(
    std::uint32_t takeId,
    std::uint64_t startSample,
    std::uint64_t endSample,
    std::uint32_t crossfadeSamples)
{
    if (endSample <= startSample) {
        return;
    }

    std::vector<CompSlice> updatedSlices;
    updatedSlices.reserve(m_slices.size() + 2);

    for (const auto& s : m_slices) {
        if (s.endSample <= startSample || s.startSample >= endSample) {
            // No overlap: keep as is
            updatedSlices.push_back(s);
        } else if (s.startSample < startSample && s.endSample > endSample) {
            // New slice splits existing slice in two
            CompSlice leftPart = s;
            leftPart.endSample = startSample;
            updatedSlices.push_back(leftPart);

            CompSlice rightPart = s;
            rightPart.sliceId = m_nextSliceId++;
            rightPart.startSample = endSample;
            updatedSlices.push_back(rightPart);
        } else if (s.startSample < startSample) {
            // Overlaps on right side: trim right
            CompSlice trimmed = s;
            trimmed.endSample = startSample;
            updatedSlices.push_back(trimmed);
        } else if (s.endSample > endSample) {
            // Overlaps on left side: trim left
            CompSlice trimmed = s;
            trimmed.startSample = endSample;
            updatedSlices.push_back(trimmed);
        }
        // Completely covered slices are dropped
    }

    // Insert new selected comp slice
    updatedSlices.push_back(CompSlice {
        .sliceId = m_nextSliceId++,
        .takeId = takeId,
        .startSample = startSample,
        .endSample = endSample,
        .crossfadeSamples = crossfadeSamples
    });

    // Sort slices chronologically
    std::sort(updatedSlices.begin(), updatedSlices.end(),
        [](const CompSlice& a, const CompSlice& b) {
            return a.startSample < b.startSample;
        });

    m_slices = std::move(updatedSlices);
}

void TakeCompManager::setTakeAuditionSolo(std::uint32_t takeId, bool solo) noexcept
{
    if (solo) {
        m_soloedTakeId = takeId;
    } else if (m_soloedTakeId == takeId) {
        m_soloedTakeId = 0;
    }

    for (auto& t : m_takes) {
        t.isAuditionSolo = (t.takeId == m_soloedTakeId);
    }
}

AudioClip TakeCompManager::flattenAndMergeComp(
    ClipId newClipId,
    MediaAssetId consolidatedAssetId,
    [[maybe_unused]] const std::string& compName) const
{
    AudioClip mergedClip;
    mergedClip.id = newClipId;
    mergedClip.assetId = consolidatedAssetId;
    mergedClip.trackId = m_trackId;
    mergedClip.gainLinear = 1.0f;
    mergedClip.warpingEnabled = false;

    if (m_slices.empty()) {
        mergedClip.startSample = 0;
        mergedClip.lengthSamples = 0;
        return mergedClip;
    }

    std::uint64_t minStart = m_slices.front().startSample;
    std::uint64_t maxEnd = m_slices.front().endSample;

    for (const auto& s : m_slices) {
        minStart = std::min(minStart, s.startSample);
        maxEnd = std::max(maxEnd, s.endSample);
    }

    mergedClip.startSample = minStart;
    mergedClip.lengthSamples = maxEnd > minStart ? (maxEnd - minStart) : 0;
    return mergedClip;
}

void TakeCompManager::applyEqualPowerCrossfade(
    float* outOutgoing,
    float* outIncoming,
    std::size_t fadeLength) noexcept
{
    if (outOutgoing == nullptr || outIncoming == nullptr || fadeLength == 0) {
        return;
    }

    for (std::size_t i = 0; i < fadeLength; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(fadeLength);
        const double theta = frac * (std::numbers::pi_v<double> * 0.5);
        const float wOut = static_cast<float>(std::cos(theta));
        const float wIn = static_cast<float>(std::sin(theta));

        outOutgoing[i] *= wOut;
        outIncoming[i] *= wIn;
    }
}

// ============================================================================
// PitchBlobEditor Implementation
// ============================================================================

std::string PitchBlobEditor::midiPitchToNoteName(const float midiPitch)
{
    const int roundedMidi = std::clamp(static_cast<int>(std::round(midiPitch)), 0, 127);
    const int noteIdx = (roundedMidi % 12);
    const int octave = (roundedMidi / 12) - 1;
    return std::string(kNoteNames[static_cast<std::size_t>(noteIdx)]) + std::to_string(octave);
}

const PitchBlob* PitchBlobEditor::findBlob(const std::uint32_t blobId) const noexcept
{
    for (const auto& blob : m_blobs) {
        if (blob.blobId == blobId) {
            return &blob;
        }
    }
    return nullptr;
}

bool PitchBlobEditor::setBlobTuning(const std::uint32_t blobId, const float targetMidi, const float fineTuneCents) noexcept
{
    for (auto& blob : m_blobs) {
        if (blob.blobId == blobId) {
            blob.targetMidiPitch = targetMidi;
            blob.fineTuneCents = std::clamp(fineTuneCents, -100.0f, 100.0f);
            blob.noteName = midiPitchToNoteName(targetMidi);
            return true;
        }
    }
    return false;
}

bool PitchBlobEditor::setBlobVibrato(const std::uint32_t blobId, const float vibratoScale) noexcept
{
    for (auto& blob : m_blobs) {
        if (blob.blobId == blobId) {
            blob.vibratoDepthScale = std::clamp(vibratoScale, 0.0f, 2.0f);
            return true;
        }
    }
    return false;
}

bool PitchBlobEditor::setBlobFormant(const std::uint32_t blobId, const float formantShiftSemitones) noexcept
{
    for (auto& blob : m_blobs) {
        if (blob.blobId == blobId) {
            blob.formantShiftSemitones = std::clamp(formantShiftSemitones, -12.0f, 12.0f);
            return true;
        }
    }
    return false;
}

void PitchBlobEditor::quantizeToScale(
    const std::vector<int>& allowedMidiClasses,
    const float snapAmount)
{
    if (allowedMidiClasses.empty()) {
        return;
    }

    const float amount = std::clamp(snapAmount, 0.0f, 1.0f);

    for (auto& blob : m_blobs) {
        const float originalMidi = blob.detectedMidiPitch;
        int bestTargetNote = static_cast<int>(std::round(originalMidi));
        float minDistance = 999.0f;

        for (int offset = -6; offset <= 6; ++offset) {
            const int candidate = bestTargetNote + offset;
            const int noteClass = (candidate % 12 + 12) % 12;

            const bool isAllowed = std::find(allowedMidiClasses.begin(), allowedMidiClasses.end(), noteClass) != allowedMidiClasses.end();
            if (isAllowed) {
                const float dist = std::abs(originalMidi - static_cast<float>(candidate));
                if (dist < minDistance) {
                    minDistance = dist;
                    bestTargetNote = candidate;
                }
            }
        }

        const float snappedPitch = (1.0f - amount) * originalMidi + amount * static_cast<float>(bestTargetNote);
        blob.targetMidiPitch = snappedPitch;
        blob.noteName = midiPitchToNoteName(snappedPitch);
    }
}

void PitchBlobEditor::analyzeAudio(
    const float* audioSamples,
    const std::size_t sampleCount,
    const double sampleRate)
{
    clear();
    if (audioSamples == nullptr || sampleCount < 512 || sampleRate <= 0.0) {
        return;
    }

    constexpr std::size_t hopSize = 256;
    constexpr std::size_t windowSize = 512;
    const std::size_t numHops = (sampleCount - windowSize) / hopSize;

    bool inVoicedSegment = false;
    std::size_t segmentStartHop = 0;
    std::vector<float> segmentPitches;

    for (std::size_t hop = 0; hop < numHops; ++hop) {
        const std::size_t offset = hop * hopSize;

        // 1. RMS Energy
        float rms = 0.0f;
        for (std::size_t i = 0; i < windowSize; ++i) {
            rms += audioSamples[offset + i] * audioSamples[offset + i];
        }
        rms = std::sqrt(rms / static_cast<float>(windowSize));

        // 2. Autocorrelation Pitch Extraction
        float bestCorr = 0.0f;
        std::size_t bestLag = 0;
        const std::size_t minLag = static_cast<std::size_t>(sampleRate / 800.0); // 800 Hz
        const std::size_t maxLag = static_cast<std::size_t>(sampleRate / 70.0);  // 70 Hz

        for (std::size_t lag = minLag; lag < std::min(maxLag, windowSize / 2); ++lag) {
            float corr = 0.0f;
            for (std::size_t i = 0; i < windowSize / 2; ++i) {
                corr += audioSamples[offset + i] * audioSamples[offset + i + lag];
            }
            if (corr > bestCorr) {
                bestCorr = corr;
                bestLag = lag;
            }
        }

        const bool isVoiced = (rms > 0.02f && bestLag > 0 && bestCorr > 0.05f);

        if (isVoiced) {
            const float f0 = static_cast<float>(sampleRate) / static_cast<float>(bestLag);
            const float midi = 69.0f + 12.0f * std::log2(std::max(20.0f, f0) / 440.0f);

            if (!inVoicedSegment) {
                inVoicedSegment = true;
                segmentStartHop = hop;
                segmentPitches.clear();
            }
            segmentPitches.push_back(midi);
        } else {
            if (inVoicedSegment) {
                // End of syllable segment
                if (segmentPitches.size() >= 3) {
                    float sumPitch = 0.0f;
                    for (float p : segmentPitches) { sumPitch += p; }
                    const float avgMidi = sumPitch / static_cast<float>(segmentPitches.size());

                    PitchBlob blob {
                        .blobId = m_nextBlobId++,
                        .startSample = segmentStartHop * hopSize,
                        .endSample = hop * hopSize + windowSize,
                        .detectedMidiPitch = avgMidi,
                        .targetMidiPitch = std::round(avgMidi),
                        .fineTuneCents = 0.0f,
                        .vibratoDepthScale = 1.0f,
                        .formantShiftSemitones = 0.0f,
                        .pitchDriftSlope = (segmentPitches.back() - segmentPitches.front()),
                        .noteName = midiPitchToNoteName(std::round(avgMidi)),
                        .isVoiced = true
                    };
                    m_blobs.push_back(blob);
                }
                inVoicedSegment = false;
                segmentPitches.clear();
            }
        }
    }

    if (inVoicedSegment && segmentPitches.size() >= 3) {
        float sumPitch = 0.0f;
        for (float p : segmentPitches) { sumPitch += p; }
        const float avgMidi = sumPitch / static_cast<float>(segmentPitches.size());

        PitchBlob blob {
            .blobId = m_nextBlobId++,
            .startSample = segmentStartHop * hopSize,
            .endSample = sampleCount,
            .detectedMidiPitch = avgMidi,
            .targetMidiPitch = std::round(avgMidi),
            .fineTuneCents = 0.0f,
            .vibratoDepthScale = 1.0f,
            .formantShiftSemitones = 0.0f,
            .pitchDriftSlope = (segmentPitches.back() - segmentPitches.front()),
            .noteName = midiPitchToNoteName(std::round(avgMidi)),
            .isVoiced = true
        };
        m_blobs.push_back(blob);
    }
}

void PitchBlobEditor::renderTunedAudio(
    const float* inputSamples,
    float* outputSamples,
    const std::size_t sampleCount,
    [[maybe_unused]] const double sampleRate) const noexcept
{
    if (inputSamples == nullptr || outputSamples == nullptr || sampleCount == 0) {
        return;
    }

    // Copy input by default
    std::copy_n(inputSamples, sampleCount, outputSamples);

    // Apply per-blob pitch shift & formant scaling
    for (const auto& blob : m_blobs) {
        const float shiftSemitones = (blob.targetMidiPitch + blob.fineTuneCents * 0.01f) - blob.detectedMidiPitch;
        if (std::abs(shiftSemitones) < 0.01f && std::abs(blob.formantShiftSemitones) < 0.01f) {
            continue;
        }

        const float ratio = std::pow(2.0f, shiftSemitones / 12.0f);
        const std::size_t start = static_cast<std::size_t>(std::min(blob.startSample, static_cast<std::uint64_t>(sampleCount)));
        const std::size_t end = static_cast<std::size_t>(std::min(blob.endSample, static_cast<std::uint64_t>(sampleCount)));

        for (std::size_t i = start; i < end; ++i) {
            const double readPos = static_cast<double>(start) + static_cast<double>(i - start) * ratio;
            const auto idx = static_cast<std::size_t>(readPos);
            if (idx + 1 < sampleCount) {
                const float frac = static_cast<float>(readPos - static_cast<double>(idx));
                outputSamples[i] = inputSamples[idx] * (1.0f - frac) + inputSamples[idx + 1] * frac;
            }
        }
    }
}

// ============================================================================
// GrooveQuantizer Implementation
// ============================================================================

GrooveTemplate GrooveQuantizer::mpc60Swing(const float swing)
{
    GrooveTemplate t;
    t.name = "MPC 60 16-Swing " + std::to_string(static_cast<int>(swing * 100.0f)) + "%";
    t.swingAmount = std::clamp(swing, 0.50f, 0.75f);
    t.humanizeJitter = 0.005f;

    // 16 subdivisions per bar
    t.stepTimingOffsets.resize(16, 0.0f);
    t.stepVelocityAccents.resize(16, 1.0f);

    const float swingOffset = (t.swingAmount - 0.50f) * 0.5f; // shift odd 16th notes
    for (std::size_t i = 0; i < 16; ++i) {
        if (i % 2 == 1) {
            t.stepTimingOffsets[i] = swingOffset;
            t.stepVelocityAccents[i] = 0.88f; // slightly lighter offbeats
        } else {
            t.stepTimingOffsets[i] = 0.0f;
            t.stepVelocityAccents[i] = (i % 4 == 0) ? 1.15f : 1.0f; // accent on quarter notes
        }
    }
    return t;
}

GrooveTemplate GrooveQuantizer::dillaShuffle()
{
    GrooveTemplate t;
    t.name = "Dilla Quintuplet Shuffle";
    t.swingAmount = 0.62f;
    t.humanizeJitter = 0.015f;

    t.stepTimingOffsets = {
        0.0f, 0.065f, 0.01f, 0.08f,
        -0.02f, 0.07f, 0.0f, 0.06f,
        0.01f, 0.075f, -0.01f, 0.08f,
        0.0f, 0.06f, 0.02f, 0.07f
    };
    t.stepVelocityAccents = {
        1.2f, 0.75f, 0.95f, 0.7f,
        1.1f, 0.80f, 0.90f, 0.75f,
        1.2f, 0.75f, 0.95f, 0.7f,
        1.15f, 0.80f, 0.92f, 0.78f
    };
    return t;
}

GrooveTemplate GrooveQuantizer::funkHumanize()
{
    GrooveTemplate t;
    t.name = "Funk Drummer Pocket";
    t.swingAmount = 0.54f;
    t.humanizeJitter = 0.02f;

    t.stepTimingOffsets = {
        0.0f, 0.025f, -0.01f, 0.035f,
        -0.015f, 0.03f, 0.0f, 0.025f,
        0.0f, 0.03f, -0.005f, 0.04f,
        -0.02f, 0.025f, 0.01f, 0.03f
    };
    t.stepVelocityAccents = {
        1.25f, 0.85f, 1.0f, 0.8f,
        1.30f, 0.82f, 0.95f, 0.85f,
        1.22f, 0.86f, 1.0f, 0.82f,
        1.28f, 0.84f, 0.96f, 0.88f
    };
    return t;
}

GrooveTemplate GrooveQuantizer::extractGrooveFromAudio(
    const float* audioSamples,
    const std::size_t sampleCount,
    const double sampleRate,
    const double bpm)
{
    GrooveTemplate groove;
    groove.name = "Extracted Live Audio Groove";
    groove.stepTimingOffsets.assign(16, 0.0f);
    groove.stepVelocityAccents.assign(16, 1.0f);

    if (audioSamples == nullptr || sampleCount == 0 || sampleRate <= 0.0 || bpm <= 0.0) {
        return groove;
    }

    const double secondsPerBeat = 60.0 / bpm;
    const double samplesPer16th = (secondsPerBeat * 0.25) * sampleRate;

    // Detect high energy transient onsets
    for (std::size_t step = 0; step < 16; ++step) {
        const auto nominalCenter = static_cast<std::size_t>(static_cast<double>(step) * samplesPer16th);
        if (nominalCenter >= sampleCount) { break; }

        const std::size_t searchRadius = static_cast<std::size_t>(samplesPer16th * 0.4);
        const std::size_t start = (nominalCenter > searchRadius) ? (nominalCenter - searchRadius) : 0;
        const std::size_t end = std::min(sampleCount, nominalCenter + searchRadius);

        float maxAmp = 0.0f;
        std::size_t peakIdx = nominalCenter;

        for (std::size_t i = start; i < end; ++i) {
            const float a = std::abs(audioSamples[i]);
            if (a > maxAmp) {
                maxAmp = a;
                peakIdx = i;
            }
        }

        if (maxAmp > 0.05f) {
            const double diffSamples = static_cast<double>(peakIdx) - static_cast<double>(nominalCenter);
            const double offsetBeats = diffSamples / (sampleRate * secondsPerBeat);
            groove.stepTimingOffsets[step] = static_cast<float>(std::clamp(offsetBeats, -0.20, 0.20));
            groove.stepVelocityAccents[step] = std::clamp(maxAmp * 2.0f, 0.5f, 1.5f);
        }
    }

    return groove;
}

void GrooveQuantizer::applyGrooveToBeatTimestamps(
    std::vector<double>& beatTimestamps,
    const GrooveTemplate& groove,
    const float grooveAmount) noexcept
{
    if (groove.stepTimingOffsets.empty()) {
        return;
    }

    const float amt = std::clamp(grooveAmount, 0.0f, 1.0f);
    const auto numOffsets = groove.stepTimingOffsets.size();

    for (double& beat : beatTimestamps) {
        const double barBeat = std::fmod(beat, 4.0);
        const auto step = static_cast<std::size_t>(std::clamp(std::floor(barBeat * 4.0), 0.0, static_cast<double>(numOffsets - 1)));
        const float offset = groove.stepTimingOffsets[step];
        beat += static_cast<double>(offset * amt);
    }
}

} // namespace dawg::project
