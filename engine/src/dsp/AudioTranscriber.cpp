#include <dawg/engine/dsp/AudioTranscriber.h>
#include <dawg/engine/dsp/VocalPitchCorrectorNode.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dawg::engine::dsp {

std::vector<std::size_t> AudioTranscriber::detectOnsets(
    const float* audio,
    const std::size_t sampleCount,
    const double sampleRate,
    const float sensitivity)
{
    std::vector<std::size_t> onsets;
    if (audio == nullptr || sampleCount < 512 || sampleRate <= 0.0)
    {
        return onsets;
    }

    constexpr std::size_t hopSize = 128;
    const auto blockCount = sampleCount / hopSize;
    if (blockCount < 4)
    {
        return onsets;
    }

    std::vector<float> detectionFunction(blockCount, 0.0f);

    // 1. Compute High-Frequency Content (HFC) and spectral flux per hop
    for (std::size_t b = 0; b < blockCount; ++b)
    {
        const auto offset = b * hopSize;
        float hfc = 0.0f;
        float energy = 0.0f;

        for (std::size_t i = 0; i < hopSize && (offset + i) < sampleCount; ++i)
        {
            const float s = audio[offset + i];
            const float prev = (offset + i > 0) ? audio[offset + i - 1] : 0.0f;
            const float diff = s - prev;
            hfc += diff * diff;
            energy += s * s;
        }

        detectionFunction[b] = hfc * std::sqrt(energy + 1e-6f);
    }

    // Check if initial audio block begins with active signal
    float initialEnergy = 0.0f;
    for (std::size_t i = 0; i < std::min<std::size_t>(sampleCount, hopSize * 2); ++i)
    {
        initialEnergy += audio[i] * audio[i];
    }
    if (initialEnergy > 0.01f)
    {
        onsets.push_back(0);
    }

    // 2. Adaptive Moving Median/Mean Threshold Peak Picking
    constexpr std::size_t windowSize = 8;
    const auto minDistanceSamples = static_cast<std::size_t>(sampleRate * 0.035); // 35ms minimum refractory period
    std::size_t lastOnsetSample = onsets.empty() ? 0 : onsets.front();

    for (std::size_t b = 1; b < blockCount - 1; ++b)
    {
        const auto winStart = (b >= windowSize) ? (b - windowSize) : 0;
        const auto winEnd = std::min(blockCount, b + windowSize);

        float meanVal = 0.0f;
        for (std::size_t k = winStart; k < winEnd; ++k)
        {
            meanVal += detectionFunction[k];
        }
        meanVal /= static_cast<float>(winEnd - winStart);

        const float threshold = meanVal * sensitivity;
        const float val = detectionFunction[b];

        if (val > threshold && val > detectionFunction[b - 1] && val >= detectionFunction[b + 1])
        {
            const std::size_t onsetSample = b * hopSize;
            if (onsets.empty() || (onsetSample >= lastOnsetSample + minDistanceSamples))
            {
                onsets.push_back(onsetSample);
                lastOnsetSample = onsetSample;
            }
        }
    }

    return onsets;
}

DrumHitType AudioTranscriber::classifyDrumHit(
    const float* audio,
    const std::size_t start,
    const std::size_t length,
    const double sampleRate)
{
    if (audio == nullptr || length < 64 || sampleRate <= 0.0)
    {
        return DrumHitType::kick;
    }

    const auto analysisLen = std::min<std::size_t>(length, static_cast<std::size_t>(sampleRate * 0.035)); // 35ms analysis

    // 3-way IIR crossover filters: Low (< 180Hz), Mid (180Hz - 3.5kHz), High (> 3.5kHz)
    const float alphaLow = std::clamp(static_cast<float>(2.0 * std::numbers::pi * 180.0 / sampleRate), 0.001f, 0.5f);
    const float alphaMid = std::clamp(static_cast<float>(2.0 * std::numbers::pi * 3500.0 / sampleRate), 0.001f, 0.9f);

    float lpLow = 0.0f;
    float lpMid = 0.0f;
    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float highEnergy = 0.0f;

    for (std::size_t i = 0; i < analysisLen; ++i)
    {
        const float x = audio[start + i];

        lpLow += alphaLow * (x - lpLow);
        lpMid += alphaMid * (x - lpMid);

        const float bandLow = lpLow;
        const float bandMid = lpMid - lpLow;
        const float bandHigh = x - lpMid;

        lowEnergy += bandLow * bandLow;
        midEnergy += bandMid * bandMid;
        highEnergy += bandHigh * bandHigh;
    }

    // 1. Kick: Dominant low sub-bass energy (< 180 Hz)
    if (lowEnergy > midEnergy * 1.5f && lowEnergy > highEnergy * 2.0f)
    {
        return DrumHitType::kick;
    }

    // 2. Hi-Hat / Cymbal: Dominant high metallic frequencies (> 3.5 kHz) with almost zero sub/low body
    if (highEnergy > midEnergy * 1.2f && lowEnergy < highEnergy * 0.25f)
    {
        return DrumHitType::hihat;
    }

    // 3. Snare: Resonant mid body (180 Hz - 3.5 kHz) plus broadband wire response
    return DrumHitType::snare;
}

std::vector<TranscribedNote> AudioTranscriber::transcribeDrums(
    const float* audio,
    const std::size_t sampleCount,
    const double sampleRate,
    const double bpm)
{
    std::vector<TranscribedNote> notes;
    const auto onsets = detectOnsets(audio, sampleCount, sampleRate, 1.3f);
    if (onsets.empty())
    {
        return notes;
    }

    const auto defaultNoteLen = static_cast<std::uint64_t>((60.0 / std::max(20.0, bpm)) / 4.0 * sampleRate);

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto start = onsets[i];
        const auto end = (i + 1 < onsets.size()) ? onsets[i + 1] : sampleCount;
        const auto hitLen = end - start;

        // Measure peak amplitude for velocity
        float peak = 0.0f;
        for (std::size_t k = start; k < std::min(start + 512, sampleCount); ++k)
        {
            peak = std::max(peak, std::abs(audio[k]));
        }

        const auto vel = static_cast<std::uint8_t>(std::clamp(peak * 127.0f, 30.0f, 127.0f));
        const auto type = classifyDrumHit(audio, start, hitLen, sampleRate);

        std::uint8_t midiPitch = 36; // Kick default
        if (type == DrumHitType::snare) midiPitch = 38;
        else if (type == DrumHitType::hihat) midiPitch = 42;
        else if (type == DrumHitType::tom) midiPitch = 45;

        TranscribedNote note;
        note.pitch = midiPitch;
        note.velocity = vel;
        note.startSample = start;
        note.lengthSamples = std::min<std::uint64_t>(defaultNoteLen, hitLen);
        note.drumType = type;
        note.confidence = std::clamp(peak * 1.2f, 0.5f, 1.0f);
        notes.push_back(note);
    }

    return notes;
}

std::vector<TranscribedNote> AudioTranscriber::transcribeMelody(
    const float* audio,
    const std::size_t sampleCount,
    const double sampleRate,
    const MusicalScale scale,
    const std::uint8_t rootNote)
{
    std::vector<TranscribedNote> notes;
    const auto onsets = detectOnsets(audio, sampleCount, sampleRate, 1.25f);
    if (onsets.empty())
    {
        return notes;
    }

    constexpr std::size_t yinSize = 512;

    for (std::size_t i = 0; i < onsets.size(); ++i)
    {
        const auto start = onsets[i];
        const auto nextOnset = (i + 1 < onsets.size()) ? onsets[i + 1] : sampleCount;
        const auto segLen = nextOnset - start;

        if (segLen < 128)
        {
            continue;
        }

        // Measure peak amplitude
        float peak = 0.0f;
        for (std::size_t k = start; k < std::min(start + 512, sampleCount); ++k)
        {
            peak = std::max(peak, std::abs(audio[k]));
        }

        // Sample 15ms past onset to analyze steady pitch
        const auto pitchSampleStart = start + std::min<std::size_t>(segLen / 4, static_cast<std::size_t>(sampleRate * 0.015));
        if (pitchSampleStart + yinSize > sampleCount)
        {
            continue;
        }

        // Fast YIN pitch detection on analysis slice
        std::array<float, yinSize / 2> dPrime {};
        const auto halfSize = yinSize / 2;
        dPrime[0] = 1.0f;
        float runningSum = 0.0f;

        const auto minTau = std::max<std::size_t>(2, static_cast<std::size_t>(sampleRate / 1200.0));
        const auto maxTau = std::min<std::size_t>(halfSize - 1, static_cast<std::size_t>(sampleRate / 55.0));

        for (std::size_t tau = 1; tau < halfSize; ++tau)
        {
            float diffSum = 0.0f;
            for (std::size_t j = 0; j < halfSize; ++j)
            {
                const float diff = audio[pitchSampleStart + j] - audio[pitchSampleStart + j + tau];
                diffSum += diff * diff;
            }
            runningSum += diffSum;
            dPrime[tau] = (runningSum > 1e-9f) ? (diffSum * static_cast<float>(tau) / runningSum) : 1.0f;
        }

        std::size_t bestTau = 0;
        float minVal = 0.25f;
        for (std::size_t tau = minTau; tau <= maxTau; ++tau)
        {
            if (dPrime[tau] < minVal)
            {
                minVal = dPrime[tau];
                bestTau = tau;
            }
        }

        if (bestTau >= minTau && bestTau <= maxTau)
        {
            const float detectedHz = static_cast<float>(sampleRate) / static_cast<float>(bestTau);
            if (detectedHz >= 50.0f && detectedHz <= 1400.0f)
            {
                const float continuousMidi = 69.0f + 12.0f * std::log2(detectedHz / 440.0f);
                const float quantizedMidi = VocalPitchCorrectorNode::quantizeToScale(continuousMidi, scale, rootNote);
                const auto pitch = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::round(quantizedMidi)), 0, 127));

                TranscribedNote note;
                note.pitch = pitch;
                note.velocity = static_cast<std::uint8_t>(std::clamp(peak * 127.0f, 30.0f, 127.0f));
                note.startSample = start;
                note.lengthSamples = segLen;
                note.confidence = std::clamp(1.0f - minVal, 0.0f, 1.0f);
                notes.push_back(note);
            }
        }
    }

    return notes;
}

ExtractedGroove AudioTranscriber::extractGroove(
    const float* audio,
    const std::size_t sampleCount,
    const double sampleRate,
    const double bpm)
{
    ExtractedGroove groove;
    const auto onsets = detectOnsets(audio, sampleCount, sampleRate, 1.3f);
    if (onsets.size() < 4)
    {
        return groove;
    }

    const double beatSamples = (60.0 / std::max(20.0, bpm)) * sampleRate;
    const double sixteenthSamples = beatSamples / 4.0;

    std::vector<float> swingDeltas;
    std::vector<float> microTimingDeltas;
    std::vector<float> velocities;

    for (const auto onset : onsets)
    {
        // Find nearest 16th grid index
        const double gridIndexExact = static_cast<double>(onset) / sixteenthSamples;
        const auto nearestGridIndex = static_cast<int>(std::round(gridIndexExact));
        const double deltaSamples = static_cast<double>(onset) - static_cast<double>(nearestGridIndex) * sixteenthSamples;
        const float deltaNorm = static_cast<float>(deltaSamples / sixteenthSamples);

        microTimingDeltas.push_back(std::abs(static_cast<float>(deltaSamples / sampleRate * 1000.0)));

        // Even vs odd 16th note for swing estimation
        if ((nearestGridIndex % 2) != 0)
        {
            // Off-beat 16th note delay indicates swing
            if (deltaNorm >= -0.2f && deltaNorm <= 0.6f)
            {
                swingDeltas.push_back(deltaNorm);
            }
        }

        // Velocity tracking
        float peak = 0.0f;
        for (std::size_t k = onset; k < std::min(onset + 256, sampleCount); ++k)
        {
            peak = std::max(peak, std::abs(audio[k]));
        }
        velocities.push_back(peak);
    }

    // Compute average swing percent
    if (! swingDeltas.empty())
    {
        const float avgSwing = std::accumulate(swingDeltas.begin(), swingDeltas.end(), 0.0f) / static_cast<float>(swingDeltas.size());
        groove.swingPercent = std::clamp(avgSwing * 100.0f, 0.0f, 75.0f);
    }

    if (! microTimingDeltas.empty())
    {
        groove.microTimingDeviationMs = std::accumulate(microTimingDeltas.begin(), microTimingDeltas.end(), 0.0f) / static_cast<float>(microTimingDeltas.size());
    }

    if (! velocities.empty())
    {
        const float avgVel = std::accumulate(velocities.begin(), velocities.end(), 0.0f) / static_cast<float>(velocities.size());
        float varSum = 0.0f;
        for (const auto v : velocities)
        {
            varSum += (v - avgVel) * (v - avgVel);
        }
        groove.humanVelocityVariance = std::clamp(std::sqrt(varSum / static_cast<float>(velocities.size())), 0.0f, 1.0f);
    }

    return groove;
}

std::vector<TransientSlice> AudioTranscriber::sliceTransients(
    const float* audio,
    const std::size_t sampleCount,
    const double sampleRate,
    const std::size_t maxSlices)
{
    std::vector<TransientSlice> slices;
    const auto onsets = detectOnsets(audio, sampleCount, sampleRate, 1.35f);
    if (onsets.empty())
    {
        return slices;
    }

    const auto sliceCount = std::min(onsets.size(), maxSlices);
    for (std::size_t i = 0; i < sliceCount; ++i)
    {
        const auto start = onsets[i];
        const auto end = (i + 1 < onsets.size()) ? onsets[i + 1] : sampleCount;

        float peak = 0.0f;
        for (std::size_t k = start; k < std::min(start + 512, sampleCount); ++k)
        {
            peak = std::max(peak, std::abs(audio[k]));
        }

        TransientSlice slice;
        slice.startSample = start;
        slice.endSample = end;
        slice.peakAmplitude = peak;
        slice.classifiedType = classifyDrumHit(audio, start, end - start, sampleRate);
        slices.push_back(slice);
    }

    return slices;
}

} // namespace dawg::engine::dsp
