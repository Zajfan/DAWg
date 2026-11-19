#pragma once

#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/dsp/ZdfLadderFilter.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace dawg::engine {

enum class JunoChorusMode : std::uint8_t
{
    off,
    chorusI,
    chorusII,
    chorusIandII
};

enum class JunoVcaMode : std::uint8_t
{
    gate,
    envelope
};

enum class JunoPwmMode : std::uint8_t
{
    manual,
    lfo
};

struct Juno106Patch
{
    std::string name {"Juno Lush Strings"};
    std::string category {"Pads"};

    // DCO
    int dcoRange {1}; // 0 = 16', 1 = 8', 2 = 4'
    float lfoRateHz {2.0f};
    float lfoDelayMs {150.0f};
    float dcoLfoDepth {0.0f}; // Vibrato
    JunoPwmMode pwmMode {JunoPwmMode::lfo};
    float pwmAmount {0.65f};
    bool pulseEnabled {true};
    bool sawEnabled {true};
    float subLevel {0.45f}; // Sub-oscillator level (0 to 1)
    float noiseLevel {0.0f};

    // HPF (0 = Bass boost, 1 = Flat, 2 = Cut, 3 = High cut)
    int hpfSetting {1};

    // VCF (IR3109 24dB 4-pole resonant filter)
    float vcfCutoff {3800.0f};
    float vcfResonance {0.45f}; // 0 to 1
    float vcfEnvAmount {0.60f};
    bool vcfEnvPolarityPos {true}; // true = positive, false = inverted
    float vcfLfoDepth {0.15f};
    float vcfKeyFollow {0.70f}; // 0 to 1

    // VCA
    JunoVcaMode vcaMode {JunoVcaMode::envelope};

    // ADSR Envelope
    float attackMs {35.0f};
    float decayMs {450.0f};
    float sustainLevel {0.75f};
    float releaseMs {650.0f};

    // BBD Stereo Chorus
    JunoChorusMode chorusMode {JunoChorusMode::chorusI};
    float bbdNoiseLevel {0.008f};
};

class Juno106SynthNode final : public ProcessorNode
{
public:
    Juno106SynthNode() noexcept;
    ~Juno106SynthNode() override = default;

    bool prepare(double sampleRate, std::size_t maxBlockSize, std::uint16_t channelCount) override;
    void reset() noexcept override;
    void process(ProcessContext& context) noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Roland Juno-106 Synth"; }

    void noteOn(std::uint8_t pitch, float velocity) noexcept;
    void noteOff(std::uint8_t pitch) noexcept;
    void allNotesOff() noexcept;

    [[nodiscard]] const Juno106Patch& patch() const noexcept { return m_patch; }
    void setPatch(const Juno106Patch& patch) noexcept { m_patch = patch; }
    void loadPreset(std::size_t index) noexcept;
    [[nodiscard]] std::size_t presetCount() const noexcept { return kPresets.size(); }
    [[nodiscard]] const char* presetName(std::size_t index) const noexcept;

    // Real-time parameter setters
    void setCutoff(float hz) noexcept { m_patch.vcfCutoff = std::clamp(hz, 10.0f, 20000.0f); }
    void setResonance(float res) noexcept { m_patch.vcfResonance = std::clamp(res, 0.0f, 1.0f); }
    void setChorusMode(JunoChorusMode mode) noexcept { m_patch.chorusMode = mode; }
    void setSubLevel(float lvl) noexcept { m_patch.subLevel = std::clamp(lvl, 0.0f, 1.0f); }
    void setPwm(float amount) noexcept { m_patch.pwmAmount = std::clamp(amount, 0.0f, 0.95f); }

private:
    struct Voice
    {
        bool active {false};
        std::uint8_t pitch {60};
        float velocity {0.8f};
        float targetFreq {261.63f};
        float phase {0.0f};
        float subPhase {0.0f};

        enum class EnvStage : std::uint8_t { idle, attack, decay, sustain, release };
        EnvStage envStage {EnvStage::idle};
        float envLevel {0.0f};

        dsp::ZdfLadderFilter vcf;

        // 1-pole HPF state
        float hpfState {0.0f};
    };

    void processVoice(Voice& v, float& outMono) noexcept;
    void applyChorus(float inMono, float& outL, float& outR) noexcept;

    double m_sampleRate {48000.0};
    Juno106Patch m_patch;

    static constexpr std::size_t kMaxVoices = 6;
    std::array<Voice, kMaxVoices> m_voices {};

    // Global LFO
    float m_lfoPhase {0.0f};

    // BBD Delay lines for Chorus (up to 30ms stereo delay)
    static constexpr std::size_t kChorusBufferLength = 2048;
    std::array<float, kChorusBufferLength> m_chorusBufferL {};
    std::array<float, kChorusBufferLength> m_chorusBufferR {};
    std::size_t m_chorusWriteIdx {0};
    float m_chorusLfoPhaseL {0.0f};
    float m_chorusLfoPhaseR {0.0f};

    // Fast noise RNG state
    std::uint32_t m_noiseRng {0x12345678};
    float nextNoise() noexcept
    {
        m_noiseRng ^= m_noiseRng << 13;
        m_noiseRng ^= m_noiseRng >> 17;
        m_noiseRng ^= m_noiseRng << 5;
        return static_cast<float>(static_cast<std::int32_t>(m_noiseRng)) * (1.0f / 2147483648.0f);
    }

    static const std::array<Juno106Patch, 6> kPresets;
};

} // namespace dawg::engine
