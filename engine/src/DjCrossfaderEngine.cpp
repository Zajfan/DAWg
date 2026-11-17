#include <dawg/engine/DjCrossfaderEngine.h>

#include <algorithm>

namespace dawg::engine {

void DjCrossfaderEngine::reset() noexcept
{
    m_position = 0.0f;
    m_curve = CrossfaderCurve::smoothEqualPower;
    m_assignmentCount = 0;
    for (auto& entry : m_assignments)
    {
        entry.trackId = 0;
        entry.assign = CrossfadeAssign::none;
    }
}

void DjCrossfaderEngine::setPosition(const float position) noexcept
{
    m_position = std::clamp(position, -1.0f, 1.0f);
}

void DjCrossfaderEngine::setTrackAssignment(const std::uint64_t trackId, const CrossfadeAssign assign) noexcept
{
    for (std::size_t i = 0; i < m_assignmentCount; ++i)
    {
        if (m_assignments[i].trackId == trackId)
        {
            m_assignments[i].assign = assign;
            return;
        }
    }

    if (m_assignmentCount < maxTrackAssigns)
    {
        m_assignments[m_assignmentCount].trackId = trackId;
        m_assignments[m_assignmentCount].assign = assign;
        m_assignmentCount++;
    }
}

CrossfadeAssign DjCrossfaderEngine::getTrackAssignment(const std::uint64_t trackId) const noexcept
{
    for (std::size_t i = 0; i < m_assignmentCount; ++i)
    {
        if (m_assignments[i].trackId == trackId)
            return m_assignments[i].assign;
    }
    return CrossfadeAssign::none;
}

void DjCrossfaderEngine::getBusGains(float& outGainA, float& outGainB) const noexcept
{
    const float u = (m_position + 1.0f) * 0.5f; // [0.0 = A, 1.0 = B]

    switch (m_curve)
    {
    case CrossfaderCurve::smoothEqualPower:
    {
        constexpr float piOver2 = 1.5707963267948966f;
        outGainA = std::cos(u * piOver2);
        outGainB = std::sin(u * piOver2);
        break;
    }
    case CrossfaderCurve::linear:
    {
        outGainA = 1.0f - u;
        outGainB = u;
        break;
    }
    case CrossfaderCurve::scratchCut:
    {
        // Sharp cut-in at edges for turntablism/scratching:
        // Cut-in lag is 5% at each edge.
        // Left edge (u in [0, 0.05]): Bus B cuts in from 0 to 1; Bus A is 1.0
        // Center (u in [0.05, 0.95]): Both Bus A and Bus B are 1.0
        // Right edge (u in [0.95, 1.0]): Bus A cuts out from 1 to 0; Bus B is 1.0
        constexpr float cutLag = 0.05f;
        outGainA = (u > (1.0f - cutLag)) ? std::clamp((1.0f - u) / cutLag, 0.0f, 1.0f) : 1.0f;
        outGainB = (u < cutLag) ? std::clamp(u / cutLag, 0.0f, 1.0f) : 1.0f;
        break;
    }
    }
}

float DjCrossfaderEngine::computeTrackGain(const std::uint64_t trackId, const float baseGain) const noexcept
{
    const auto assign = getTrackAssignment(trackId);
    if (assign == CrossfadeAssign::none)
        return baseGain;

    float gainA = 1.0f;
    float gainB = 1.0f;
    getBusGains(gainA, gainB);

    if (assign == CrossfadeAssign::busA)
        return baseGain * gainA;
    if (assign == CrossfadeAssign::busB)
        return baseGain * gainB;

    return baseGain;
}

double DjCrossfaderEngine::calculateTempoNudgeBpm(const double currentBpm, const float nudgePercent) noexcept
{
    const double factor = 1.0 + (static_cast<double>(nudgePercent) * 0.01);
    return std::clamp(currentBpm * factor, 20.0, 999.0);
}

} // namespace dawg::engine
