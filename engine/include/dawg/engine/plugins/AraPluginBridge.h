#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dawg::engine::plugins {

// Immutable Audio File Source registered with ARA Document Controller
struct AraAudioSource
{
    std::string id;
    std::string name;
    double sampleRate {48000.0};
    std::uint16_t channelCount {2};
    std::size_t sampleCount {0};
    std::vector<std::vector<float>> sampleData;
};

// Non-destructive modification layer (Melodyne pitch/formant/timing edits)
struct AraAudioModification
{
    std::string id;
    std::string audioSourceId;
    std::string name;
    float globalPitchShiftSemitones {0.0f};
    float globalFormantShiftSemitones {0.0f};
    bool isModified {false};
};

// Timeline mapping: binds a slice of an Audio Modification to timeline song playback time
struct AraPlaybackRegion
{
    std::string id;
    std::string audioModificationId;
    double startInModificationSec {0.0};
    double durationInModificationSec {0.0};
    double startInPlaybackSec {0.0};
    double durationInPlaybackSec {0.0};
};

// Note detection data extracted by ARA plugin (Melodyne notes, SpectraLayers spectral components)
struct AraDetectedNote
{
    double startSeconds {0.0};
    double durationSeconds {0.0};
    float pitchMidi {60.0f};
    float pitchCentDeviation {0.0f};
    float confidence {0.9f};
};

// Musical Context synchronized between DAWg and ARA2 plugin
struct AraMusicalContext
{
    double bpm {120.0};
    int timeSigNumerator {4};
    int timeSigDenominator {4};
    std::string keyScale {"C Major"};
};

// ARA2 (Audio Random Access) Sample-Accurate Host Bridge (Celemony Melodyne / VocALign style)
// Provides plugins direct random access to audio clip samples without real-time streaming,
// coordinates bidirectional musical context synchronization, note editing, and instant offline rendering.
class AraPluginBridge final
{
public:
    AraPluginBridge() noexcept;
    explicit AraPluginBridge(std::string documentName) noexcept;
    ~AraPluginBridge() = default;

    [[nodiscard]] bool isAra2Supported() const noexcept { return m_ara2Supported; }
    [[nodiscard]] const std::string& documentName() const noexcept { return m_documentName; }

    // Audio Source & Modification Management
    bool addAudioSource(AraAudioSource source);
    bool removeAudioSource(const std::string& sourceId);
    [[nodiscard]] const AraAudioSource* getAudioSource(const std::string& sourceId) const noexcept;

    bool addAudioModification(AraAudioModification modification);
    bool updateModificationEdits(const std::string& modificationId, float pitchShift, float formantShift);
    [[nodiscard]] const AraAudioModification* getAudioModification(const std::string& modificationId) const noexcept;

    bool addPlaybackRegion(AraPlaybackRegion region);
    bool removePlaybackRegion(const std::string& regionId);
    [[nodiscard]] const AraPlaybackRegion* getPlaybackRegion(const std::string& regionId) const noexcept;
    [[nodiscard]] const std::vector<AraPlaybackRegion>& playbackRegions() const noexcept { return m_playbackRegions; }

    // Musical Context Synchronization
    void syncMusicalContext(const AraMusicalContext& context) noexcept;
    [[nodiscard]] const AraMusicalContext& musicalContext() const noexcept { return m_musicalContext; }

    // ARA Analysis: Extract detected musical notes directly from audio source
    [[nodiscard]] std::vector<AraDetectedNote> analyzeAudioSource(const std::string& sourceId) const;

    // Instant Sample-Accurate Offline Rendering
    [[nodiscard]] std::vector<std::vector<float>> renderPlaybackRegion(
        const std::string& regionId,
        double sampleRate) const;

    // Bake ARA edits directly into an audio buffer
    [[nodiscard]] std::vector<std::vector<float>> bakeEditsToBuffer(
        const std::string& modificationId,
        double sampleRate) const;

private:
    std::string m_documentName {"DAWg_ARA_Document"};
    bool m_ara2Supported {true};

    AraMusicalContext m_musicalContext {};
    std::vector<AraAudioSource> m_audioSources;
    std::vector<AraAudioModification> m_modifications;
    std::vector<AraPlaybackRegion> m_playbackRegions;
};

} // namespace dawg::engine::plugins
