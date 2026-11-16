#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class ModSource : std::uint8_t
{
    macro1, macro2, macro3, macro4,
    macro5, macro6, macro7, macro8,
    lfo1, lfo2,
    mseg,
    lorenzChaos,
    audioFollower,
    mpePressure,
    mpeTimbre,
    mpePitchBend,
    count
};

enum class MsegLoopMode : std::uint8_t
{
    oneShot,
    loop,
    sustain
};

struct MsegNode
{
    float time {0.0f};    // Normalized time 0.0 to 1.0
    float level {0.0f};   // Normalized level 0.0 to 1.0
    float tension {0.0f}; // -1.0 (log/ease-in) to +1.0 (exp/ease-out), 0.0 = linear
};

struct ModulationRoute
{
    std::uint32_t targetParamId {0};
    ModSource source {ModSource::macro1};
    float depth {0.0f};       // -1.0 to +1.0 (bipolar depth)
    float minClamp {0.0f};
    float maxClamp {1.0f};
    float smoothedValue {0.0f};
    float slewMs {5.0f};      // Lag smoothing time in milliseconds
};

struct ModulationRingState
{
    float baseValue {0.0f};
    float currentValue {0.0f};
    float arcMin {0.0f};
    float arcMax {0.0f};
};

// Intelligent Macro Modulation System & Expression Matrix (Bitwig / Ableton style)
// Features unified modulation routing, Multi-Stage Envelope (MSEG), Lorenz chaotic attractor,
// audio-rate envelope follower, MPE expression, and real-time modulation ring telemetry.
class MacroModulationMatrix
{
public:
    static constexpr std::size_t MaxMacros = 8;
    static constexpr std::size_t MaxRoutes = 64;
    static constexpr std::size_t MaxMsegNodes = 16;

    MacroModulationMatrix() noexcept;
    ~MacroModulationMatrix() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Macro Knobs (0.0 to 1.0)
    void setMacro(std::size_t index, float value) noexcept;
    [[nodiscard]] float macro(std::size_t index) const noexcept;

    // Multi-Stage Envelope (MSEG)
    void setMsegNodes(const std::vector<MsegNode>& nodes);
    void setMsegLoopMode(MsegLoopMode mode) noexcept { m_msegLoopMode = mode; }
    [[nodiscard]] MsegLoopMode msegLoopMode() const noexcept { return m_msegLoopMode; }
    void setMsegRateHz(float rateHz) noexcept { m_msegRateHz = std::clamp(rateHz, 0.01f, 100.0f); }
    [[nodiscard]] float msegRateHz() const noexcept { return m_msegRateHz; }
    void triggerMseg() noexcept { m_msegPhase = 0.0f; m_msegSustaining = false; }
    void releaseMseg() noexcept { m_msegSustaining = false; }

    // Lorenz Chaotic Attractor
    void setLorenzSpeed(float speedHz) noexcept { m_lorenzSpeedHz = std::clamp(speedHz, 0.01f, 50.0f); }
    [[nodiscard]] float lorenzSpeed() const noexcept { return m_lorenzSpeedHz; }
    [[nodiscard]] float lorenzX() const noexcept { return m_lorenzNormalizedX; }
    [[nodiscard]] float lorenzY() const noexcept { return m_lorenzNormalizedY; }

    // Audio-Rate Envelope Follower
    void setAudioFollowerAttack(float ms) noexcept;
    void setAudioFollowerRelease(float ms) noexcept;

    // MIDI / MPE Expression
    void processMidi(const MidiBufferView& midi) noexcept;
    void setMpePressure(float val) noexcept { m_mpePressure = std::clamp(val, 0.0f, 1.0f); }
    void setMpeTimbre(float val) noexcept { m_mpeTimbre = std::clamp(val, 0.0f, 1.0f); }
    void setMpePitchBend(float val) noexcept { m_mpePitchBend = std::clamp(val, -1.0f, 1.0f); }

    // Routing Matrix API
    bool addRoute(const ModulationRoute& route);
    void clearRoutes() noexcept;
    [[nodiscard]] std::size_t routeCount() const noexcept { return m_routes.size(); }

    // Real-Time Frame Processing
    void process(
        const float* const* audioChannels,
        std::size_t numChannels,
        std::size_t frameCount) noexcept;

    // Parameter Evaluation & Modulation Rings
    [[nodiscard]] float evaluateModulatedParam(std::uint32_t targetParamId, float baseValue) const noexcept;
    [[nodiscard]] ModulationRingState getModulationRing(std::uint32_t targetParamId, float baseValue) const noexcept;
    [[nodiscard]] float getSourceValue(ModSource source) const noexcept;

private:
    float evaluateMseg(float phase) const noexcept;
    void updateLorenzAttractor(float dt) noexcept;

    double m_sampleRate {48000.0};

    // 8 Macro Knobs
    std::array<float, MaxMacros> m_macros {};

    // MSEG State
    std::vector<MsegNode> m_msegNodes;
    MsegLoopMode m_msegLoopMode {MsegLoopMode::loop};
    float m_msegRateHz {1.0f};
    float m_msegPhase {0.0f};
    bool m_msegSustaining {false};
    float m_currentMsegVal {0.0f};

    // Lorenz Attractor State
    float m_lorenzX {0.1f};
    float m_lorenzY {0.0f};
    float m_lorenzZ {0.0f};
    float m_lorenzNormalizedX {0.5f};
    float m_lorenzNormalizedY {0.5f};
    float m_lorenzSpeedHz {1.0f};

    // Audio Follower State
    float m_followerAttackCoeff {0.1f};
    float m_followerReleaseCoeff {0.01f};
    float m_audioFollowerLevel {0.0f};

    // MPE Expression State
    float m_mpePressure {0.0f};
    float m_mpeTimbre {0.5f};
    float m_mpePitchBend {0.0f};

    // Routing table
    std::vector<ModulationRoute> m_routes;
};

} // namespace dawg::engine::dsp
