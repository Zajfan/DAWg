#pragma once

#include <dawg/engine/dsp/MusicalScale.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

struct VocalNoteBlob
{
    std::size_t id {0};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {0};
    double centerPitchMidi {60.0};
    std::uint8_t targetMidiNote {60};
    float pitchCorrectionCents {0.0f};
    float pitchDriftAmount {0.0f};      // 0.0 (natural) .. 1.0 (flat autotune)
    float vibratoDepthFactor {1.0f};     // 1.0 = original, 0.0 = flattened vibrato, 2.0 = exaggerated
    float formantShiftSemitones {0.0f}; // -12.0 .. +12.0
    float gainDb {0.0f};
    bool isSibilanceOrBreath {false};    // True if unvoiced consonant / breath
};

class VocalNoteBlobEditorEngine final
{
public:
    VocalNoteBlobEditorEngine() = default;
    ~VocalNoteBlobEditorEngine() = default;

    /// Analyzes an audio stream, extracting pitch blobs and isolating unvoiced sibilants
    void analyzeAudio(const float* audioData, std::size_t sampleCount, double sampleRate);

    [[nodiscard]] std::size_t blobCount() const noexcept { return m_blobs.size(); }
    [[nodiscard]] const std::vector<VocalNoteBlob>& blobs() const noexcept { return m_blobs; }
    [[nodiscard]] VocalNoteBlob* blobById(std::size_t id) noexcept;

    // =========================================================================
    // Melodyne / VariAudio Style Graphical Pitch Edits
    // =========================================================================

    /// 1-Click Scale Snap: quantizes note center pitches to the given scale
    void quantizePitchToScale(float strength0to1, MusicalScale scale = MusicalScale::chromatic, std::uint8_t rootKey = 0);

    /// Flattens pitch drift and scooping across a note blob
    void setBlobPitchDrift(std::size_t blobIndex, float driftAmount0to1);

    /// Scales vibrato depth (0.0 = pure flat, 1.0 = natural, 2.0 = double vibrato)
    void setBlobVibratoDepth(std::size_t blobIndex, float depthFactor);

    /// Shifts vocal formant resonances for gender morphing / vocal thickening
    void setBlobFormantShift(std::size_t blobIndex, float semitones);

    /// Independent Sibilance & Breath Volume Leveler
    void setGlobalSibilanceGainDb(float gainDb) noexcept;
    [[nodiscard]] float globalSibilanceGainDb() const noexcept { return m_sibilanceGainDb; }

    /// Synthesizes edited audio applying scale snaps, formant shifts, and sibilance attenuations
    void renderEditedAudio(float* outAudio, std::size_t maxSamples) const;

private:
    double m_sampleRate {48000.0};
    std::vector<float> m_sourceAudio;
    std::vector<VocalNoteBlob> m_blobs;
    float m_sibilanceGainDb {0.0f};
};

} // namespace dawg::engine::dsp
