#include <dawg/engine/dsp/SpatialObjectPannerNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kHeadRadiusM = 0.0875f;
constexpr float kSpeedOfSound = 343.0f;

constexpr std::array<SpatialSpeakerDef, 2> kStereo {{
    {"L", -30.0f, 0.0f, false}, {"R", 30.0f, 0.0f, false}
}};

constexpr std::array<SpatialSpeakerDef, 6> kSurround51 {{
    {"L", -30.0f, 0.0f, false}, {"R", 30.0f, 0.0f, false}, {"C", 0.0f, 0.0f, false},
    {"LFE", 0.0f, 0.0f, true}, {"Ls", -110.0f, 0.0f, false}, {"Rs", 110.0f, 0.0f, false}
}};

constexpr std::array<SpatialSpeakerDef, 10> kAtmos712 {{
    {"L", -30.0f, 0.0f, false}, {"R", 30.0f, 0.0f, false}, {"C", 0.0f, 0.0f, false},
    {"LFE", 0.0f, 0.0f, true}, {"Lss", -90.0f, 0.0f, false}, {"Rss", 90.0f, 0.0f, false},
    {"Lrs", -150.0f, 0.0f, false}, {"Rrs", 150.0f, 0.0f, false},
    {"Ltm", -90.0f, 60.0f, false}, {"Rtm", 90.0f, 60.0f, false}
}};

constexpr std::array<SpatialSpeakerDef, 12> kAtmos714 {{
    {"L", -30.0f, 0.0f, false}, {"R", 30.0f, 0.0f, false}, {"C", 0.0f, 0.0f, false},
    {"LFE", 0.0f, 0.0f, true}, {"Lss", -90.0f, 0.0f, false}, {"Rss", 90.0f, 0.0f, false},
    {"Lrs", -150.0f, 0.0f, false}, {"Rrs", 150.0f, 0.0f, false},
    {"Ltf", -45.0f, 45.0f, false}, {"Rtf", 45.0f, 45.0f, false},
    {"Ltr", -135.0f, 45.0f, false}, {"Rtr", 135.0f, 45.0f, false}
}};

struct Vec3
{
    float x {0.0f}; // right
    float y {0.0f}; // front
    float z {0.0f}; // up
};

Vec3 toUnitVector(const float azimuthDeg, const float elevationDeg) noexcept
{
    const float az = azimuthDeg * kDegToRad;
    const float el = elevationDeg * kDegToRad;
    return { std::sin(az) * std::cos(el), std::cos(az) * std::cos(el), std::sin(el) };
}

float wrapAzimuth(float deg) noexcept
{
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f)
        deg += 360.0f;
    return deg - 180.0f;
}

} // namespace

SpatialObjectPannerNode::SpatialObjectPannerNode() noexcept
{
    computeGains(m_format, m_position, m_targetGains);
    m_currentGains = m_targetGains;
}

std::span<const SpatialSpeakerDef> SpatialObjectPannerNode::speakerLayout(const SpatialBusFormat format) noexcept
{
    switch (format)
    {
    case SpatialBusFormat::stereo20: return kStereo;
    case SpatialBusFormat::surround51: return kSurround51;
    case SpatialBusFormat::atmos712: return kAtmos712;
    case SpatialBusFormat::atmos714: return kAtmos714;
    }
    return kStereo;
}

std::size_t SpatialObjectPannerNode::speakerCount(const SpatialBusFormat format) noexcept
{
    return speakerLayout(format).size();
}

void SpatialObjectPannerNode::computeGains(const SpatialBusFormat format, const SphericalPosition& position,
                                           std::array<float, maxSpeakers>& outGains) noexcept
{
    outGains.fill(0.0f);
    const auto layout = speakerLayout(format);
    const Vec3 src = toUnitVector(position.azimuthDeg, position.elevationDeg);

    // Lobe sharpness: point sources focus on the nearest speakers, large sources diffuse.
    const float size = std::clamp(position.size, 0.0f, 1.0f);
    const float sharpness = 0.5f + 11.5f * (1.0f - size) * (1.0f - size);

    float energy = 0.0f;
    for (std::size_t i = 0; i < layout.size(); ++i)
    {
        if (layout[i].isLfe)
            continue;
        const Vec3 spk = toUnitVector(layout[i].azimuthDeg, layout[i].elevationDeg);
        const float dot = src.x * spk.x + src.y * spk.y + src.z * spk.z;
        const float w = std::pow(std::clamp((dot + 1.0f) * 0.5f, 0.0f, 1.0f), sharpness);
        outGains[i] = w;
        energy += w * w;
    }

    if (energy <= 1e-12f)
        return;

    const float norm = 1.0f / std::sqrt(energy);
    for (std::size_t i = 0; i < layout.size(); ++i)
        outGains[i] *= norm;
}

float SpatialObjectPannerNode::distanceGain(const float distanceM) noexcept
{
    const float d = std::clamp(distanceM, 0.1f, 50.0f);
    // Inverse distance law referenced to 1 m, near-field boost limited to +6 dB.
    return std::min(2.0f, 1.0f / d);
}

SphericalPosition SpatialObjectPannerNode::evaluateTrajectory(const SpatialTrajectory& trajectory,
                                                              const SphericalPosition& base,
                                                              const double timeSec) noexcept
{
    if (trajectory.shape == SpatialTrajectoryShape::none || trajectory.periodSec <= 0.0)
        return base;

    double cycles = timeSec / trajectory.periodSec;
    const auto phase = static_cast<float>(cycles - std::floor(cycles));
    SphericalPosition p = base;
    const float radius = std::clamp(trajectory.radiusM, 0.1f, 50.0f);

    switch (trajectory.shape)
    {
    case SpatialTrajectoryShape::none:
        break;

    case SpatialTrajectoryShape::orbit:
        p.azimuthDeg = wrapAzimuth(base.azimuthDeg + 360.0f * phase);
        p.elevationDeg = trajectory.elevationDeg;
        p.distanceM = radius;
        break;

    case SpatialTrajectoryShape::figure8:
    {
        // Figure-8 with its crossing point straight ahead of the listener at distance = radius.
        const float ang = 2.0f * kPi * phase;
        const float x = 1.5f * radius * std::sin(ang);
        const float y = radius + 0.75f * radius * std::sin(2.0f * ang);
        p.azimuthDeg = std::atan2(x, y) / kDegToRad;
        p.elevationDeg = trajectory.elevationDeg;
        p.distanceM = std::clamp(std::sqrt(x * x + y * y), 0.1f, 50.0f);
        break;
    }

    case SpatialTrajectoryShape::spiral:
        p.azimuthDeg = wrapAzimuth(base.azimuthDeg + 360.0f * trajectory.spiralTurns * phase);
        p.elevationDeg = trajectory.elevationDeg
            + (trajectory.spiralEndElevationDeg - trajectory.elevationDeg) * phase;
        p.distanceM = radius;
        break;
    }

    p.elevationDeg = std::clamp(p.elevationDeg, -90.0f, 90.0f);
    return p;
}

bool SpatialObjectPannerNode::prepare(const double sampleRate, const std::size_t maxBlockSize,
                                      const std::uint16_t /*channelCount*/)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0)
        return false;

    m_sampleRate = sampleRate;
    m_monoScratch.assign(maxBlockSize, 0.0f);
    m_lfeCoeff = 1.0f - std::exp(-2.0f * kPi * 120.0f / static_cast<float>(sampleRate));
    updateAirAbsorption();
    updateBinauralModels();
    reset();
    return true;
}

void SpatialObjectPannerNode::reset() noexcept
{
    m_trajectoryTimeSec = 0.0;
    m_airState = 0.0f;
    m_lfeState = 0.0f;
    m_itdWritePos = 0;
    for (auto& buf : m_itdBuffers)
        buf.fill(0.0f);
    for (auto& ears : m_earStates)
        ears = {};
    m_currentGains = m_targetGains;
}

void SpatialObjectPannerNode::setFormat(const SpatialBusFormat format) noexcept
{
    m_format = format;
    computeGains(m_format, m_position, m_targetGains);
    m_currentGains = m_targetGains;
    updateBinauralModels();
}

void SpatialObjectPannerNode::setPosition(const SphericalPosition& position) noexcept
{
    m_position.azimuthDeg = wrapAzimuth(position.azimuthDeg);
    m_position.elevationDeg = std::clamp(position.elevationDeg, -90.0f, 90.0f);
    m_position.distanceM = std::clamp(position.distanceM, 0.1f, 50.0f);
    m_position.size = std::clamp(position.size, 0.0f, 1.0f);
    computeGains(m_format, m_position, m_targetGains);
    m_distanceGain = distanceGain(m_position.distanceM);
    updateAirAbsorption();
}

void SpatialObjectPannerNode::setTrajectory(const SpatialTrajectory& trajectory) noexcept
{
    m_trajectory = trajectory;
    m_trajectoryTimeSec = 0.0;
}

void SpatialObjectPannerNode::setLfeSendDb(const float db) noexcept
{
    const float clamped = std::clamp(db, -60.0f, 12.0f);
    m_lfeGain = clamped <= -60.0f ? 0.0f : std::pow(10.0f, clamped / 20.0f);
}

void SpatialObjectPannerNode::updateAirAbsorption() noexcept
{
    const float fs = static_cast<float>(m_sampleRate);
    const float fc = std::min(0.45f * fs, 20000.0f * std::exp(-m_position.distanceM / 25.0f));
    m_airCoeff = 1.0f - std::exp(-2.0f * kPi * fc / fs);
}

void SpatialObjectPannerNode::updateBinauralModels() noexcept
{
    const auto layout = speakerLayout(m_format);
    const float fs = static_cast<float>(m_sampleRate);
    const float K = 2.0f * fs;
    const float beta = 2.0f * kSpeedOfSound / kHeadRadiusM;
    const float headDelaySec = kHeadRadiusM / kSpeedOfSound;
    const std::array<float, 2> earAxisX {-1.0f, 1.0f};

    for (std::size_t s = 0; s < maxSpeakers; ++s)
    {
        if (s >= layout.size())
        {
            m_earModels[s] = {};
            continue;
        }

        const Vec3 v = toUnitVector(layout[s].azimuthDeg, layout[s].elevationDeg);
        for (std::size_t ear = 0; ear < 2; ++ear)
        {
            const float cosTheta = std::clamp(v.x * earAxisX[ear], -1.0f, 1.0f);
            const float theta = std::acos(cosTheta); // angle between ear axis and source

            // Woodworth ITD relative to head centre, offset to be non-negative.
            const float tau = theta < 0.5f * kPi ? -headDelaySec * cosTheta
                                                 : headDelaySec * (theta - 0.5f * kPi);
            auto& model = m_earModels[s][ear];
            model.delaySamples = std::clamp((tau + headDelaySec) * fs, 0.0f, static_cast<float>(itdBufferSize - 2));

            // Brown-Duda head-shadow: H(s) = (alpha*s + beta) / (s + beta), bilinear transform.
            const float alpha = 1.05f + 0.95f * std::cos(theta * 1.2f);
            const float denom = K + beta;
            model.b0 = (alpha * K + beta) / denom;
            model.b1 = (beta - alpha * K) / denom;
            model.a1 = (beta - K) / denom;
        }
    }
}

void SpatialObjectPannerNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0 || m_monoScratch.empty())
        return;

    const std::size_t numFrames = std::min(audio.frameCount, m_monoScratch.size());
    const std::size_t numChannels = audio.channelCount;

    // 1. Mono object input (average of first two channels when present)
    for (std::size_t i = 0; i < numFrames; ++i)
    {
        float in = audio.channels[0] != nullptr ? audio.channels[0][i] : 0.0f;
        if (numChannels >= 2 && audio.channels[1] != nullptr)
            in = 0.5f * (in + audio.channels[1][i]);
        m_monoScratch[i] = in;
    }

    // 2. Trajectory automation (block-rate gain targets, per-sample interpolation)
    if (m_trajectory.shape != SpatialTrajectoryShape::none)
    {
        const auto pos = evaluateTrajectory(m_trajectory, m_position, m_trajectoryTimeSec);
        computeGains(m_format, pos, m_targetGains);
        m_distanceGain = distanceGain(pos.distanceM);
        const float fs = static_cast<float>(m_sampleRate);
        const float fc = std::min(0.45f * fs, 20000.0f * std::exp(-pos.distanceM / 25.0f));
        m_airCoeff = 1.0f - std::exp(-2.0f * kPi * fc / fs);
        m_trajectoryTimeSec += static_cast<double>(numFrames) / m_sampleRate;
    }

    const auto layout = speakerLayout(m_format);
    const std::size_t numSpeakers = layout.size();
    const float invFrames = 1.0f / static_cast<float>(numFrames);
    std::array<float, maxSpeakers> startGains = m_currentGains;
    std::array<float, maxSpeakers> deltaGains {};
    for (std::size_t s = 0; s < maxSpeakers; ++s)
        deltaGains[s] = (m_targetGains[s] - startGains[s]) * invFrames;

    constexpr std::size_t mask = itdBufferSize - 1;

    for (std::size_t i = 0; i < numFrames; ++i)
    {
        const float raw = m_monoScratch[i];
        m_lfeState += (raw - m_lfeState) * m_lfeCoeff;
        m_airState += (raw - m_airState) * m_airCoeff;
        const float obj = m_airState * m_distanceGain;
        const float lfe = m_lfeState * m_lfeGain;
        const float t = static_cast<float>(i + 1);

        if (m_binaural)
        {
            float earL = 0.5f * lfe;
            float earR = 0.5f * lfe;
            for (std::size_t s = 0; s < numSpeakers; ++s)
            {
                if (layout[s].isLfe)
                    continue;
                const float feed = obj * (startGains[s] + deltaGains[s] * t);
                m_itdBuffers[s][m_itdWritePos] = feed;

                for (std::size_t ear = 0; ear < 2; ++ear)
                {
                    const auto& model = m_earModels[s][ear];
                    const auto whole = static_cast<std::size_t>(model.delaySamples);
                    const float frac = model.delaySamples - static_cast<float>(whole);
                    const float a = m_itdBuffers[s][(m_itdWritePos + itdBufferSize - whole) & mask];
                    const float b = m_itdBuffers[s][(m_itdWritePos + itdBufferSize - whole - 1) & mask];
                    const float delayed = a + (b - a) * frac;

                    auto& st = m_earStates[s][ear];
                    const float y = model.b0 * delayed + model.b1 * st.x1 - model.a1 * st.y1;
                    st.x1 = delayed;
                    st.y1 = y;
                    (ear == 0 ? earL : earR) += y;
                }
            }
            m_itdWritePos = (m_itdWritePos + 1) & mask;

            if (audio.channels[0] != nullptr)
                audio.channels[0][i] = earL;
            if (numChannels >= 2 && audio.channels[1] != nullptr)
                audio.channels[1][i] = earR;
            for (std::size_t ch = 2; ch < numChannels; ++ch)
                if (audio.channels[ch] != nullptr)
                    audio.channels[ch][i] = 0.0f;
        }
        else
        {
            for (std::size_t ch = 0; ch < numChannels; ++ch)
            {
                if (audio.channels[ch] == nullptr)
                    continue;
                float out = 0.0f;
                if (ch < numSpeakers)
                    out = layout[ch].isLfe ? lfe : obj * (startGains[ch] + deltaGains[ch] * t);
                audio.channels[ch][i] = out;
            }
        }
    }

    m_currentGains = m_targetGains;
}

} // namespace dawg::engine::dsp
