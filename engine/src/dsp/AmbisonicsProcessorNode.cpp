#include <dawg/engine/dsp/AmbisonicsProcessorNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float degToRad(float deg) noexcept
{
    return deg * static_cast<float>(std::numbers::pi / 180.0);
}

} // namespace

AmbisonicsProcessorNode::AmbisonicsProcessorNode() noexcept
{
    initializeVirtualSpeakers();
    updateEncodingWeights();
    updateRotationMatrix();
    setOutputLevelDb(m_outputLevelDb);
}

bool AmbisonicsProcessorNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;

    m_hoaBuffers.resize(numHoaChannels);
    for (auto& buf : m_hoaBuffers)
    {
        buf.assign(maxBlockSize, 0.0f);
    }

    initializeVirtualSpeakers();
    updateEncodingWeights();
    updateRotationMatrix();
    reset();
    return true;
}

void AmbisonicsProcessorNode::reset() noexcept
{
    for (auto& spk : m_speakers)
    {
        spk.shadowFilterL = 0.0f;
        spk.shadowFilterR = 0.0f;
        std::fill(std::begin(spk.delayBufferL), std::end(spk.delayBufferL), 0.0f);
        std::fill(std::begin(spk.delayBufferR), std::end(spk.delayBufferR), 0.0f);
        spk.delayWritePos = 0;
    }

    for (auto& buf : m_hoaBuffers)
    {
        std::fill(buf.begin(), buf.end(), 0.0f);
    }

    m_hoaLevels.fill(0.0f);
}

void AmbisonicsProcessorNode::setSourceAzimuthDeg(const float deg) noexcept
{
    m_sourceAzimuthDeg = std::clamp(deg, -180.0f, 180.0f);
    updateEncodingWeights();
}

void AmbisonicsProcessorNode::setSourceElevationDeg(const float deg) noexcept
{
    m_sourceElevationDeg = std::clamp(deg, -90.0f, 90.0f);
    updateEncodingWeights();
}

void AmbisonicsProcessorNode::setSourceSpread(const float spread) noexcept
{
    m_sourceSpread = std::clamp(spread, 0.0f, 1.0f);
    updateEncodingWeights();
}

void AmbisonicsProcessorNode::setHeadRotation(const float yawDeg, const float pitchDeg, const float rollDeg) noexcept
{
    m_headYawDeg = std::clamp(yawDeg, -180.0f, 180.0f);
    m_headPitchDeg = std::clamp(pitchDeg, -90.0f, 90.0f);
    m_headRollDeg = std::clamp(rollDeg, -180.0f, 180.0f);
    updateRotationMatrix();
}

void AmbisonicsProcessorNode::setOutputLevelDb(const float db) noexcept
{
    m_outputLevelDb = std::clamp(db, -24.0f, 12.0f);
    m_outputGainLinear = std::pow(10.0f, m_outputLevelDb / 20.0f);
}

void AmbisonicsProcessorNode::getHoaChannelLevels(float* dest, const std::size_t count) const noexcept
{
    if (dest == nullptr || count == 0)
    {
        return;
    }

    const auto copyCount = std::min(count, numHoaChannels);
    for (std::size_t i = 0; i < copyCount; ++i)
    {
        dest[i] = m_hoaLevels[i];
    }
}

void AmbisonicsProcessorNode::computeHarmonics(
    const float azimuthRad,
    const float elevationRad,
    float* weightsOut) noexcept
{
    if (weightsOut == nullptr)
    {
        return;
    }

    const float cosElev = std::cos(elevationRad);
    const float sinElev = std::sin(elevationRad);
    const float cosElev2 = cosElev * cosElev;
    const float cosElev3 = cosElev2 * cosElev;
    const float sinElev2 = sinElev * sinElev;

    const float cosAz = std::cos(azimuthRad);
    const float sinAz = std::sin(azimuthRad);
    const float cos2Az = std::cos(2.0f * azimuthRad);
    const float sin2Az = std::sin(2.0f * azimuthRad);
    const float cos3Az = std::cos(3.0f * azimuthRad);
    const float sin3Az = std::sin(3.0f * azimuthRad);

    // Order 0 (Monopole)
    weightsOut[0] = 1.0f; // W

    // Order 1 (Dipoles)
    weightsOut[1] = sinAz * cosElev; // Y
    weightsOut[2] = sinElev;         // Z
    weightsOut[3] = cosAz * cosElev; // X

    // Order 2 (Quadrupoles)
    constexpr float sqrt3 = 1.73205080757f;
    constexpr float sqrt3Div2 = 0.86602540378f;
    weightsOut[4] = sqrt3Div2 * sin2Az * cosElev2;              // V
    weightsOut[5] = sqrt3 * sinAz * sinElev * cosElev;          // T
    weightsOut[6] = 0.5f * (3.0f * sinElev2 - 1.0f);            // R
    weightsOut[7] = sqrt3 * cosAz * sinElev * cosElev;          // S
    weightsOut[8] = sqrt3Div2 * cos2Az * cosElev2;              // U

    // Order 3 (Octupoles)
    constexpr float sqrt5Div8 = 0.790569415f;
    constexpr float sqrt15Div2 = 1.936491673f;
    constexpr float sqrt3Div8 = 0.612372435f;

    weightsOut[9]  = sqrt5Div8 * sin3Az * cosElev3;                                  // Q
    weightsOut[10] = sqrt15Div2 * sin2Az * sinElev * cosElev2;                       // O
    weightsOut[11] = sqrt3Div8 * sinAz * cosElev * (5.0f * sinElev2 - 1.0f);        // M
    weightsOut[12] = 0.5f * sinElev * (5.0f * sinElev2 - 3.0f);                     // K
    weightsOut[13] = sqrt3Div8 * cosAz * cosElev * (5.0f * sinElev2 - 1.0f);        // L
    weightsOut[14] = sqrt15Div2 * cos2Az * sinElev * cosElev2;                       // N
    weightsOut[15] = sqrt5Div8 * cos3Az * cosElev3;                                  // P
}

void AmbisonicsProcessorNode::updateEncodingWeights() noexcept
{
    const float azRad = degToRad(m_sourceAzimuthDeg);
    const float elRad = degToRad(m_sourceElevationDeg);

    computeHarmonics(azRad, elRad, m_encodingWeights.data());

    // Angular spread damping on higher orders:
    // When spread is 1.0 (diffuse), higher orders are attenuated, leaving omni W dominant.
    const float order1Damp = 1.0f - m_sourceSpread * 0.45f;
    const float order2Damp = (1.0f - m_sourceSpread) * (1.0f - m_sourceSpread * 0.6f);
    const float order3Damp = std::pow(1.0f - m_sourceSpread, 3.0f);

    for (std::size_t i = 1; i <= 3; ++i)
    {
        m_encodingWeights[i] *= order1Damp;
    }
    for (std::size_t i = 4; i <= 8; ++i)
    {
        m_encodingWeights[i] *= order2Damp;
    }
    for (std::size_t i = 9; i <= 15; ++i)
    {
        m_encodingWeights[i] *= order3Damp;
    }
}

void AmbisonicsProcessorNode::updateRotationMatrix() noexcept
{
    // Initialize to identity
    for (std::size_t r = 0; r < numHoaChannels; ++r)
    {
        for (std::size_t c = 0; c < numHoaChannels; ++c)
        {
            m_rotMatrix[r][c] = (r == c) ? 1.0f : 0.0f;
        }
    }

    // 1. Yaw rotation (rotation about vertical Z axis by -yaw)
    const float yaw = -degToRad(m_headYawDeg);
    const float cosY1 = std::cos(yaw);
    const float sinY1 = std::sin(yaw);
    const float cosY2 = std::cos(2.0f * yaw);
    const float sinY2 = std::sin(2.0f * yaw);
    const float cosY3 = std::cos(3.0f * yaw);
    const float sinY3 = std::sin(3.0f * yaw);

    // Order 1 (Y=1, X=3)
    m_rotMatrix[1][1] = cosY1;
    m_rotMatrix[1][3] = -sinY1;
    m_rotMatrix[3][1] = sinY1;
    m_rotMatrix[3][3] = cosY1;

    // Order 2: (V=4, U=8) at 2*yaw, (T=5, S=7) at 1*yaw
    m_rotMatrix[4][4] = cosY2;
    m_rotMatrix[4][8] = -sinY2;
    m_rotMatrix[8][4] = sinY2;
    m_rotMatrix[8][8] = cosY2;

    m_rotMatrix[5][5] = cosY1;
    m_rotMatrix[5][7] = -sinY1;
    m_rotMatrix[7][5] = sinY1;
    m_rotMatrix[7][7] = cosY1;

    // Order 3: (Q=9, P=15) at 3*yaw, (O=10, N=14) at 2*yaw, (M=11, L=13) at 1*yaw
    m_rotMatrix[9][9]   = cosY3;
    m_rotMatrix[9][15]  = -sinY3;
    m_rotMatrix[15][9]  = sinY3;
    m_rotMatrix[15][15] = cosY3;

    m_rotMatrix[10][10] = cosY2;
    m_rotMatrix[10][14] = -sinY2;
    m_rotMatrix[14][10] = sinY2;
    m_rotMatrix[14][14] = cosY2;

    m_rotMatrix[11][11] = cosY1;
    m_rotMatrix[11][13] = -sinY1;
    m_rotMatrix[13][11] = sinY1;
    m_rotMatrix[13][13] = cosY1;

    // 2. Pitch rotation (rotation about lateral Y axis by -pitch)
    // Pitch couples Z (2) and X (3) in Order 1
    const float pitch = -degToRad(m_headPitchDeg);
    const float cosP = std::cos(pitch);
    const float sinP = std::sin(pitch);

    const float cur2_2 = m_rotMatrix[2][2];
    const float cur3_3 = m_rotMatrix[3][3];
    m_rotMatrix[2][2] = cur2_2 * cosP;
    m_rotMatrix[2][3] = sinP;
    m_rotMatrix[3][2] = -sinP;
    m_rotMatrix[3][3] = cur3_3 * cosP;
}

void AmbisonicsProcessorNode::initializeVirtualSpeakers() noexcept
{
    // 7.1.4 Dolby Atmos standard virtual speaker coordinates
    // (Azimuth deg, Elevation deg)
    static constexpr std::pair<float, float> speakerCoords[numVirtualSpeakers] = {
        {  30.0f,   0.0f }, // 0: Left
        { -30.0f,   0.0f }, // 1: Right
        {   0.0f,   0.0f }, // 2: Center
        {   0.0f, -20.0f }, // 3: LFE
        { 110.0f,   0.0f }, // 4: Left Surround
        {-110.0f,   0.0f }, // 5: Right Surround
        { 150.0f,   0.0f }, // 6: Left Back
        {-150.0f,   0.0f }, // 7: Right Back
        {  45.0f,  45.0f }, // 8: Top Front Left
        { -45.0f,  45.0f }, // 9: Top Front Right
        { 135.0f,  45.0f }, // 10: Top Rear Left
        {-135.0f,  45.0f }  // 11: Top Rear Right
    };

    for (std::size_t s = 0; s < numVirtualSpeakers; ++s)
    {
        auto& spk = m_speakers[s];
        spk.azimuthRad = degToRad(speakerCoords[s].first);
        spk.elevationRad = degToRad(speakerCoords[s].second);

        // Compute decoding weights from spherical harmonics
        computeHarmonics(spk.azimuthRad, spk.elevationRad, spk.decodeWeights.data());

        // Max-rE weighting optimization for high-frequency directivity in ambisonic decoding
        // Order 0: 1.0, Order 1: 0.89, Order 2: 0.70, Order 3: 0.48
        constexpr float maxReWeights[4] = { 1.0f, 0.89f, 0.70f, 0.48f };
        spk.decodeWeights[0] *= maxReWeights[0];
        for (std::size_t i = 1; i <= 3; ++i) { spk.decodeWeights[i] *= maxReWeights[1]; }
        for (std::size_t i = 4; i <= 8; ++i) { spk.decodeWeights[i] *= maxReWeights[2]; }
        for (std::size_t i = 9; i <= 15; ++i) { spk.decodeWeights[i] *= maxReWeights[3]; }

        // Normalization factor for virtual speaker array
        constexpr float invSpeakerCount = 1.0f / static_cast<float>(numVirtualSpeakers);
        for (float& w : spk.decodeWeights)
        {
            w *= invSpeakerCount * 1.5f;
        }

        // Binaural Woodworth ITD calculation (approx 0.0875m ear radius, speed of sound 343 m/s)
        const float sinAz = std::sin(spk.azimuthRad);
        const float itdSeconds = (0.0875f / 343.0f) * (sinAz + spk.azimuthRad);
        spk.itdSamples = std::clamp(itdSeconds * static_cast<float>(m_sampleRate), -30.0f, 30.0f);
    }
}

void AmbisonicsProcessorNode::decodeMultichannel(
    const float* const* hoaInputs,
    float* const* speakerOutputs,
    const std::size_t channelCount,
    const std::size_t frameCount) noexcept
{
    if (hoaInputs == nullptr || speakerOutputs == nullptr || channelCount == 0 || frameCount == 0)
    {
        return;
    }

    const std::size_t decodeLimit = std::min(channelCount, numVirtualSpeakers);

    for (std::size_t s = 0; s < decodeLimit; ++s)
    {
        auto* out = speakerOutputs[s];
        if (out == nullptr) { continue; }

        const auto& weights = m_speakers[s].decodeWeights;

        for (std::size_t i = 0; i < frameCount; ++i)
        {
            float sum = 0.0f;
            for (std::size_t h = 0; h < numHoaChannels; ++h)
            {
                if (hoaInputs[h] != nullptr)
                {
                    sum += hoaInputs[h][i] * weights[h];
                }
            }
            out[i] = sum * m_outputGainLinear;
        }
    }
}

void AmbisonicsProcessorNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channelCount == 0 || context.audio.frameCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    // Ensure internal HOA buffers match block size
    if (m_hoaBuffers.empty() || m_hoaBuffers[0].size() < frameCount)
    {
        m_hoaBuffers.resize(numHoaChannels);
        for (auto& buf : m_hoaBuffers)
        {
            buf.assign(frameCount, 0.0f);
        }
    }

    // 1. Encode input signal to 16-channel HOA soundfield
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float monoIn = 0.5f * (ch0[i] + (ch1 != nullptr ? ch1[i] : ch0[i]));

        for (std::size_t h = 0; h < numHoaChannels; ++h)
        {
            m_hoaBuffers[h][i] = monoIn * m_encodingWeights[h];
        }
    }

    // 2. Apply 3D Head Rotation Matrix across all 16 HOA channels
    for (std::size_t i = 0; i < frameCount; ++i)
    {
        std::array<float, numHoaChannels> unrotated {};
        for (std::size_t h = 0; h < numHoaChannels; ++h)
        {
            unrotated[h] = m_hoaBuffers[h][i];
        }

        for (std::size_t r = 0; r < numHoaChannels; ++r)
        {
            float rotatedSample = 0.0f;
            for (std::size_t c = 0; c < numHoaChannels; ++c)
            {
                rotatedSample += m_rotMatrix[r][c] * unrotated[c];
            }
            m_hoaBuffers[r][i] = rotatedSample;
        }
    }

    // Measure HOA channel energies for telemetry
    for (std::size_t h = 0; h < numHoaChannels; ++h)
    {
        float sumSq = 0.0f;
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            sumSq += m_hoaBuffers[h][i] * m_hoaBuffers[h][i];
        }
        m_hoaLevels[h] = std::sqrt(sumSq / static_cast<float>(frameCount));
    }

    // 3. Decode to Target Output
    if (m_decoderMode == AmbisonicsDecoderMode::binauralHeadphones)
    {
        // Decode to virtual speakers and render binaural headphone stereo
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            float binL = 0.0f;
            float binR = 0.0f;

            for (std::size_t s = 0; s < numVirtualSpeakers; ++s)
            {
                auto& spk = m_speakers[s];

                // Decode HOA to this speaker
                float spkSample = 0.0f;
                for (std::size_t h = 0; h < numHoaChannels; ++h)
                {
                    spkSample += m_hoaBuffers[h][i] * spk.decodeWeights[h];
                }

                // Push to delay buffer
                spk.delayBufferL[spk.delayWritePos] = spkSample;
                spk.delayBufferR[spk.delayWritePos] = spkSample;

                // Read with ITD offset (positive itd means closer to left ear)
                const float itdOffset = spk.itdSamples;
                const int delayL = std::clamp(static_cast<int>(16.0f - itdOffset), 0, 60);
                const int delayR = std::clamp(static_cast<int>(16.0f + itdOffset), 0, 60);

                const std::size_t readPosL = (spk.delayWritePos + 64 - static_cast<std::size_t>(delayL)) % 64;
                const std::size_t readPosR = (spk.delayWritePos + 64 - static_cast<std::size_t>(delayR)) % 64;

                const float delayedL = spk.delayBufferL[readPosL];
                const float delayedR = spk.delayBufferR[readPosR];

                // Head shadow spectral filter: 1-pole lowpass for far ear
                const float shadowCutoffL = (itdOffset >= 0.0f) ? 0.95f : 0.65f;
                const float shadowCutoffR = (itdOffset <= 0.0f) ? 0.95f : 0.65f;

                spk.shadowFilterL += shadowCutoffL * (delayedL - spk.shadowFilterL);
                spk.shadowFilterR += shadowCutoffR * (delayedR - spk.shadowFilterR);

                binL += spk.shadowFilterL;
                binR += spk.shadowFilterR;

                spk.delayWritePos = (spk.delayWritePos + 1) % 64;
            }

            ch0[i] = binL * m_outputGainLinear;
            if (ch1 != nullptr)
            {
                ch1[i] = binR * m_outputGainLinear;
            }
        }
    }
    else if (m_decoderMode == AmbisonicsDecoderMode::rawBFormatHoa3)
    {
        // Output ACN 0 and 1 onto the stereo pair
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            ch0[i] = m_hoaBuffers[0][i] * m_outputGainLinear;
            if (ch1 != nullptr)
            {
                ch1[i] = m_hoaBuffers[1][i] * m_outputGainLinear;
            }
        }
    }
    else
    {
        // 5.1 / 7.1 / Atmos fold-down to stereo monitor
        for (std::size_t i = 0; i < frameCount; ++i)
        {
            float spkL = 0.0f;
            float spkR = 0.0f;

            for (std::size_t h = 0; h < numHoaChannels; ++h)
            {
                spkL += m_hoaBuffers[h][i] * m_speakers[0].decodeWeights[h];
                spkR += m_hoaBuffers[h][i] * m_speakers[1].decodeWeights[h];
            }

            ch0[i] = spkL * m_outputGainLinear;
            if (ch1 != nullptr)
            {
                ch1[i] = spkR * m_outputGainLinear;
            }
        }
    }
}

} // namespace dawg::engine::dsp
