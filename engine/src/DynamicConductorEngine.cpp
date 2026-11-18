#include <dawg/engine/DynamicConductorEngine.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dawg::engine {

namespace {

double interpolateBpm(
    const ConductorTempoNode& n0,
    const ConductorTempoNode& n1,
    const double tick) noexcept
{
    if (tick <= static_cast<double>(n0.tick))
        return n0.bpm;
    if (tick >= static_cast<double>(n1.tick))
        return n1.bpm;

    const double span = static_cast<double>(n1.tick - n0.tick);
    if (span <= 1e-6)
        return n1.bpm;

    const double t = std::clamp((tick - static_cast<double>(n0.tick)) / span, 0.0, 1.0);

    switch (n1.curveType)
    {
    case TempoCurveType::step:
        return n0.bpm;

    case TempoCurveType::linear:
        return n0.bpm + t * (n1.bpm - n0.bpm);

    case TempoCurveType::exponential:
    {
        if (n0.bpm > 0.0 && n1.bpm > 0.0)
            return n0.bpm * std::pow(n1.bpm / n0.bpm, t);
        return n0.bpm + t * (n1.bpm - n0.bpm);
    }

    case TempoCurveType::sCurve:
    {
        const double s = t * t * (3.0 - 2.0 * t);
        return n0.bpm + s * (n1.bpm - n0.bpm);
    }
    }

    return n0.bpm;
}

} // namespace

DynamicConductorEngine::DynamicConductorEngine() noexcept
{
    m_tempoNodes.push_back(ConductorTempoNode {
        .tick = 0,
        .bpm = 120.0,
        .curveType = TempoCurveType::linear,
        .tension = 0.0f
    });

    m_meterNodes.push_back(ComplexMeterNode {
        .bar = 1,
        .numerator = 4,
        .denominator = 4,
        .groupings = {2, 2}
    });
}

void DynamicConductorEngine::clearTempoNodes() noexcept
{
    m_tempoNodes.clear();
}

void DynamicConductorEngine::addTempoNode(const ConductorTempoNode& node)
{
    auto it = std::lower_bound(
        m_tempoNodes.begin(),
        m_tempoNodes.end(),
        node.tick,
        [](const ConductorTempoNode& a, std::uint64_t tick) { return a.tick < tick; });

    if (it != m_tempoNodes.end() && it->tick == node.tick)
    {
        *it = node;
    }
    else
    {
        m_tempoNodes.insert(it, node);
    }
}

double DynamicConductorEngine::bpmAtTick(const std::uint64_t tick) const noexcept
{
    if (m_tempoNodes.empty())
        return 120.0;
    if (m_tempoNodes.size() == 1 || tick <= m_tempoNodes.front().tick)
        return m_tempoNodes.front().bpm;
    if (tick >= m_tempoNodes.back().tick)
        return m_tempoNodes.back().bpm;

    for (std::size_t i = 0; i + 1 < m_tempoNodes.size(); ++i)
    {
        if (tick >= m_tempoNodes[i].tick && tick <= m_tempoNodes[i + 1].tick)
        {
            return interpolateBpm(m_tempoNodes[i], m_tempoNodes[i + 1], static_cast<double>(tick));
        }
    }

    return m_tempoNodes.back().bpm;
}

double DynamicConductorEngine::tickToSeconds(const double tick) const noexcept
{
    if (tick <= 0.0 || m_tempoNodes.empty())
        return 0.0;

    double totalSeconds = 0.0;
    constexpr double ticksPerQuarter = 960.0;

    for (std::size_t i = 0; i < m_tempoNodes.size(); ++i)
    {
        const auto currTick = static_cast<double>(m_tempoNodes[i].tick);
        if (tick <= currTick)
            break;

        const double nextTick = (i + 1 < m_tempoNodes.size())
            ? std::min(tick, static_cast<double>(m_tempoNodes[i + 1].tick))
            : tick;

        const double segmentTicks = nextTick - currTick;
        const double segmentBeats = segmentTicks / ticksPerQuarter;

        const double bpmStart = m_tempoNodes[i].bpm;
        const double bpmEnd = (i + 1 < m_tempoNodes.size())
            ? interpolateBpm(m_tempoNodes[i], m_tempoNodes[i + 1], nextTick)
            : bpmStart;

        const double avgBpm = std::max(10.0, 0.5 * (bpmStart + bpmEnd));
        totalSeconds += (segmentBeats * 60.0) / avgBpm;

        if (nextTick >= tick)
            break;
    }

    return totalSeconds;
}

double DynamicConductorEngine::secondsToTick(const double seconds) const noexcept
{
    if (seconds <= 0.0)
        return 0.0;

    // Numerical inversion via binary search
    double low = 0.0;
    double high = std::max(960.0, seconds * 120.0 / 60.0 * 960.0 * 2.0);

    for (int iter = 0; iter < 40; ++iter)
    {
        const double mid = 0.5 * (low + high);
        const double s = tickToSeconds(mid);
        if (s < seconds)
            low = mid;
        else
            high = mid;
    }

    return 0.5 * (low + high);
}

void DynamicConductorEngine::clearMeterNodes() noexcept
{
    m_meterNodes.clear();
}

void DynamicConductorEngine::addMeterNode(const ComplexMeterNode& node)
{
    auto it = std::lower_bound(
        m_meterNodes.begin(),
        m_meterNodes.end(),
        node.bar,
        [](const ComplexMeterNode& a, std::uint32_t bar) { return a.bar < bar; });

    if (it != m_meterNodes.end() && it->bar == node.bar)
    {
        *it = node;
    }
    else
    {
        m_meterNodes.insert(it, node);
    }
}

ComplexMeterNode DynamicConductorEngine::meterAtBar(const std::uint32_t bar) const noexcept
{
    if (m_meterNodes.empty())
        return ComplexMeterNode {.bar = 1, .numerator = 4, .denominator = 4, .groupings = {2, 2}};

    ComplexMeterNode active = m_meterNodes.front();
    for (const auto& m : m_meterNodes)
    {
        if (m.bar <= bar)
            active = m;
        else
            break;
    }
    return active;
}

MeterClickAccent DynamicConductorEngine::evaluateClickAccent(
    const ComplexMeterNode& meter,
    const std::size_t subdivisionIndex) noexcept
{
    if (subdivisionIndex == 0)
        return MeterClickAccent::high; // Primary downbeat

    if (meter.groupings.empty())
        return MeterClickAccent::low;

    std::size_t accumulated = 0;
    for (std::size_t i = 0; i < meter.groupings.size(); ++i)
    {
        accumulated += meter.groupings[i];
        if (subdivisionIndex == accumulated && subdivisionIndex < meter.numerator)
        {
            return MeterClickAccent::mid; // Secondary subdivision pulse
        }
    }

    return MeterClickAccent::low;
}

FittedTempoSolution DynamicConductorEngine::fitConstantTempo(
    const double startTimeSeconds,
    const double endTimeSeconds,
    const double elapsedBeats) noexcept
{
    FittedTempoSolution sol;
    const double duration = endTimeSeconds - startTimeSeconds;
    if (duration <= 0.001 || elapsedBeats <= 0.0)
    {
        sol.succeeded = false;
        return sol;
    }

    // BPM = beats * 60 / duration
    const double bpm = (elapsedBeats * 60.0) / duration;
    if (bpm < 20.0 || bpm > 999.0)
    {
        sol.succeeded = false;
        return sol;
    }

    sol.initialBpm = bpm;
    sol.targetBpm = bpm;
    sol.timeErrorMs = 0.0;
    sol.succeeded = true;
    return sol;
}

FittedTempoSolution DynamicConductorEngine::fitAccelerandoRamp(
    const double startTimeSeconds,
    const double endTimeSeconds,
    const double initialBpm,
    const double elapsedBeats) noexcept
{
    FittedTempoSolution sol;
    const double duration = endTimeSeconds - startTimeSeconds;
    if (duration <= 0.001 || elapsedBeats <= 0.0 || initialBpm <= 10.0)
    {
        sol.succeeded = false;
        return sol;
    }

    // Average BPM = (initialBpm + targetBpm) / 2
    // elapsedBeats = AvgBpm * duration / 60
    // => targetBpm = 120 * elapsedBeats / duration - initialBpm
    const double targetBpm = (120.0 * elapsedBeats / duration) - initialBpm;
    if (targetBpm < 20.0 || targetBpm > 999.0)
    {
        sol.succeeded = false;
        return sol;
    }

    sol.initialBpm = initialBpm;
    sol.targetBpm = targetBpm;
    sol.timeErrorMs = 0.0;
    sol.succeeded = true;
    return sol;
}

} // namespace dawg::engine
