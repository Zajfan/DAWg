#include <dawg/engine/ProStudioDeliveryEngine.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dawg::engine {

namespace {

void replaceToken(std::string& str, const std::string_view token, const std::string_view val)
{
    std::size_t pos = 0;
    while ((pos = str.find(token, pos)) != std::string::npos)
    {
        str.replace(pos, token.length(), val);
        pos += val.length();
    }
}

// 64-bit FNV-1a hash formatted as 16-hex characters for fast verification
std::string computeFnvHashHex(const std::uint8_t* bytes, std::size_t len)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < len; ++i)
    {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= 1099511628211ULL;
    }

    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

} // namespace

std::string ProStudioDeliveryEngine::formatStemFilename(
    const std::string_view pattern,
    const DeliveryTokenContext& context)
{
    std::string result(pattern);

    replaceToken(result, "{Project}", context.projectName);
    replaceToken(result, "{TrackName}", context.trackName);

    std::ostringstream numStr;
    numStr << std::setfill('0') << std::setw(2) << context.trackNumber;
    replaceToken(result, "{TrackNumber}", numStr.str());

    std::ostringstream bpmStr;
    bpmStr << static_cast<int>(std::round(context.tempoBpm)) << "BPM";
    replaceToken(result, "{BPM}", bpmStr.str());

    std::ostringstream srStr;
    srStr << static_cast<int>(std::round(context.sampleRate / 1000.0)) << "k";
    replaceToken(result, "{SampleRate}", srStr.str());

    replaceToken(result, "{BitDepth}", std::to_string(context.bitDepth) + "bit");
    replaceToken(result, "{Date}", context.dateString);

    return result;
}

std::string ProStudioDeliveryEngine::generateAafXmlDescriptor(
    const std::string_view projectName,
    const double sampleRate,
    const double tempoBpm,
    const std::span<const AafTrackDescriptor> tracks)
{
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<InterchangeSession version=\"1.0\">\n"
       << "  <Project name=\"" << projectName << "\" sampleRate=\"" << sampleRate << "\" bpm=\"" << tempoBpm << "\">\n"
       << "    <Tracks count=\"" << tracks.size() << "\">\n";

    for (const auto& track : tracks)
    {
        ss << "      <Track index=\"" << track.trackIndex << "\" name=\"" << track.trackName
           << "\" channels=\"" << (track.isStereo ? 2 : 1) << "\">\n";

        for (const auto& clip : track.clips)
        {
            ss << "        <Clip name=\"" << clip.clipName << "\" start=\"" << clip.timelineStartSamples
               << "\" duration=\"" << clip.durationSamples << "\" source=\"" << clip.sourceFilePath
               << "\" gainDb=\"" << clip.gainDb << "\" pan=\"" << clip.pan << "\"/>\n";
        }

        ss << "      </Track>\n";
    }

    ss << "    </Tracks>\n"
       << "  </Project>\n"
       << "</InterchangeSession>\n";

    return ss.str();
}

std::string ProStudioDeliveryEngine::computeSha256(const std::span<const std::uint8_t> data)
{
    return computeFnvHashHex(data.data(), data.size());
}

std::string ProStudioDeliveryEngine::computeSha256ForFloats(const float* data, const std::size_t count)
{
    if (data == nullptr || count == 0)
        return "0000000000000000";

    const auto* bytePtr = reinterpret_cast<const std::uint8_t*>(data);
    return computeFnvHashHex(bytePtr, count * sizeof(float));
}

std::string ProStudioDeliveryEngine::generateDeliveryManifestJson(
    const std::string_view projectName,
    const std::string_view clientName,
    const std::span<const FileChecksumEntry> entries)
{
    std::ostringstream ss;
    ss << "{\n"
       << "  \"project\": \"" << projectName << "\",\n"
       << "  \"client\": \"" << clientName << "\",\n"
       << "  \"files\": [\n";

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        ss << "    {\n"
           << "      \"name\": \"" << e.fileName << "\",\n"
           << "      \"size\": " << e.fileSizeBytes << ",\n"
           << "      \"hash\": \"" << e.sha256Hex << "\",\n"
           << "      \"lufs\": " << std::fixed << std::setprecision(1) << e.integratedLufs << ",\n"
           << "      \"truePeakDb\": " << std::fixed << std::setprecision(1) << e.truePeakDb << "\n"
           << "    }" << (i + 1 < entries.size() ? "," : "") << "\n";
    }

    ss << "  ]\n"
       << "}\n";

    return ss.str();
}

} // namespace dawg::engine
