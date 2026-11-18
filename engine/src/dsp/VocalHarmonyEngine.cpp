#include <dawg/engine/dsp/VocalHarmonyEngine.h>

#include <algorithm>
#include <cmath>
#include <functional>

namespace dawg::engine::dsp {

namespace {

constexpr double kTwoPi = 6.283185307179586;

std::array<int, 7> scaleIntervals(const ChordScale scale) noexcept
{
    switch (scale)
    {
    case ChordScale::major:         return {0, 2, 4, 5, 7, 9, 11};
    case ChordScale::naturalMinor:  return {0, 2, 3, 5, 7, 8, 10};
    case ChordScale::dorian:        return {0, 2, 3, 5, 7, 9, 10};
    case ChordScale::phrygian:      return {0, 1, 3, 5, 7, 8, 10};
    case ChordScale::lydian:        return {0, 2, 4, 6, 7, 9, 11};
    case ChordScale::mixolydian:    return {0, 2, 4, 5, 7, 9, 10};
    case ChordScale::harmonicMinor: return {0, 2, 3, 5, 7, 8, 11};
    }
    return {0, 2, 4, 5, 7, 9, 11};
}

int floorDiv(const int a, const int b) noexcept
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

int floorMod(const int a, const int b) noexcept
{
    return a - floorDiv(a, b) * b;
}

double midiFromHz(const double hz) noexcept { return 69.0 + 12.0 * std::log2(hz / 440.0); }

float sampleAt(const float* x, const std::size_t n, const double pos) noexcept
{
    if (pos < 0.0)
        return 0.0f;
    const auto i0 = static_cast<std::size_t>(pos);
    if (i0 + 1 >= n)
        return i0 < n ? x[i0] : 0.0f;
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));
    return x[i0] + (x[i0 + 1] - x[i0]) * frac;
}

struct PitchMark
{
    std::size_t position {0};
    double period {0.0};
    double f0 {0.0}; // 0 = unvoiced
};

std::vector<PitchMark> analysePitchMarks(const float* x, const std::size_t n, const double sr,
                                         const std::vector<float>& track, const std::size_t hop)
{
    std::vector<PitchMark> marks;
    if (n == 0)
        return marks;

    const double unvoicedPeriod = 0.01 * sr;
    double t = 0.0;
    while (t < static_cast<double>(n))
    {
        const auto pos = static_cast<std::size_t>(t);
        const std::size_t frame = std::min(track.empty() ? 0 : track.size() - 1, pos / hop);
        const double f0 = track.empty() ? 0.0 : static_cast<double>(track[frame]);
        const double period = f0 > 0.0 ? sr / f0 : unvoicedPeriod;

        std::size_t markPos = pos;
        if (f0 > 0.0)
        {
            // Align the mark to the strongest peak within +/- a quarter period (glottal pulse).
            const auto quarter = static_cast<std::size_t>(period * 0.25);
            const std::size_t lo = pos > quarter ? pos - quarter : 0;
            const std::size_t hi = std::min(n - 1, pos + quarter);
            float best = -1.0f;
            for (std::size_t i = lo; i <= hi; ++i)
            {
                if (std::abs(x[i]) > best)
                {
                    best = std::abs(x[i]);
                    markPos = i;
                }
            }
            if (! marks.empty() && static_cast<double>(markPos) < static_cast<double>(marks.back().position) + 0.5 * period)
                markPos = pos;
        }

        marks.push_back({markPos, period, f0});
        t = static_cast<double>(markPos) + period;
    }
    return marks;
}

// TD-PSOLA core. ratioFn(f0) returns the pitch ratio for a voiced period.
std::vector<float> psola(const float* x, const std::size_t n, const double sr, const float formantFactor,
                         const std::function<double(double)>& ratioFn)
{
    std::vector<float> out(n, 0.0f);
    if (x == nullptr || n < 64 || sr <= 0.0)
        return out;

    constexpr std::size_t hop = 512;
    const auto track = VocalHarmonyEngine::detectPitchTrack(x, n, sr, hop);
    const auto marks = analysePitchMarks(x, n, sr, track, hop);
    if (marks.empty())
        return out;

    const double F = std::clamp(static_cast<double>(formantFactor), 0.25, 4.0);
    double ts = static_cast<double>(marks.front().position);
    std::size_t k = 0;

    while (ts < static_cast<double>(n))
    {
        while (k + 1 < marks.size()
               && std::abs(static_cast<double>(marks[k + 1].position) - ts) <= std::abs(static_cast<double>(marks[k].position) - ts))
            ++k;

        const auto& mark = marks[k];
        const double ratio = mark.f0 > 0.0 ? std::clamp(ratioFn(mark.f0), 0.25, 4.0) : 1.0;
        const double synthHop = std::max(1.0, mark.period / ratio);
        const double halfLen = std::max(2.0, mark.period / F);
        const double grainWeight = std::min(1.0, synthHop / halfLen);

        const auto L = static_cast<long>(halfLen);
        const auto centre = static_cast<long>(ts);
        for (long j = -L; j <= L; ++j)
        {
            const long o = centre + j;
            if (o < 0 || o >= static_cast<long>(n))
                continue;
            const double w = 0.5 * (1.0 + std::cos(kTwoPi * 0.5 * static_cast<double>(j) / halfLen));
            const float s = sampleAt(x, n, static_cast<double>(mark.position) + static_cast<double>(j) * F);
            out[static_cast<std::size_t>(o)] += static_cast<float>(w * grainWeight) * s;
        }
        ts += synthHop;
    }
    return out;
}

} // namespace

double VocalHarmonyEngine::detectFrameF0(const float* x, const std::size_t n, const double sr) noexcept
{
    if (x == nullptr || sr <= 0.0)
        return 0.0;

    const auto tauMin = static_cast<std::size_t>(sr / maxF0Hz);
    const auto tauMax = static_cast<std::size_t>(sr / minF0Hz);
    if (n < tauMax * 2)
        return 0.0;
    const std::size_t window = n - tauMax;

    double energy = 0.0;
    for (std::size_t i = 0; i < window; ++i)
        energy += static_cast<double>(x[i]) * x[i];
    if (energy / static_cast<double>(window) < 1e-6)
        return 0.0;

    // YIN cumulative-mean-normalised difference function
    double runningSum = 0.0;
    std::size_t bestTau = 0;
    std::vector<double> cmnd(tauMax + 1, 1.0);
    for (std::size_t tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < window; ++i)
        {
            const double diff = static_cast<double>(x[i]) - x[i + tau];
            d += diff * diff;
        }
        runningSum += d;
        cmnd[tau] = runningSum > 0.0 ? d * static_cast<double>(tau) / runningSum : 1.0;
    }

    for (std::size_t tau = std::max<std::size_t>(tauMin, 2); tau < tauMax; ++tau)
    {
        if (cmnd[tau] < 0.15 && cmnd[tau] <= cmnd[tau + 1])
        {
            bestTau = tau;
            break;
        }
    }
    if (bestTau == 0)
        return 0.0;

    const double a = cmnd[bestTau - 1], b = cmnd[bestTau], c = cmnd[bestTau + 1];
    const double denom = a - 2.0 * b + c;
    const double offset = std::abs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
    return sr / (static_cast<double>(bestTau) + std::clamp(offset, -0.5, 0.5));
}

std::vector<float> VocalHarmonyEngine::detectPitchTrack(const float* x, const std::size_t n, const double sr,
                                                        const std::size_t hopSize)
{
    std::vector<float> track;
    if (x == nullptr || n == 0 || sr <= 0.0 || hopSize == 0)
        return track;

    const auto frame = static_cast<std::size_t>(2.0 * sr / minF0Hz) + 2;
    for (std::size_t start = 0; start < n; start += hopSize)
    {
        const std::size_t len = std::min(frame, n - start);
        track.push_back(static_cast<float>(detectFrameF0(x + start, len, sr)));
    }

    // Fill short unvoiced gaps at the tail (frames shorter than the analysis window)
    for (std::size_t i = 1; i < track.size(); ++i)
        if (track[i] <= 0.0f && track[i - 1] > 0.0f && i * hopSize + frame > n)
            track[i] = track[i - 1];
    return track;
}

int VocalHarmonyEngine::snapToScale(const int midiNote, const std::uint8_t rootKey, const ChordScale scale) noexcept
{
    const auto intervals = scaleIntervals(scale);
    const int pc = floorMod(midiNote - static_cast<int>(rootKey % 12), 12);
    int bestDelta = 12;
    for (const int iv : intervals)
    {
        for (const int candidate : {iv - 12, iv, iv + 12})
        {
            const int delta = candidate - pc;
            if (std::abs(delta) < std::abs(bestDelta) || (std::abs(delta) == std::abs(bestDelta) && delta < bestDelta))
                bestDelta = delta;
        }
    }
    return midiNote + bestDelta;
}

int VocalHarmonyEngine::shiftByScaleDegrees(const int midiNote, const std::uint8_t rootKey, const ChordScale scale,
                                            const int degrees) noexcept
{
    const auto intervals = scaleIntervals(scale);
    const int root = static_cast<int>(rootKey % 12);
    const int snapped = snapToScale(midiNote, rootKey, scale);
    const int pc = floorMod(snapped - root, 12);

    int idx = 0;
    for (int i = 0; i < 7; ++i)
        if (intervals[static_cast<std::size_t>(i)] == pc)
            idx = i;

    const int octaveBase = floorDiv(snapped - root - pc, 12);
    const int newIdx = idx + degrees;
    const int octaveOffset = floorDiv(newIdx, 7);
    const int wrapped = floorMod(newIdx, 7);
    return root + 12 * (octaveBase + octaveOffset) + intervals[static_cast<std::size_t>(wrapped)];
}

std::vector<float> VocalHarmonyEngine::pitchShift(const float* x, const std::size_t n, const double sr,
                                                  const float semitones, const float formantSemitones)
{
    const double ratio = std::pow(2.0, static_cast<double>(semitones) / 12.0);
    const auto F = static_cast<float>(std::pow(2.0, static_cast<double>(formantSemitones) / 12.0));
    return psola(x, n, sr, F, [ratio](double) { return ratio; });
}

std::vector<float> VocalHarmonyEngine::shiftFormants(const float* x, const std::size_t n, const double sr,
                                                     const float formantSemitones)
{
    return pitchShift(x, n, sr, 0.0f, formantSemitones);
}

VocalHarmonyResult VocalHarmonyEngine::renderHarmonies(const float* x, const std::size_t n, const double sr,
                                                       const std::uint8_t rootKey, const ChordScale scale,
                                                       const std::span<const VocalHarmonyVoice> voices, const float dryGain)
{
    VocalHarmonyResult result;
    result.left.assign(n, 0.0f);
    result.right.assign(n, 0.0f);
    if (x == nullptr || n == 0)
        return result;

    for (std::size_t i = 0; i < n; ++i)
    {
        result.left[i] = x[i] * dryGain * 0.7071f;
        result.right[i] = x[i] * dryGain * 0.7071f;
    }

    for (const auto& voice : voices)
    {
        if (! voice.enabled || voice.gain <= 0.0f)
            continue;

        auto ratioFn = [&voice, rootKey, scale](const double f0) {
            const double sung = midiFromHz(f0);
            const int target = voice.manualMidiNote >= 0
                ? voice.manualMidiNote
                : shiftByScaleDegrees(static_cast<int>(std::lround(sung)), rootKey, scale, voice.scaleDegreeOffset);
            return std::pow(2.0, (static_cast<double>(target) - sung) / 12.0);
        };

        const auto F = static_cast<float>(std::pow(2.0, static_cast<double>(voice.formantSemitones) / 12.0));
        const auto rendered = psola(x, n, sr, F, ratioFn);

        constexpr float piOver4 = 0.78539816f;
        const float angle = (std::clamp(voice.pan, -1.0f, 1.0f) + 1.0f) * piOver4;
        const float gl = std::cos(angle) * voice.gain;
        const float gr = std::sin(angle) * voice.gain;
        for (std::size_t i = 0; i < n; ++i)
        {
            result.left[i] += rendered[i] * gl;
            result.right[i] += rendered[i] * gr;
        }
    }
    return result;
}

std::array<VocalHarmonyVoice, 4> VocalHarmonyEngine::defaultSatbVoices() noexcept
{
    return {{
        {true, 4, -1, 0.55f, 0.35f, 0.0f},   // Soprano: diatonic 5th above
        {true, 2, -1, 0.60f, -0.35f, 0.0f},  // Alto: diatonic 3rd above
        {true, -3, -1, 0.55f, 0.6f, -1.0f},  // Tenor: diatonic 4th below, slightly darker
        {true, -7, -1, 0.50f, -0.6f, -2.0f}  // Bass: octave below, darker formants
    }};
}

// ---------------------------------------------------------------------------
// VocalDoublerNode
// ---------------------------------------------------------------------------

bool VocalDoublerNode::prepare(const double sampleRate, const std::size_t /*maxBlockSize*/,
                               const std::uint16_t /*channelCount*/)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    reset();
    return true;
}

void VocalDoublerNode::reset() noexcept
{
    m_buffer.fill(0.0f);
    m_writePos = 0;
    m_lfoPhase = {0.0, 1.5707963267948966};
    m_jitterTarget = {};
    m_jitterValue = {};
    m_jitterCountdown = 0;
}

void VocalDoublerNode::setHaasDelayMs(const float ms) noexcept { m_haasMs = std::clamp(ms, 5.0f, 40.0f); }
void VocalDoublerNode::setDetuneCents(const float cents) noexcept { m_detuneCents = std::clamp(cents, 0.0f, 25.0f); }
void VocalDoublerNode::setDetuneRateHz(const float hz) noexcept { m_detuneRateHz = std::clamp(hz, 0.2f, 2.0f); }
void VocalDoublerNode::setJitterMs(const float ms) noexcept { m_jitterMs = std::clamp(ms, 0.0f, 10.0f); }
void VocalDoublerNode::setWidth(const float width) noexcept { m_width = std::clamp(width, 0.0f, 1.0f); }
void VocalDoublerNode::setDoubleLevel(const float level) noexcept { m_doubleLevel = std::clamp(level, 0.0f, 1.0f); }
void VocalDoublerNode::setDryLevel(const float level) noexcept { m_dryLevel = std::clamp(level, 0.0f, 1.0f); }

float VocalDoublerNode::modulationAmplitudeSec() const noexcept
{
    // A delay modulated as A*sin(2*pi*f*t) changes playback rate by up to 2*pi*f*A.
    const double rateDeviation = std::pow(2.0, static_cast<double>(m_detuneCents) / 1200.0) - 1.0;
    const double amp = rateDeviation / (kTwoPi * static_cast<double>(m_detuneRateHz));
    return static_cast<float>(std::min(amp, 0.9 * static_cast<double>(m_haasMs) * 1e-3));
}

float VocalDoublerNode::nextNoise() noexcept
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 7;
    m_rng ^= m_rng << 17;
    return static_cast<float>(static_cast<double>(m_rng >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f;
}

void VocalDoublerNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0 || audio.channels[0] == nullptr)
        return;

    const bool stereo = audio.channelCount >= 2 && audio.channels[1] != nullptr;
    const auto fs = static_cast<float>(m_sampleRate);
    const float modAmpSamples = modulationAmplitudeSec() * fs;
    const std::array<float, 2> baseDelay {m_haasMs * 1e-3f * fs, m_haasMs * 1.37e-3f * fs};
    const float jitterCoeff = 1.0f / (0.08f * fs); // ~80 ms glide between jitter targets
    const auto jitterPeriod = static_cast<std::uint32_t>(0.12f * fs);
    const double phaseInc = kTwoPi * static_cast<double>(m_detuneRateHz) / m_sampleRate;
    const float sideA = 0.5f + 0.5f * m_width;
    const float sideB = 0.5f - 0.5f * m_width;
    constexpr std::size_t mask = delayBufferSize - 1;

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        const float mono = stereo ? 0.5f * (audio.channels[0][i] + audio.channels[1][i]) : audio.channels[0][i];
        m_buffer[m_writePos] = mono;

        if (m_jitterCountdown == 0)
        {
            m_jitterTarget[0] = nextNoise() * m_jitterMs * 1e-3f * fs;
            m_jitterTarget[1] = nextNoise() * m_jitterMs * 1e-3f * fs;
            m_jitterCountdown = jitterPeriod;
        }
        --m_jitterCountdown;

        std::array<float, 2> voices {};
        for (std::size_t v = 0; v < 2; ++v)
        {
            m_jitterValue[v] += (m_jitterTarget[v] - m_jitterValue[v]) * jitterCoeff;
            m_lfoPhase[v] += phaseInc;
            if (m_lfoPhase[v] > kTwoPi)
                m_lfoPhase[v] -= kTwoPi;
            // Voices modulate in opposite directions for a wider, less phasey double.
            const float sign = v == 0 ? 1.0f : -1.0f;
            const float d = std::clamp(baseDelay[v] + m_jitterValue[v]
                                           + sign * modAmpSamples * static_cast<float>(std::sin(m_lfoPhase[v])),
                                       1.0f, static_cast<float>(delayBufferSize - 2));
            const auto whole = static_cast<std::size_t>(d);
            const float frac = d - static_cast<float>(whole);
            const float a = m_buffer[(m_writePos + delayBufferSize - whole) & mask];
            const float b = m_buffer[(m_writePos + delayBufferSize - whole - 1) & mask];
            voices[v] = a + (b - a) * frac;
        }
        m_writePos = (m_writePos + 1) & mask;

        const float left = m_dryLevel * mono + m_doubleLevel * (voices[0] * sideA + voices[1] * sideB);
        const float right = m_dryLevel * mono + m_doubleLevel * (voices[1] * sideA + voices[0] * sideB);
        if (stereo)
        {
            audio.channels[0][i] = left;
            audio.channels[1][i] = right;
        }
        else
        {
            audio.channels[0][i] = 0.5f * (left + right);
        }
    }
}

} // namespace dawg::engine::dsp
