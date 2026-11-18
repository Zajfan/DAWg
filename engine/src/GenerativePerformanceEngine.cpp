#include <dawg/engine/GenerativePerformanceEngine.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dawg::engine {

namespace {

uint64_t xorshift64(uint64_t& state) noexcept
{
    uint64_t x = state;
    if (x == 0) x = 0x2545F4914F6CDD1Dull;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state = x;
    return x;
}

double uniformFromRng(uint64_t& state) noexcept
{
    return static_cast<double>(xorshift64(state) >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace

// ---------------------------------------------------------------------------
// MarkovSceneFollowEngine
// ---------------------------------------------------------------------------

MarkovSceneFollowEngine::MarkovSceneFollowEngine(const std::size_t sceneCount, const std::uint64_t seed)
    : m_rng(seed != 0 ? seed : 1)
{
    setSceneCount(sceneCount);
}

void MarkovSceneFollowEngine::setSceneCount(const std::size_t count)
{
    const std::size_t validCount = std::max<std::size_t>(1, count);
    m_matrix.assign(validCount, std::vector<float>(validCount, 0.0f));
    m_repeatBars.assign(validCount, 1);
    setLinearTransitions();
    if (m_current >= validCount)
        m_current = 0;
    m_barCounter = 0;
}

void MarkovSceneFollowEngine::setTransitionWeight(const std::size_t from, const std::size_t to, const float weight) noexcept
{
    if (from < m_matrix.size() && to < m_matrix[from].size())
    {
        m_matrix[from][to] = std::max(0.0f, weight);
    }
}

float MarkovSceneFollowEngine::transitionProbability(const std::size_t from, const std::size_t to) const noexcept
{
    if (from >= m_matrix.size() || to >= m_matrix[from].size())
        return 0.0f;

    float sum = 0.0f;
    for (const float w : m_matrix[from])
        sum += w;

    if (sum <= 1e-9f)
        return from == to ? 1.0f : 0.0f;

    return m_matrix[from][to] / sum;
}

void MarkovSceneFollowEngine::setLinearTransitions() noexcept
{
    const std::size_t n = m_matrix.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        std::fill(m_matrix[i].begin(), m_matrix[i].end(), 0.0f);
        m_matrix[i][(i + 1) % n] = 1.0f;
    }
}

void MarkovSceneFollowEngine::setRepeatBars(const std::size_t scene, const std::uint32_t bars) noexcept
{
    if (scene < m_repeatBars.size())
    {
        m_repeatBars[scene] = std::max<std::uint32_t>(1, bars);
    }
}

std::uint32_t MarkovSceneFollowEngine::repeatBars(const std::size_t scene) const noexcept
{
    return scene < m_repeatBars.size() ? m_repeatBars[scene] : 1;
}

void MarkovSceneFollowEngine::setCurrentScene(const std::size_t scene) noexcept
{
    if (scene < m_matrix.size())
    {
        m_current = scene;
        m_barCounter = 0;
    }
}

std::optional<std::size_t> MarkovSceneFollowEngine::advanceBar() noexcept
{
    if (m_matrix.empty())
        return std::nullopt;

    ++m_barCounter;
    const std::uint32_t targetBars = repeatBars(m_current);
    if (m_barCounter >= targetBars)
    {
        m_barCounter = 0;
        m_current = chooseNextScene(m_current);
        return m_current;
    }

    return std::nullopt;
}

std::size_t MarkovSceneFollowEngine::chooseNextScene(const std::size_t from) noexcept
{
    const std::size_t n = m_matrix.size();
    if (n == 0) return 0;
    if (from >= n) return 0;

    float sum = 0.0f;
    for (const float w : m_matrix[from])
        sum += w;

    if (sum <= 1e-9f)
        return (from + 1) % n;

    const double roll = nextUniform() * static_cast<double>(sum);
    double accum = 0.0;
    for (std::size_t to = 0; to < n; ++to)
    {
        accum += static_cast<double>(m_matrix[from][to]);
        if (roll <= accum)
            return to;
    }

    return (from + 1) % n;
}

std::optional<std::size_t> MarkovSceneFollowEngine::resolveFollowAction(
    const FollowAction action,
    const std::size_t current,
    const std::size_t jumpTarget) noexcept
{
    const std::size_t n = m_matrix.size();
    if (n == 0) return std::nullopt;

    switch (action)
    {
    case FollowAction::none:
    case FollowAction::stop:
        return std::nullopt;

    case FollowAction::nextClip:
        return (current + 1) % n;

    case FollowAction::prevClip:
        return current == 0 ? (n - 1) : (current - 1);

    case FollowAction::firstClip:
        return 0;

    case FollowAction::lastClip:
        return n - 1;

    case FollowAction::randomClip:
        return static_cast<std::size_t>(nextUniform() * static_cast<double>(n)) % n;

    case FollowAction::jumpToScene:
        return jumpTarget % n;
    }

    return std::nullopt;
}

std::optional<std::size_t> MarkovSceneFollowEngine::resolveDualFollowAction(
    const FollowAction actionA,
    const FollowAction actionB,
    const float chanceA,
    const std::size_t current,
    const std::size_t jumpA,
    const std::size_t jumpB) noexcept
{
    const float roll = static_cast<float>(nextUniform());
    if (roll < std::clamp(chanceA, 0.0f, 1.0f))
    {
        return resolveFollowAction(actionA, current, jumpA);
    }
    return resolveFollowAction(actionB, current, jumpB);
}

std::vector<double> MarkovSceneFollowEngine::stationaryDistribution(const std::size_t iterations) const
{
    const std::size_t n = m_matrix.size();
    if (n == 0) return {};

    std::vector<double> dist(n, 1.0 / static_cast<double>(n));
    std::vector<double> nextDist(n, 0.0);

    for (std::size_t iter = 0; iter < iterations; ++iter)
    {
        std::fill(nextDist.begin(), nextDist.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            float rowSum = 0.0f;
            for (std::size_t j = 0; j < n; ++j) rowSum += m_matrix[i][j];

            if (rowSum > 1e-9f)
            {
                for (std::size_t j = 0; j < n; ++j)
                {
                    const double p = static_cast<double>(m_matrix[i][j] / rowSum);
                    nextDist[j] += dist[i] * p;
                }
            }
            else
            {
                nextDist[(i + 1) % n] += dist[i];
            }
        }
        dist = nextDist;
    }

    // Normalise
    const double sum = std::accumulate(dist.begin(), dist.end(), 0.0);
    if (sum > 1e-12)
    {
        for (double& d : dist) d /= sum;
    }
    return dist;
}

double MarkovSceneFollowEngine::nextUniform() noexcept
{
    return uniformFromRng(m_rng);
}

// ---------------------------------------------------------------------------
// GroovePocketQuantizer
// ---------------------------------------------------------------------------

float GroovePocketQuantizer::defaultSwing(const GrooveTemplate grooveTemplate) noexcept
{
    switch (grooveTemplate)
    {
    case GrooveTemplate::straight:   return 50.0f;
    case GrooveTemplate::mpc60:      return 62.0f;
    case GrooveTemplate::sp1200:     return 58.0f;
    case GrooveTemplate::linnDrum:   return 58.0f;
    case GrooveTemplate::dillaPocket:return 54.0f;
    }
    return 50.0f;
}

double GroovePocketQuantizer::swungGridPosition(const double beat, const float swingPercent) noexcept
{
    constexpr double step = 0.25; // 16th note
    const double posInQuarter = std::fmod(beat, 1.0);
    const double baseQuarter = beat - posInQuarter;
    const double stepIndexExact = posInQuarter / step;
    const auto stepIdx = static_cast<int>(std::floor(stepIndexExact));
    const double frac = stepIndexExact - static_cast<double>(stepIdx);

    // Pair division (1st 16th vs 2nd 16th of each 8th note pair)
    // swingPercent = 50% means 50/50 split of the 8th note (0.5 beats)
    const double swingRatio = static_cast<double>(std::clamp(swingPercent, 40.0f, 85.0f)) / 100.0;
    const double offbeatShift = (swingRatio - 0.5) * 0.5;

    double targetAnchor = static_cast<double>(stepIdx) * step;
    if ((stepIdx % 2) == 1) // odd 16th (the swung note)
    {
        targetAnchor += offbeatShift;
    }

    return baseQuarter + targetAnchor + frac * step;
}

std::vector<GrooveNote> GroovePocketQuantizer::apply(
    const std::vector<GrooveNote>& notes,
    const GrooveSettings& settings)
{
    if (notes.empty())
        return {};

    const float swing = settings.swingPercent >= 0.0f ? settings.swingPercent : defaultSwing(settings.grooveTemplate);
    const float strength = std::clamp(settings.strength, 0.0f, 1.0f);
    const double secPerBeat = settings.bpm > 0.0 ? (60.0 / settings.bpm) : 0.5;
    uint64_t rng = settings.seed != 0 ? settings.seed : 12345;

    std::vector<GrooveNote> result = notes;

    for (auto& n : result)
    {
        // 1. Calculate swung grid target
        const double swung = swungGridPosition(n.startBeat, swing);
        double targetBeat = n.startBeat + (swung - n.startBeat) * static_cast<double>(strength);

        // 2. Template-specific micro-timing & velocity mechanics
        switch (settings.grooveTemplate)
        {
        case GrooveTemplate::straight:
            break;

        case GrooveTemplate::mpc60:
        {
            // MPC 60: 96 PPQN grid rounding
            constexpr double mpcPpqn = 96.0;
            const double tick = std::round(targetBeat * mpcPpqn) / mpcPpqn;
            targetBeat = targetBeat + (tick - targetBeat) * static_cast<double>(strength);
            break;
        }

        case GrooveTemplate::sp1200:
        {
            // SP-1200: 8-level velocity truncation characteristic of 12-bit pads
            const auto level = static_cast<uint8_t>((n.velocity / 16) * 16 + 8);
            n.velocity = static_cast<uint8_t>(std::clamp<int>(
                static_cast<int>(n.velocity + (level - n.velocity) * strength), 1, 127));
            break;
        }

        case GrooveTemplate::linnDrum:
        {
            // Roger Linn signature accent dynamics (downbeats 1 & 3 punch harder)
            const double beatInBar = std::fmod(targetBeat, 4.0);
            if (beatInBar < 0.1 || std::abs(beatInBar - 2.0) < 0.1)
            {
                n.velocity = static_cast<uint8_t>(std::min(127, static_cast<int>(n.velocity * (1.0f + 0.15f * strength))));
            }
            break;
        }

        case GrooveTemplate::dillaPocket:
        {
            // J Dilla push & pull pocket:
            // Kick (36) is nudged slightly early (-8 ms)
            // Snare / Rim (38, 40) is delayed late into the pocket (+15 ms)
            // Closed Hi-Hat (42) has subtle wandering humanize
            double dillaOffsetSec = 0.0;
            if (n.pitch == 36)
            {
                dillaOffsetSec = -0.008 * static_cast<double>(strength);
            }
            else if (n.pitch == 38 || n.pitch == 40)
            {
                dillaOffsetSec = 0.016 * static_cast<double>(strength);
            }
            else if (n.pitch == 42 || n.pitch == 44 || n.pitch == 46)
            {
                dillaOffsetSec = 0.006 * static_cast<double>(strength);
            }

            const double offsetBeats = dillaOffsetSec / secPerBeat;
            targetBeat += offsetBeats;
            break;
        }
        }

        // 3. Humanize timing jitter
        if (settings.humanizeMs > 0.0f)
        {
            const double jitterSec = (uniformFromRng(rng) * 2.0 - 1.0) * static_cast<double>(settings.humanizeMs * 0.001f);
            targetBeat += (jitterSec / secPerBeat) * static_cast<double>(strength);
        }

        n.startBeat = std::max(0.0, targetBeat);
    }

    return result;
}

GrooveProfile GroovePocketQuantizer::extractProfile(const std::vector<GrooveNote>& notes)
{
    GrooveProfile profile;
    if (notes.empty()) return profile;

    std::array<double, 16> sumOffsets {};
    std::array<double, 16> sumVelocities {};
    std::array<std::size_t, 16> counts {};

    for (const auto& n : notes)
    {
        const double beatInBar = std::fmod(n.startBeat, 4.0);
        const double slotIndexExact = beatInBar / 0.25;
        const auto slot = static_cast<std::size_t>(std::clamp<int>(static_cast<int>(std::round(slotIndexExact)) % 16, 0, 15));
        const double gridBeat = static_cast<double>(slot) * 0.25;
        const double offset = beatInBar - gridBeat;

        sumOffsets[slot] += offset;
        sumVelocities[slot] += static_cast<double>(n.velocity);
        counts[slot]++;
    }

    double globalAvgVel = 100.0;
    std::size_t totalCount = 0;
    for (std::size_t i = 0; i < 16; ++i)
    {
        totalCount += counts[i];
    }
    if (totalCount > 0)
    {
        double totalVel = 0.0;
        for (std::size_t i = 0; i < 16; ++i) totalVel += sumVelocities[i];
        globalAvgVel = std::max(1.0, totalVel / static_cast<double>(totalCount));
    }

    for (std::size_t i = 0; i < 16; ++i)
    {
        if (counts[i] > 0)
        {
            profile.offsetBeats[i] = sumOffsets[i] / static_cast<double>(counts[i]);
            const double avgSlotVel = sumVelocities[i] / static_cast<double>(counts[i]);
            profile.velocityScale[i] = static_cast<float>(avgSlotVel / globalAvgVel);
        }
        else
        {
            profile.offsetBeats[i] = 0.0;
            profile.velocityScale[i] = 1.0f;
        }
    }

    return profile;
}

std::vector<GrooveNote> GroovePocketQuantizer::applyProfile(
    const std::vector<GrooveNote>& notes,
    const GrooveProfile& profile,
    const float strength)
{
    if (notes.empty()) return {};

    const float clampedStrength = std::clamp(strength, 0.0f, 1.0f);
    std::vector<GrooveNote> result = notes;

    for (auto& n : result)
    {
        const double beatInBar = std::fmod(n.startBeat, 4.0);
        const auto slot = static_cast<std::size_t>(std::clamp<int>(static_cast<int>(std::round(beatInBar / 0.25)) % 16, 0, 15));

        const double shift = profile.offsetBeats[slot] * static_cast<double>(clampedStrength);
        n.startBeat = std::max(0.0, n.startBeat + shift);

        const float velScale = 1.0f + (profile.velocityScale[slot] - 1.0f) * clampedStrength;
        n.velocity = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(static_cast<float>(n.velocity) * velScale), 1, 127));
    }

    return result;
}

} // namespace dawg::engine
