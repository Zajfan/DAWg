#pragma once

#include <dawg/engine/SessionMatrixEngine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dawg::engine {

// Probabilistic / Markov-chain scene follow engine (Option 5).
// Builds on the SessionMatrixEngine FollowAction vocabulary and adds:
// - A weighted scene-to-scene transition matrix (Markov chain)
// - Per-scene repetition counters (play N bars before branching)
// - Dual follow actions with A/B percentage chance
// - Long-run scene occupancy (stationary distribution) for previewing generative sets
// Uses a seeded xorshift RNG so performances are reproducible.
class MarkovSceneFollowEngine final
{
public:
    explicit MarkovSceneFollowEngine(std::size_t sceneCount = 8, std::uint64_t seed = 0x2545F4914F6CDD1Dull);

    void setSceneCount(std::size_t count);
    [[nodiscard]] std::size_t sceneCount() const noexcept { return m_matrix.size(); }

    void setTransitionWeight(std::size_t from, std::size_t to, float weight) noexcept;
    [[nodiscard]] float transitionProbability(std::size_t from, std::size_t to) const noexcept;
    void setLinearTransitions() noexcept; // scene i -> i+1 (wrapping), a plain song order

    void setRepeatBars(std::size_t scene, std::uint32_t bars) noexcept;
    [[nodiscard]] std::uint32_t repeatBars(std::size_t scene) const noexcept;

    void setCurrentScene(std::size_t scene) noexcept;
    [[nodiscard]] std::size_t currentScene() const noexcept { return m_current; }

    // Call once per bar; returns the next scene when the current scene's repeat count elapses.
    std::optional<std::size_t> advanceBar() noexcept;
    [[nodiscard]] std::size_t chooseNextScene(std::size_t from) noexcept;

    // FollowAction resolution (nullopt = stop)
    [[nodiscard]] std::optional<std::size_t> resolveFollowAction(FollowAction action, std::size_t current,
                                                                 std::size_t jumpTarget) noexcept;
    [[nodiscard]] std::optional<std::size_t> resolveDualFollowAction(FollowAction actionA, FollowAction actionB,
                                                                     float chanceA, std::size_t current,
                                                                     std::size_t jumpA, std::size_t jumpB) noexcept;

    [[nodiscard]] std::vector<double> stationaryDistribution(std::size_t iterations = 512) const;

    void reseed(std::uint64_t seed) noexcept { m_rng = seed != 0 ? seed : 1; }
    [[nodiscard]] double nextUniform() noexcept;

private:
    std::vector<std::vector<float>> m_matrix;
    std::vector<std::uint32_t> m_repeatBars;
    std::uint32_t m_barCounter {0};
    std::size_t m_current {0};
    std::uint64_t m_rng {1};
};

enum class GrooveTemplate : std::uint8_t
{
    straight,
    mpc60,      // 16th swing (default 62%), timing rounded to 96 PPQN
    sp1200,     // 16th swing (default 58%), flattened velocity steps
    linnDrum,   // Swing snapped to Linn's classic settings: 50/54/58/62/66/71%
    dillaPocket // Light swing + per-instrument push/pull (early kicks, late snares, loose hats)
};

struct GrooveNote
{
    std::uint8_t pitch {36};
    double startBeat {0.0};
    double durationBeats {0.25};
    std::uint8_t velocity {100};
};

struct GrooveSettings
{
    GrooveTemplate grooveTemplate {GrooveTemplate::mpc60};
    float swingPercent {-1.0f};   // < 0 = use template default; 50 = straight .. 75 = hard swing
    float strength {1.0f};        // 0 .. 1 blend from original timing to grooved timing
    float humanizeMs {0.0f};      // random timing spread (approx. gaussian)
    double bpm {90.0};
    std::uint64_t seed {12345};
};

struct GrooveProfile
{
    std::array<double, 16> offsetBeats {};   // Per 16th slot within a 4-beat bar
    std::array<float, 16> velocityScale {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                         1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

// Groove & swing pocket quantizer (Option 5).
// Templates are musically-motivated approximations of each machine's feel, not sample-accurate
// measurements of the original hardware.
class GroovePocketQuantizer final
{
public:
    GroovePocketQuantizer() = delete;

    [[nodiscard]] static float defaultSwing(GrooveTemplate grooveTemplate) noexcept;
    [[nodiscard]] static double swungGridPosition(double beat, float swingPercent) noexcept;
    [[nodiscard]] static std::vector<GrooveNote> apply(const std::vector<GrooveNote>& notes, const GrooveSettings& settings);

    // Groove extraction: average deviation from the straight 16th grid per bar slot
    [[nodiscard]] static GrooveProfile extractProfile(const std::vector<GrooveNote>& notes);
    [[nodiscard]] static std::vector<GrooveNote> applyProfile(const std::vector<GrooveNote>& notes,
                                                              const GrooveProfile& profile, float strength = 1.0f);
};

} // namespace dawg::engine
