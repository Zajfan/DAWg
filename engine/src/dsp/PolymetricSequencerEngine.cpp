#include <dawg/engine/dsp/PolymetricSequencerEngine.h>

#include <cmath>

namespace dawg::engine::dsp {

namespace {

// Scale degree intervals from root
constexpr std::array<uint8_t, 7> kMajorIntervals = {0, 2, 4, 5, 7, 9, 11};
constexpr std::array<uint8_t, 7> kNaturalMinorIntervals = {0, 2, 3, 5, 7, 8, 10};
constexpr std::array<uint8_t, 7> kDorianIntervals = {0, 2, 3, 5, 7, 9, 10};
constexpr std::array<uint8_t, 7> kMixolydianIntervals = {0, 2, 4, 5, 7, 9, 10};
constexpr std::array<uint8_t, 5> kPentatonicMinor = {0, 3, 5, 7, 10};
constexpr std::array<uint8_t, 5> kPentatonicMajor = {0, 2, 4, 7, 9};
constexpr std::array<uint8_t, 6> kBluesIntervals = {0, 3, 5, 6, 7, 10};

} // namespace

PolymetricSequencerEngine::PolymetricSequencerEngine() noexcept
{
    reset();
}

void PolymetricSequencerEngine::reset() noexcept
{
    for (std::size_t t = 0; t < maxTracks; ++t)
    {
        m_tracks[t] = PolymetricTrack {};
        m_tracks[t].midiChannel = static_cast<std::uint8_t>(t + 1);
        m_tracks[t].stepCount = 16;
        m_tracks[t].division = StepClockDivision::div_1_16;
        m_tracks[t].currentStep = 0;
        m_tracks[t].subSampleAccumulator = 0.0;
    }
}

void PolymetricSequencerEngine::setTrackEnabled(std::size_t trackIdx, bool enabled) noexcept
{
    if (trackIdx < maxTracks)
        m_tracks[trackIdx].enabled = enabled;
}

void PolymetricSequencerEngine::setTrackStepCount(std::size_t trackIdx, std::uint8_t stepCount) noexcept
{
    if (trackIdx < maxTracks)
    {
        m_tracks[trackIdx].stepCount = std::clamp<std::uint8_t>(stepCount, 1, PolymetricTrack::maxSteps);
        if (m_tracks[trackIdx].currentStep >= m_tracks[trackIdx].stepCount)
            m_tracks[trackIdx].currentStep = 0;
    }
}

void PolymetricSequencerEngine::setTrackDivision(std::size_t trackIdx, StepClockDivision div) noexcept
{
    if (trackIdx < maxTracks)
        m_tracks[trackIdx].division = div;
}

void PolymetricSequencerEngine::setTrackMidiChannel(std::size_t trackIdx, std::uint8_t channel) noexcept
{
    if (trackIdx < maxTracks)
        m_tracks[trackIdx].midiChannel = std::clamp<std::uint8_t>(channel, 1, 16);
}

PolymetricTrack& PolymetricSequencerEngine::getTrack(std::size_t trackIdx) noexcept
{
    if (trackIdx < maxTracks)
        return m_tracks[trackIdx];
    return m_tracks[0];
}

const PolymetricTrack& PolymetricSequencerEngine::getTrack(std::size_t trackIdx) const noexcept
{
    if (trackIdx < maxTracks)
        return m_tracks[trackIdx];
    return m_tracks[0];
}

void PolymetricSequencerEngine::setStep(std::size_t trackIdx, std::size_t stepIdx, const SequencerStep& step) noexcept
{
    if (trackIdx < maxTracks && stepIdx < PolymetricTrack::maxSteps)
    {
        m_tracks[trackIdx].steps[stepIdx] = step;
    }
}

void PolymetricSequencerEngine::generateEuclidean(
    std::size_t trackIdx,
    std::size_t hits,
    std::size_t totalSteps,
    int rotation,
    std::uint8_t pitch,
    float velocity) noexcept
{
    if (trackIdx >= maxTracks)
        return;

    const std::size_t n = std::clamp<std::size_t>(totalSteps, 1, PolymetricTrack::maxSteps);
    const std::size_t k = std::min(hits, n);

    auto& trk = m_tracks[trackIdx];
    trk.stepCount = static_cast<std::uint8_t>(n);

    for (std::size_t i = 0; i < n; ++i)
    {
        // Canonical Euclidean distribution formula via Bresenham modular arithmetic
        const bool isHit = (k > 0) && (((i * k) % n) < k);

        // Apply rotation with positive modulo wrap
        int rotated = (static_cast<int>(i) + rotation) % static_cast<int>(n);
        if (rotated < 0) rotated += static_cast<int>(n);

        auto& st = trk.steps[static_cast<std::size_t>(rotated)];
        st.active = isHit;
        st.pitch = pitch;
        st.velocity = velocity;
        st.gateFraction = 0.75f;
        st.probability = 1.0f;
        st.ratchet = 1;
        st.microTimingOffset = 0.0f;
    }
}

bool PolymetricSequencerEngine::isScaleDegree(std::uint8_t semitoneFromRoot, MusicalScaleType scale) noexcept
{
    const uint8_t octMod = semitoneFromRoot % 12;
    switch (scale)
    {
    case MusicalScaleType::chromatic:
        return true;
    case MusicalScaleType::major:
        return std::find(kMajorIntervals.begin(), kMajorIntervals.end(), octMod) != kMajorIntervals.end();
    case MusicalScaleType::naturalMinor:
        return std::find(kNaturalMinorIntervals.begin(), kNaturalMinorIntervals.end(), octMod) != kNaturalMinorIntervals.end();
    case MusicalScaleType::dorian:
        return std::find(kDorianIntervals.begin(), kDorianIntervals.end(), octMod) != kDorianIntervals.end();
    case MusicalScaleType::mixolydian:
        return std::find(kMixolydianIntervals.begin(), kMixolydianIntervals.end(), octMod) != kMixolydianIntervals.end();
    case MusicalScaleType::pentatonicMinor:
        return std::find(kPentatonicMinor.begin(), kPentatonicMinor.end(), octMod) != kPentatonicMinor.end();
    case MusicalScaleType::pentatonicMajor:
        return std::find(kPentatonicMajor.begin(), kPentatonicMajor.end(), octMod) != kPentatonicMajor.end();
    case MusicalScaleType::blues:
        return std::find(kBluesIntervals.begin(), kBluesIntervals.end(), octMod) != kBluesIntervals.end();
    }
    return true;
}

void PolymetricSequencerEngine::generateMarkovProgression(
    std::size_t trackIdx,
    std::uint8_t rootNote,
    MusicalScaleType scale,
    float randomnessTemperature,
    std::uint32_t seed) noexcept
{
    if (trackIdx >= maxTracks)
        return;

    auto& trk = m_tracks[trackIdx];
    std::uint32_t prng = (seed == 0) ? 987654321u : seed;

    auto nextRand = [&prng]() -> float {
        prng = prng * 1664525u + 1013904223u;
        return static_cast<float>(prng >> 16) / 65536.0f;
    };

    // Transition matrix over scale degrees (Tonic, Supertonic, Mediant, Subdominant, Dominant, Submediant, Leading)
    // Degrees: 0 (I), 1 (ii), 2 (iii), 3 (IV), 4 (V), 5 (vi), 6 (vii)
    std::size_t currentDegree = 0; // Start on tonic

    for (std::size_t s = 0; s < trk.stepCount; ++s)
    {
        auto& step = trk.steps[s];
        if (!step.active)
            continue;

        // Choose next musical step using Markov tendencies
        const float r = nextRand();
        const float temp = std::clamp(randomnessTemperature, 0.0f, 1.0f);

        if (r < (1.0f - temp) * 0.4f)
        {
            // Strong resolution: return to Tonic (I) or Dominant (V)
            currentDegree = (r < 0.2f) ? 0 : 4;
        }
        else if (r < (1.0f - temp) * 0.8f)
        {
            // Stepwise motion: move by +1 or -1 scale degree
            currentDegree = (currentDegree + (nextRand() > 0.5f ? 1 : 6)) % 7;
        }
        else
        {
            // Chromatic / random scale leap
            currentDegree = static_cast<std::size_t>(nextRand() * 7.0f) % 7;
        }

        // Map degree to interval
        std::uint8_t interval = 0;
        if (scale == MusicalScaleType::major || scale == MusicalScaleType::pentatonicMajor)
            interval = kMajorIntervals[currentDegree % kMajorIntervals.size()];
        else if (scale == MusicalScaleType::pentatonicMinor)
            interval = kPentatonicMinor[currentDegree % kPentatonicMinor.size()];
        else if (scale == MusicalScaleType::blues)
            interval = kBluesIntervals[currentDegree % kBluesIntervals.size()];
        else
            interval = kNaturalMinorIntervals[currentDegree % kNaturalMinorIntervals.size()];

        // Octave range (Middle octave: 0 or +12)
        const std::uint8_t octave = (nextRand() > 0.75f) ? 12 : 0;
        step.pitch = std::clamp<std::uint8_t>(rootNote + interval + octave, 0, 127);
    }
}

double PolymetricSequencerEngine::getStepSamples(StepClockDivision div, double bpm, double sampleRate) const noexcept
{
    const double beatSamples = (60.0 / std::max(20.0, bpm)) * sampleRate;
    switch (div)
    {
    case StepClockDivision::div_1_4:
        return beatSamples;
    case StepClockDivision::div_1_8:
        return beatSamples * 0.5;
    case StepClockDivision::div_1_8_triplet:
        return beatSamples * (1.0 / 3.0);
    case StepClockDivision::div_1_16:
        return beatSamples * 0.25;
    case StepClockDivision::div_1_32:
        return beatSamples * 0.125;
    }
    return beatSamples * 0.25;
}

std::size_t PolymetricSequencerEngine::processBlock(
    double bpm,
    double sampleRate,
    std::size_t frameCount,
    std::span<MidiEvent> outEvents) noexcept
{
    if (frameCount == 0 || outEvents.empty())
        return 0;

    std::size_t eventCount = 0;

    auto nextPrng = [this]() -> float {
        m_prngState = m_prngState * 1664525u + 1013904223u;
        return (static_cast<float>(m_prngState >> 16) / 65536.0f) * 2.0f - 1.0f; // -1 to +1
    };

    for (std::size_t t = 0; t < maxTracks; ++t)
    {
        auto& trk = m_tracks[t];
        if (!trk.enabled || trk.stepCount == 0)
            continue;

        const double baseStepSamples = getStepSamples(trk.division, bpm, sampleRate);
        trk.subSampleAccumulator += static_cast<double>(frameCount);

        while (trk.subSampleAccumulator >= baseStepSamples)
        {
            trk.subSampleAccumulator -= baseStepSamples;

            const std::size_t sIdx = trk.currentStep;
            trk.currentStep = (trk.currentStep + 1) % trk.stepCount;

            const auto& step = trk.steps[sIdx];
            if (!step.active)
                continue;

            // Probability check
            if (step.probability < 0.999f)
            {
                const float probCheck = (nextPrng() + 1.0f) * 0.5f;
                if (probCheck > step.probability)
                    continue;
            }

            // Swing calculation: delay odd steps
            double swingOffset = 0.0;
            if ((sIdx % 2) == 1 && m_swingPercent > 50.0f)
            {
                swingOffset = (m_swingPercent - 50.0f) * 0.02 * (baseStepSamples * 0.5);
            }

            // Humanize micro-timing jitter
            const double humanizeJitter = m_humanizeTiming * nextPrng() * (baseStepSamples * 0.1);
            const double totalOffset = swingOffset + humanizeJitter + (step.microTimingOffset * baseStepSamples);

            const std::uint32_t noteOnFrame = static_cast<std::uint32_t>(
                std::clamp<double>(totalOffset, 0.0, static_cast<double>(frameCount - 1)));

            // Velocity humanization
            float finalVel = step.velocity;
            if (m_humanizeVelocity > 0.001f)
            {
                finalVel = std::clamp(finalVel + m_humanizeVelocity * nextPrng() * 0.15f, 0.05f, 1.0f);
            }
            const auto velByte = static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(finalVel * 127.0f), 1, 127));

            // Ratchet count (1 to 4 triggers)
            const std::uint8_t ratchets = std::clamp<std::uint8_t>(step.ratchet, 1, 4);
            const double ratchetSubPeriod = (baseStepSamples * step.gateFraction) / ratchets;

            for (std::uint8_t r = 0; r < ratchets && eventCount + 2 <= outEvents.size(); ++r)
            {
                const std::uint32_t rOnFrame = static_cast<std::uint32_t>(
                    std::clamp<double>(noteOnFrame + r * ratchetSubPeriod, 0.0, static_cast<double>(frameCount - 1)));

                // Note-On
                auto& onEv = outEvents[eventCount++];
                onEv.frameOffset = rOnFrame;
                onEv.size = 3;
                onEv.data[0] = static_cast<std::byte>(0x90 | ((trk.midiChannel - 1) & 0x0F));
                onEv.data[1] = static_cast<std::byte>(step.pitch);
                onEv.data[2] = static_cast<std::byte>(velByte);

                // Note-Off
                const std::uint32_t rOffFrame = static_cast<std::uint32_t>(
                    std::clamp<double>(rOnFrame + ratchetSubPeriod * 0.8, 0.0, static_cast<double>(frameCount - 1)));

                auto& offEv = outEvents[eventCount++];
                offEv.frameOffset = rOffFrame;
                offEv.size = 3;
                offEv.data[0] = static_cast<std::byte>(0x80 | ((trk.midiChannel - 1) & 0x0F));
                offEv.data[1] = static_cast<std::byte>(step.pitch);
                offEv.data[2] = static_cast<std::byte>(64);
            }
        }
    }

    return eventCount;
}

} // namespace dawg::engine::dsp
