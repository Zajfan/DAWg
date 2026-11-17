#include <dawg/engine/dsp/ArpeggiatorEngine.h>

#include <algorithm>
#include <random>

namespace dawg::engine::dsp {

std::vector<ArpStep> ArpeggiatorEngine::generateSequence(
    const std::vector<std::uint8_t>& heldNotes,
    const std::size_t numSteps,
    const double stepDivisionBeats) const
{
    if (heldNotes.empty() || numSteps == 0 || stepDivisionBeats <= 0.0)
        return {};

    // Build octave-expanded note pool
    std::vector<std::uint8_t> baseSorted = heldNotes;
    std::sort(baseSorted.begin(), baseSorted.end());

    std::vector<std::uint8_t> expandedNotes;
    expandedNotes.reserve(baseSorted.size() * static_cast<std::size_t>(m_octaveRange));

    for (int oct = 0; oct < m_octaveRange; ++oct)
    {
        for (const auto note : baseSorted)
        {
            const int p = static_cast<int>(note) + oct * 12;
            if (p <= 127)
                expandedNotes.push_back(static_cast<std::uint8_t>(p));
        }
    }

    if (expandedNotes.empty())
        return {};

    // Build pitch sequence based on pattern
    std::vector<std::uint8_t> sequenceOrder;
    switch (m_pattern)
    {
    case ArpPattern::up:
        sequenceOrder = expandedNotes;
        break;

    case ArpPattern::down:
        sequenceOrder = expandedNotes;
        std::reverse(sequenceOrder.begin(), sequenceOrder.end());
        break;

    case ArpPattern::upDown:
        sequenceOrder = expandedNotes;
        if (expandedNotes.size() > 2)
        {
            for (std::size_t i = expandedNotes.size() - 2; i > 0; --i)
                sequenceOrder.push_back(expandedNotes[i]);
        }
        break;

    case ArpPattern::pingPong:
        sequenceOrder = expandedNotes;
        if (expandedNotes.size() > 1)
        {
            for (std::size_t i = expandedNotes.size() - 1; i > 0; --i)
                sequenceOrder.push_back(expandedNotes[i]);
        }
        break;

    case ArpPattern::converge:
    {
        // Outside-in pattern: lowest, highest, second lowest, second highest...
        std::size_t low = 0;
        std::size_t high = expandedNotes.size() - 1;
        while (low <= high)
        {
            sequenceOrder.push_back(expandedNotes[low++]);
            if (low <= high)
                sequenceOrder.push_back(expandedNotes[high--]);
        }
        break;
    }

    case ArpPattern::asPlayed:
        for (int oct = 0; oct < m_octaveRange; ++oct)
        {
            for (const auto note : heldNotes)
            {
                const int p = static_cast<int>(note) + oct * 12;
                if (p <= 127)
                    sequenceOrder.push_back(static_cast<std::uint8_t>(p));
            }
        }
        break;

    case ArpPattern::random:
        sequenceOrder = expandedNotes;
        // Deterministic shuffle for predictable testability
        for (std::size_t i = sequenceOrder.size() - 1; i > 0; --i)
        {
            const std::size_t j = (i * 3 + 1) % (i + 1);
            std::swap(sequenceOrder[i], sequenceOrder[j]);
        }
        break;

    case ArpPattern::chord:
        // Handled specially below: all notes trigger simultaneously on each step
        break;
    }

    std::vector<ArpStep> steps;
    steps.reserve(numSteps * (m_pattern == ArpPattern::chord ? expandedNotes.size() : 1));

    const double dur = stepDivisionBeats * static_cast<double>(m_gateLength);

    for (std::size_t s = 0; s < numSteps; ++s)
    {
        // Swing calculation: delay every odd 16th/step
        double swingOffset = 0.0;
        if ((s % 2) == 1)
        {
            swingOffset = static_cast<double>(m_swing) * stepDivisionBeats;
        }

        const double beatPos = static_cast<double>(s) * stepDivisionBeats + swingOffset;

        if (m_pattern == ArpPattern::chord)
        {
            for (const auto pitch : expandedNotes)
            {
                steps.push_back(ArpStep {
                    .pitch = pitch,
                    .beatOffset = beatPos,
                    .durationBeats = dur,
                    .velocity = 100,
                    .ratchetCount = static_cast<std::uint8_t>(m_ratchets)
                });
            }
        }
        else
        {
            const auto pitch = sequenceOrder[s % sequenceOrder.size()];
            steps.push_back(ArpStep {
                .pitch = pitch,
                .beatOffset = beatPos,
                .durationBeats = dur,
                .velocity = (s % 4 == 0) ? static_cast<std::uint8_t>(115) : static_cast<std::uint8_t>(95),
                .ratchetCount = static_cast<std::uint8_t>(m_ratchets)
            });
        }
    }

    return steps;
}

std::vector<bool> EuclideanRhythmEngine::generate(int steps, int pulses, int rotation)
{
    if (steps <= 0)
        return {};

    pulses = std::clamp(pulses, 0, steps);
    if (pulses == 0)
        return std::vector<bool>(static_cast<std::size_t>(steps), false);
    if (pulses == steps)
        return std::vector<bool>(static_cast<std::size_t>(steps), true);

    // Bjorklund algorithm
    std::vector<std::vector<bool>> pattern(static_cast<std::size_t>(steps));
    for (int i = 0; i < steps; ++i)
    {
        pattern[static_cast<std::size_t>(i)].push_back(i < pulses);
    }

    int divisor = steps - pulses;
    int count = pulses;

    while (divisor > 1)
    {
        const int iterations = std::min(count, divisor);
        for (int i = 0; i < iterations; ++i)
        {
            const auto sourceIdx = static_cast<std::size_t>(pattern.size() - 1 - static_cast<std::size_t>(i));
            const auto targetIdx = static_cast<std::size_t>(i);
            pattern[targetIdx].insert(pattern[targetIdx].end(), pattern[sourceIdx].begin(), pattern[sourceIdx].end());
        }
        pattern.erase(pattern.end() - iterations, pattern.end());

        const int newDivisor = count;
        count = divisor - count;
        divisor = newDivisor;
        if (count <= 0) break;
    }

    std::vector<bool> result;
    result.reserve(static_cast<std::size_t>(steps));
    for (const auto& group : pattern)
    {
        result.insert(result.end(), group.begin(), group.end());
    }

    // Apply rotation
    if (rotation != 0 && ! result.empty())
    {
        const int normRot = ((rotation % steps) + steps) % steps;
        std::rotate(result.begin(), result.begin() + normRot, result.end());
    }

    return result;
}

} // namespace dawg::engine::dsp
