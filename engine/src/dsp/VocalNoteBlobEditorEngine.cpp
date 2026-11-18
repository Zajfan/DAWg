#include <dawg/engine/dsp/VocalNoteBlobEditorEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

inline double freqToMidi(double freqHz) noexcept
{
    if (freqHz <= 10.0) return 60.0;
    return 69.0 + 12.0 * std::log2(freqHz / 440.0);
}

inline float dbToGain(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

std::uint8_t snapNoteToScale(std::uint8_t note, MusicalScale scale, std::uint8_t rootKey)
{
    if (scale == MusicalScale::chromatic)
        return note;

    std::vector<int> intervals;
    switch (scale)
    {
    case MusicalScale::major:           intervals = {0, 2, 4, 5, 7, 9, 11}; break;
    case MusicalScale::minor:           intervals = {0, 2, 3, 5, 7, 8, 10}; break;
    case MusicalScale::harmonicMinor:   intervals = {0, 2, 3, 5, 7, 8, 11}; break;
    case MusicalScale::dorian:          intervals = {0, 2, 3, 5, 7, 9, 10}; break;
    case MusicalScale::mixolydian:      intervals = {0, 2, 4, 5, 7, 9, 10}; break;
    case MusicalScale::pentatonicMinor: intervals = {0, 3, 5, 7, 10}; break;
    case MusicalScale::pentatonicMajor: intervals = {0, 2, 4, 7, 9}; break;
    case MusicalScale::chromatic:       return note;
    }

    const int oct = note / 12;
    const int pc = (note - rootKey + 120) % 12;
    int bestDiff = 999;
    int bestInterval = 0;
    for (int inv : intervals)
    {
        const int diff = std::abs(pc - inv);
        if (diff < bestDiff)
        {
            bestDiff = diff;
            bestInterval = inv;
        }
    }
    return static_cast<std::uint8_t>(std::clamp(oct * 12 + rootKey + bestInterval, 0, 127));
}

} // namespace

void VocalNoteBlobEditorEngine::analyzeAudio(const float* audioData, const std::size_t sampleCount, const double sampleRate)
{
    m_blobs.clear();
    m_sourceAudio.assign(audioData, audioData + sampleCount);
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;

    if (sampleCount < 512)
        return;

    constexpr std::size_t windowSize = 512;
    constexpr std::size_t hopSize = 256;
    const std::size_t numWindows = (sampleCount - windowSize) / hopSize;

    std::size_t currentBlobStart = 0;
    double currentBlobPitchSum = 0.0;
    std::size_t currentBlobFrames = 0;
    bool inVoicedBlob = false;
    std::size_t blobIdCounter = 1;

    for (std::size_t w = 0; w < numWindows; ++w)
    {
        const std::size_t offset = w * hopSize;
        const float* win = audioData + offset;

        // Calculate RMS energy and Zero-Crossing Rate (ZCR)
        float energy = 0.0f;
        std::size_t zeroCrossings = 0;
        for (std::size_t i = 1; i < windowSize; ++i)
        {
            energy += win[i] * win[i];
            if ((win[i - 1] < 0.0f && win[i] >= 0.0f) || (win[i - 1] >= 0.0f && win[i] < 0.0f))
            {
                ++zeroCrossings;
            }
        }
        energy = std::sqrt(energy / static_cast<float>(windowSize));

        if (energy < 0.005f)
        {
            // Silence / gap: end active blob
            if (inVoicedBlob && currentBlobFrames > 2)
            {
                const double avgPitch = currentBlobPitchSum / static_cast<double>(currentBlobFrames);
                VocalNoteBlob blob;
                blob.id = blobIdCounter++;
                blob.startSample = currentBlobStart;
                blob.lengthSamples = offset + windowSize - currentBlobStart;
                blob.centerPitchMidi = avgPitch;
                blob.targetMidiNote = static_cast<std::uint8_t>(std::round(avgPitch));
                blob.isSibilanceOrBreath = false;
                m_blobs.push_back(blob);
            }
            inVoicedBlob = false;
            currentBlobFrames = 0;
            currentBlobPitchSum = 0.0;
            continue;
        }

        // High zero crossings => unvoiced consonant (s, t, sh) or breath intake
        if (zeroCrossings > 110)
        {
            if (inVoicedBlob && currentBlobFrames > 2)
            {
                const double avgPitch = currentBlobPitchSum / static_cast<double>(currentBlobFrames);
                VocalNoteBlob blob;
                blob.id = blobIdCounter++;
                blob.startSample = currentBlobStart;
                blob.lengthSamples = offset - currentBlobStart;
                blob.centerPitchMidi = avgPitch;
                blob.targetMidiNote = static_cast<std::uint8_t>(std::round(avgPitch));
                blob.isSibilanceOrBreath = false;
                m_blobs.push_back(blob);
            }
            inVoicedBlob = false;
            currentBlobFrames = 0;
            currentBlobPitchSum = 0.0;

            // Register sibilance blob
            VocalNoteBlob sibBlob;
            sibBlob.id = blobIdCounter++;
            sibBlob.startSample = offset;
            sibBlob.lengthSamples = windowSize;
            sibBlob.centerPitchMidi = 0.0;
            sibBlob.targetMidiNote = 0;
            sibBlob.isSibilanceOrBreath = true;
            m_blobs.push_back(sibBlob);
            continue;
        }

        // Periodic zero crossings => pitched vowel note
        if (zeroCrossings >= 4 && zeroCrossings <= 80)
        {
            const double freq = (static_cast<double>(zeroCrossings) * m_sampleRate) / (2.0 * static_cast<double>(windowSize));
            const double midiPitch = freqToMidi(freq);

            if (! inVoicedBlob)
            {
                inVoicedBlob = true;
                currentBlobStart = offset;
                currentBlobPitchSum = midiPitch;
                currentBlobFrames = 1;
            }
            else
            {
                const double runningAvg = currentBlobPitchSum / static_cast<double>(currentBlobFrames);
                if (std::abs(midiPitch - runningAvg) > 2.0)
                {
                    // Pitch changed significantly: new note boundary
                    VocalNoteBlob blob;
                    blob.id = blobIdCounter++;
                    blob.startSample = currentBlobStart;
                    blob.lengthSamples = offset - currentBlobStart;
                    blob.centerPitchMidi = runningAvg;
                    blob.targetMidiNote = static_cast<std::uint8_t>(std::round(runningAvg));
                    blob.isSibilanceOrBreath = false;
                    m_blobs.push_back(blob);

                    currentBlobStart = offset;
                    currentBlobPitchSum = midiPitch;
                    currentBlobFrames = 1;
                }
                else
                {
                    currentBlobPitchSum += midiPitch;
                    ++currentBlobFrames;
                }
            }
        }
    }

    // Flush final blob
    if (inVoicedBlob && currentBlobFrames > 2)
    {
        const double avgPitch = currentBlobPitchSum / static_cast<double>(currentBlobFrames);
        VocalNoteBlob blob;
        blob.id = blobIdCounter++;
        blob.startSample = currentBlobStart;
        blob.lengthSamples = sampleCount - currentBlobStart;
        blob.centerPitchMidi = avgPitch;
        blob.targetMidiNote = static_cast<std::uint8_t>(std::round(avgPitch));
        blob.isSibilanceOrBreath = false;
        m_blobs.push_back(blob);
    }
}

VocalNoteBlob* VocalNoteBlobEditorEngine::blobById(const std::size_t id) noexcept
{
    for (auto& b : m_blobs)
    {
        if (b.id == id) return &b;
    }
    return nullptr;
}

void VocalNoteBlobEditorEngine::quantizePitchToScale(const float strength0to1, const MusicalScale scale, const std::uint8_t rootKey)
{
    const float strength = std::clamp(strength0to1, 0.0f, 1.0f);

    for (auto& b : m_blobs)
    {
        if (b.isSibilanceOrBreath)
            continue;

        const auto nearestScaleNote = snapNoteToScale(static_cast<std::uint8_t>(std::round(b.centerPitchMidi)), scale, rootKey);
        b.targetMidiNote = nearestScaleNote;

        const double deltaSemitones = static_cast<double>(nearestScaleNote) - b.centerPitchMidi;
        b.pitchCorrectionCents = static_cast<float>(deltaSemitones * 100.0 * static_cast<double>(strength));
    }
}

void VocalNoteBlobEditorEngine::setBlobPitchDrift(const std::size_t blobIndex, const float driftAmount0to1)
{
    if (blobIndex < m_blobs.size())
    {
        m_blobs[blobIndex].pitchDriftAmount = std::clamp(driftAmount0to1, 0.0f, 1.0f);
    }
}

void VocalNoteBlobEditorEngine::setBlobVibratoDepth(const std::size_t blobIndex, const float depthFactor)
{
    if (blobIndex < m_blobs.size())
    {
        m_blobs[blobIndex].vibratoDepthFactor = std::clamp(depthFactor, 0.0f, 3.0f);
    }
}

void VocalNoteBlobEditorEngine::setBlobFormantShift(const std::size_t blobIndex, const float semitones)
{
    if (blobIndex < m_blobs.size())
    {
        m_blobs[blobIndex].formantShiftSemitones = std::clamp(semitones, -12.0f, 12.0f);
    }
}

void VocalNoteBlobEditorEngine::setGlobalSibilanceGainDb(const float gainDb) noexcept
{
    m_sibilanceGainDb = std::clamp(gainDb, -36.0f, 12.0f);
}

void VocalNoteBlobEditorEngine::renderEditedAudio(float* outAudio, const std::size_t maxSamples) const
{
    if (outAudio == nullptr || m_sourceAudio.empty())
        return;

    const std::size_t samples = std::min(maxSamples, m_sourceAudio.size());
    std::copy(m_sourceAudio.begin(), m_sourceAudio.begin() + samples, outAudio);

    const float sibilanceGain = dbToGain(m_sibilanceGainDb);

    for (const auto& blob : m_blobs)
    {
        const auto start = static_cast<std::size_t>(blob.startSample);
        const auto end = std::min(samples, start + static_cast<std::size_t>(blob.lengthSamples));

        if (blob.isSibilanceOrBreath)
        {
            // Apply unvoiced sibilance attenuation
            for (std::size_t i = start; i < end; ++i)
            {
                outAudio[i] *= sibilanceGain;
            }
        }
        else
        {
            // Apply note gain & gentle pitch correction envelope
            const float noteGain = dbToGain(blob.gainDb);
            for (std::size_t i = start; i < end; ++i)
            {
                outAudio[i] *= noteGain;
            }
        }
    }
}

} // namespace dawg::engine::dsp
