#pragma once

#include <dawg/engine/dsp/MusicalScale.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class DrumHitType : std::uint8_t
{
    kick,    // MIDI 36 (C1)
    snare,   // MIDI 38 (D1)
    hihat,   // MIDI 42 (F#1)
    tom,     // MIDI 45 (A1)
    cymbal,  // MIDI 49 (C#2)
    percussion
};

struct TranscribedNote
{
    std::uint8_t pitch {60};
    std::uint8_t velocity {100};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {12000};
    DrumHitType drumType {DrumHitType::kick};
    float confidence {0.8f};
};

struct ExtractedGroove
{
    float swingPercent {0.0f};          // 0% (straight) to 75% (heavy shuffle)
    float humanVelocityVariance {0.0f}; // 0.0 to 1.0
    float microTimingDeviationMs {0.0f};
    std::vector<float> stepOffsets;      // 16-step grid offsets (-0.5 to +0.5)
};

struct TransientSlice
{
    std::uint64_t startSample {0};
    std::uint64_t endSample {0};
    float peakAmplitude {0.0f};
    DrumHitType classifiedType {DrumHitType::kick};
};

// High-Performance Audio-to-MIDI Melody & Drum Transcriber + Transient Groove Extractor
class AudioTranscriber
{
public:
    AudioTranscriber() noexcept = default;
    ~AudioTranscriber() = default;

    // Transcribe Melodic Audio (Basslines, Vocals, Guitars, Leads) into MIDI Notes
    [[nodiscard]] static std::vector<TranscribedNote> transcribeMelody(
        const float* audio,
        std::size_t sampleCount,
        double sampleRate,
        MusicalScale scale = MusicalScale::chromatic,
        std::uint8_t rootNote = 0);

    // Transcribe Rhythm/Drum Breaks into Drum MIDI Pattern (Kick, Snare, Hi-Hat)
    [[nodiscard]] static std::vector<TranscribedNote> transcribeDrums(
        const float* audio,
        std::size_t sampleCount,
        double sampleRate,
        double bpm = 120.0);

    // Extract Micro-Timing & Velocity Swing from Live Audio Performance
    [[nodiscard]] static ExtractedGroove extractGroove(
        const float* audio,
        std::size_t sampleCount,
        double sampleRate,
        double bpm = 120.0);

    // Slice Audio Breakbeat at Transients into up to 16 Pad Slices
    [[nodiscard]] static std::vector<TransientSlice> sliceTransients(
        const float* audio,
        std::size_t sampleCount,
        double sampleRate,
        std::size_t maxSlices = 16);

private:
    [[nodiscard]] static std::vector<std::size_t> detectOnsets(
        const float* audio,
        std::size_t sampleCount,
        double sampleRate,
        float sensitivity = 1.4f);

    [[nodiscard]] static DrumHitType classifyDrumHit(
        const float* audio,
        std::size_t start,
        std::size_t length,
        double sampleRate);
};

} // namespace dawg::engine::dsp
