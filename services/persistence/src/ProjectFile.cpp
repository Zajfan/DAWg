#include <dawg/persistence/ProjectFile.h>
#include <dawg/project/Project.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace dawg::persistence {
namespace {
namespace fs = std::filesystem;
using Code = ProjectFileError::Code;
using Json = nlohmann::json;

std::unexpected<ProjectFileError> failure(const Code code, std::string message)
{
    return std::unexpected(ProjectFileError {code, std::move(message)});
}

std::string_view encodePathKind(const project::AudioAssetPathKind kind) noexcept
{
    return kind == project::AudioAssetPathKind::absolute ? "absolute" : "projectRelative";
}

std::optional<project::AudioAssetPathKind> decodePathKind(const std::string_view value) noexcept
{
    if (value == "projectRelative")
        return project::AudioAssetPathKind::projectRelative;
    if (value == "absolute")
        return project::AudioAssetPathKind::absolute;
    return std::nullopt;
}

bool validPath(const fs::path& path)
{
    const auto& native = path.native();
    return ! path.empty() && ! path.filename().empty() && path.filename() != "."
        && path.filename() != ".."
        && native.find(fs::path::value_type {}) == fs::path::string_type::npos;
}

// Exclusive creation prevents stale files or concurrent saves from sharing a
// temporary file. Cleanup owns only the file successfully created by this call.
struct TemporaryFile final
{
    fs::path path;
    std::FILE* handle {};
    bool owned {};

    explicit TemporaryFile(const fs::path& destination) : path {destination} {}
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile()
    {
        if (handle != nullptr)
            std::fclose(handle);
        if (owned)
        {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    }
};

std::expected<void, ProjectFileError> writeTransaction(
    const std::string& document, const fs::path& destination)
{
    static std::atomic<std::uint64_t> sequence {};
#if defined(_WIN32)
    const auto processId = _getpid();
#else
    const auto processId = getpid();
#endif
    TemporaryFile temporary {destination};
    for (unsigned attempt = 0; attempt < 100; ++attempt)
    {
        temporary.path = destination;
        temporary.path += ".tmp-" + std::to_string(processId) + "-"
                        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
#if defined(_WIN32)
        _wfopen_s(&temporary.handle, temporary.path.c_str(), L"wbx");
#else
        temporary.handle = std::fopen(temporary.path.c_str(), "wbx");
#endif
        if (temporary.handle != nullptr)
        {
            temporary.owned = true;
            break;
        }
        if (errno != EEXIST)
            return failure(Code::ioFailure, "Cannot create sibling temporary project file");
    }
    if (! temporary.owned)
        return failure(Code::ioFailure, "Cannot reserve a unique temporary project file");

    if (std::fwrite(document.data(), 1, document.size(), temporary.handle) != document.size()
        || std::fflush(temporary.handle) != 0)
        return failure(Code::ioFailure, "Cannot write or flush temporary project file");

    auto* handle = std::exchange(temporary.handle, nullptr);
    if (std::fclose(handle) != 0)
        return failure(Code::ioFailure, "Cannot close temporary project file");

    std::error_code error;
    fs::rename(temporary.path, destination, error);
    if (error)
        return failure(Code::ioFailure, "Cannot replace project file: " + error.message());
    temporary.owned = false;
    return {};
}

std::expected<project::ProjectSnapshot, ProjectFileError> decode(const Json& document)
{
    const auto& version = document.at("formatVersion");
    if (! version.is_number_integer())
        return failure(Code::invalidFormat, "formatVersion must be an integer");
    // Compare before narrowing, including versions larger than uint32_t.
    if (version != Json(1) && version != Json(2) && version != Json(3))
        return failure(Code::unsupportedVersion, "Only project format versions 1, 2, and 3 are supported");

    const auto& id = document.at("projectId");
    const auto& tracks = document.at("tracks");
    if (! id.is_number_unsigned() || ! tracks.is_array())
        return failure(Code::invalidFormat, "Expected an unsigned projectId and a tracks array");

    const std::uint32_t normalizedVersion = (version == Json(3)) ? 3u : 2u;
    project::ProjectSnapshot snapshot {
        normalizedVersion, id.get<project::ProjectId>(), document.at("name").get<std::string>(), {}, {}, {}};
    snapshot.tracks.reserve(tracks.size());
    for (const auto& track : tracks)
    {
        const auto& trackId = track.at("id");
        const auto& muted = track.at("muted");
        const auto& solo = track.at("solo");
        if (! trackId.is_number_unsigned() || ! muted.is_boolean() || ! solo.is_boolean())
            return failure(Code::invalidFormat, "Expected an unsigned track ID and boolean flags");

        project::Track parsedTrack {
            trackId.get<project::TrackId>(),
            track.at("name").get<std::string>(),
            muted.get<bool>(),
            solo.get<bool>()
        };
        if (track.contains("gainLinear"))
        {
            const auto& gain = track.at("gainLinear");
            if (! gain.is_number())
                return failure(Code::invalidFormat, "Expected numeric track gainLinear");
            parsedTrack.gainLinear = gain.get<float>();
        }
        if (track.contains("pan"))
        {
            const auto& pan = track.at("pan");
            if (! pan.is_number())
                return failure(Code::invalidFormat, "Expected numeric track pan");
            parsedTrack.pan = pan.get<float>();
        }
        if (track.contains("phaseInvert"))
        {
            const auto& phaseInvert = track.at("phaseInvert");
            if (! phaseInvert.is_boolean())
                return failure(Code::invalidFormat, "Expected boolean track phaseInvert");
            parsedTrack.phaseInvert = phaseInvert.get<bool>();
        }
        if (track.contains("kind"))
        {
            const auto& kind = track.at("kind");
            if (! kind.is_string())
                return failure(Code::invalidFormat, "Expected string track kind");
            parsedTrack.kind = (kind.get<std::string>() == "instrument")
                ? project::TrackKind::instrument
                : project::TrackKind::audio;
        }
        if (track.contains("inserts"))
        {
            const auto& inserts = track.at("inserts");
            if (! inserts.is_array())
                return failure(Code::invalidFormat, "Expected array for track inserts");
            for (const auto& ins : inserts)
            {
                project::DspNodeDescriptor desc;
                desc.slotIndex = ins.at("slotIndex").get<std::uint32_t>();
                const auto typeStr = ins.at("type").get<std::string>();
                if (typeStr == "parametricEq") desc.type = project::DspNodeType::parametricEq;
                else if (typeStr == "dynamicsCompressor") desc.type = project::DspNodeType::dynamicsCompressor;
                else if (typeStr == "brickwallLimiter") desc.type = project::DspNodeType::brickwallLimiter;
                else if (typeStr == "tnhcSynth") desc.type = project::DspNodeType::tnhcSynth;
                else if (typeStr == "clapPlugin") desc.type = project::DspNodeType::clapPlugin;
                desc.bypassed = ins.at("bypassed").get<bool>();
                desc.name = ins.at("name").get<std::string>();
                if (ins.contains("pluginId")) desc.pluginId = ins.at("pluginId").get<std::string>();
                if (ins.contains("parameters") && ins.at("parameters").is_array())
                {
                    for (const auto& p : ins.at("parameters"))
                        desc.parameters.push_back(p.get<float>());
                }
                parsedTrack.inserts.push_back(std::move(desc));
            }
        }
        snapshot.tracks.push_back(std::move(parsedTrack));
    }

    if (version == Json(2) || version == Json(3))
    {
        const auto& assets = document.at("assets");
        const auto& clips = document.at("clips");
        if (! assets.is_array() || ! clips.is_array())
            return failure(Code::invalidFormat, "Expected assets and clips arrays in format version 2/3");

        snapshot.assets.reserve(assets.size());
        for (const auto& asset : assets)
        {
            const auto& assetId = asset.at("id");
            const auto& path = asset.at("path");
            const auto& pathKind = asset.at("pathKind");
            const auto& sampleRate = asset.at("sampleRate");
            const auto& channelCount = asset.at("channelCount");
            const auto& frameCount = asset.at("frameCount");
            if (! assetId.is_number_unsigned() || ! path.is_string() || ! pathKind.is_string()
                || ! sampleRate.is_number_unsigned() || ! channelCount.is_number_unsigned()
                || ! frameCount.is_number_unsigned())
                return failure(Code::invalidFormat, "Expected typed asset metadata fields");

            const auto decodedKind = decodePathKind(pathKind.get<std::string>());
            const auto rawSampleRate = sampleRate.get<std::uint64_t>();
            const auto rawChannelCount = channelCount.get<std::uint64_t>();
            if (! decodedKind || rawSampleRate > std::numeric_limits<std::uint32_t>::max()
                || rawChannelCount > std::numeric_limits<std::uint16_t>::max())
                return failure(Code::invalidFormat, "Asset pathKind or numeric bounds are invalid");

            snapshot.assets.push_back({assetId.get<project::MediaAssetId>(),
                path.get<std::string>(), *decodedKind,
                static_cast<std::uint32_t>(rawSampleRate),
                static_cast<std::uint16_t>(rawChannelCount),
                frameCount.get<std::uint64_t>()});
        }

        snapshot.clips.reserve(clips.size());
        for (const auto& clip : clips)
        {
            const auto& clipId = clip.at("id");
            const auto& assetId = clip.at("assetId");
            const auto& startSample = clip.at("startSample");
            const auto& sourceOffset = clip.at("sourceOffset");
            const auto& lengthSamples = clip.at("lengthSamples");
            if (! clipId.is_number_unsigned() || ! assetId.is_number_unsigned()
                || ! startSample.is_number_unsigned() || ! sourceOffset.is_number_unsigned()
                || ! lengthSamples.is_number_unsigned())
                return failure(Code::invalidFormat, "Expected unsigned clip timing and ID fields");

            project::AudioClip parsedClip {
                clipId.get<project::ClipId>(),
                assetId.get<project::MediaAssetId>(),
                startSample.get<std::uint64_t>(),
                sourceOffset.get<std::uint64_t>(),
                lengthSamples.get<std::uint64_t>()
            };
            if (clip.contains("trackId"))
            {
                const auto& trackId = clip.at("trackId");
                if (! trackId.is_number_unsigned())
                    return failure(Code::invalidFormat, "Expected unsigned clip trackId");
                parsedClip.trackId = trackId.get<project::TrackId>();
            }
            if (clip.contains("gainLinear"))
            {
                const auto& gain = clip.at("gainLinear");
                if (! gain.is_number())
                    return failure(Code::invalidFormat, "Expected numeric clip gainLinear");
                parsedClip.gainLinear = gain.get<float>();
            }
            if (clip.contains("fadeInSamples"))
            {
                const auto& fadeIn = clip.at("fadeInSamples");
                if (! fadeIn.is_number_unsigned()
                    || fadeIn.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                    return failure(Code::invalidFormat, "Expected uint32 clip fadeInSamples");
                parsedClip.fadeInSamples = static_cast<std::uint32_t>(fadeIn.get<std::uint64_t>());
            }
            if (clip.contains("fadeOutSamples"))
            {
                const auto& fadeOut = clip.at("fadeOutSamples");
                if (! fadeOut.is_number_unsigned()
                    || fadeOut.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                    return failure(Code::invalidFormat, "Expected uint32 clip fadeOutSamples");
                parsedClip.fadeOutSamples = static_cast<std::uint32_t>(fadeOut.get<std::uint64_t>());
            }
            if (clip.contains("warpingEnabled"))
            {
                parsedClip.warpingEnabled = clip.at("warpingEnabled").get<bool>();
            }
            if (clip.contains("warpMarkers"))
            {
                const auto& markers = clip.at("warpMarkers");
                if (! markers.is_array())
                    return failure(Code::invalidFormat, "Expected array for clip warpMarkers");
                for (const auto& m : markers)
                {
                    parsedClip.warpMarkers.push_back(project::WarpMarker {
                        .id = m.at("id").get<std::uint32_t>(),
                        .sourceSample = m.at("sourceSample").get<std::uint64_t>(),
                        .targetSample = m.at("targetSample").get<std::uint64_t>()
                    });
                }
            }
            snapshot.clips.push_back(parsedClip);
        }

        if (version == Json(3) || document.contains("tempo"))
        {
            const auto& tempo = document.at("tempo");
            if (! tempo.is_object())
                return failure(Code::invalidFormat, "Expected tempo object");
            const auto& bpm = tempo.at("bpm");
            const auto& num = tempo.at("timeSigNumerator");
            const auto& den = tempo.at("timeSigDenominator");
            const auto& met = tempo.at("metronomeEnabled");
            if (! bpm.is_number() || ! num.is_number_unsigned() || ! den.is_number_unsigned()
                || ! met.is_boolean() || num.get<std::uint64_t>() > 255
                || den.get<std::uint64_t>() > 255)
                return failure(Code::invalidFormat, "Invalid tempo object fields");
            snapshot.tempo = project::TempoSetting {
                .bpm = bpm.get<double>(),
                .timeSigNumerator = static_cast<std::uint8_t>(num.get<std::uint64_t>()),
                .timeSigDenominator = static_cast<std::uint8_t>(den.get<std::uint64_t>()),
                .metronomeEnabled = met.get<bool>()
            };
        }

        if (version == Json(3) || document.contains("loop"))
        {
            const auto& loop = document.at("loop");
            if (! loop.is_object())
                return failure(Code::invalidFormat, "Expected loop object");
            const auto& enabled = loop.at("enabled");
            const auto& startSample = loop.at("startSample");
            const auto& endSample = loop.at("endSample");
            if (! enabled.is_boolean() || ! startSample.is_number_unsigned()
                || ! endSample.is_number_unsigned())
                return failure(Code::invalidFormat, "Invalid loop object fields");
            snapshot.loop = project::LoopSetting {
                .enabled = enabled.get<bool>(),
                .startSample = startSample.get<std::uint64_t>(),
                .endSample = endSample.get<std::uint64_t>()
            };
        }

        if (document.contains("midiClips"))
        {
            const auto& mClips = document.at("midiClips");
            if (! mClips.is_array())
                return failure(Code::invalidFormat, "Expected array for midiClips");
            for (const auto& mc : mClips)
            {
                project::MidiClip parsedMidiClip;
                parsedMidiClip.id = mc.at("id").get<project::MidiClipId>();
                if (mc.contains("trackId"))
                    parsedMidiClip.trackId = mc.at("trackId").get<project::TrackId>();
                parsedMidiClip.startSample = mc.at("startSample").get<std::uint64_t>();
                parsedMidiClip.lengthSamples = mc.at("lengthSamples").get<std::uint64_t>();
                if (mc.contains("notes"))
                {
                    const auto& notes = mc.at("notes");
                    if (! notes.is_array())
                        return failure(Code::invalidFormat, "Expected array for notes");
                    for (const auto& n : notes)
                    {
                        parsedMidiClip.notes.push_back({
                            .id = n.at("id").get<project::NoteId>(),
                            .pitch = static_cast<std::uint8_t>(n.at("pitch").get<unsigned>()),
                            .velocity = static_cast<std::uint8_t>(n.at("velocity").get<unsigned>()),
                            .startSample = n.at("startSample").get<std::uint64_t>(),
                            .lengthSamples = n.at("lengthSamples").get<std::uint64_t>()
                        });
                    }
                }
                snapshot.midiClips.push_back(std::move(parsedMidiClip));
            }
        }

        if (document.contains("automationLanes"))
        {
            const auto& aLanes = document.at("automationLanes");
            if (! aLanes.is_array())
                return failure(Code::invalidFormat, "Expected array for automationLanes");
            for (const auto& al : aLanes)
            {
                project::AutomationLane lane;
                lane.id = al.at("id").get<project::AutomationLaneId>();
                if (al.contains("target"))
                {
                    const auto& tgt = al.at("target");
                    const auto kindStr = tgt.at("kind").get<std::string>();
                    if (kindStr == "trackVolume") lane.target.kind = project::AutomationTargetKind::trackVolume;
                    else if (kindStr == "trackPan") lane.target.kind = project::AutomationTargetKind::trackPan;
                    else if (kindStr == "trackSend") lane.target.kind = project::AutomationTargetKind::trackSend;
                    else if (kindStr == "dspParameter") lane.target.kind = project::AutomationTargetKind::dspParameter;
                    lane.target.trackId = tgt.at("trackId").get<project::TrackId>();
                    lane.target.slotIndex = tgt.at("slotIndex").get<std::uint32_t>();
                    lane.target.parameterIndex = tgt.at("parameterIndex").get<std::uint32_t>();
                }
                if (al.contains("points") && al.at("points").is_array())
                {
                    for (const auto& pt : al.at("points"))
                    {
                        lane.points.push_back(project::AutomationPoint {
                            .id = pt.at("id").get<project::AutomationPointId>(),
                            .samplePosition = pt.at("samplePosition").get<std::uint64_t>(),
                            .value = pt.at("value").get<float>(),
                            .curveTension = pt.contains("curveTension") ? pt.at("curveTension").get<float>() : 0.0f
                        });
                    }
                }
                snapshot.automationLanes.push_back(std::move(lane));
            }
        }
    }

    // Keep model invariants (including duplicate IDs and clip ranges) owned by the project layer.
    const auto loaded = project::Project::fromSnapshot(std::move(snapshot));
    if (! loaded)
        return failure(Code::invalidFormat, "Project IDs, names, assets, and clips must be valid");
    return loaded->snapshot();
}
} // namespace

std::expected<void, ProjectFileError> saveProject(
    const project::ProjectSnapshot& snapshot, const fs::path& path)
{
    try
    {
        if (! validPath(path))
            return failure(Code::invalidPath, "Expected a nonempty project file path without NUL characters");
        if (snapshot.formatVersion != 2 && snapshot.formatVersion != 3)
            return failure(Code::unsupportedVersion, "Only project format versions 2 and 3 are supported");
        if (! project::Project::fromSnapshot(snapshot))
            return failure(Code::invalidFormat, "Project IDs, names, assets, and clips must be valid");

        Json tracks = Json::array();
        for (const auto& track : snapshot.tracks)
        {
            Json trackJson {{"id", track.id}, {"name", track.name},
                            {"muted", track.muted}, {"solo", track.solo}};
            if (snapshot.formatVersion >= 3)
            {
                trackJson["gainLinear"] = track.gainLinear;
                trackJson["pan"] = track.pan;
                trackJson["phaseInvert"] = track.phaseInvert;
                trackJson["kind"] = (track.kind == project::TrackKind::instrument) ? "instrument" : "audio";
                if (! track.inserts.empty())
                {
                    Json inserts = Json::array();
                    for (const auto& ins : track.inserts)
                    {
                        std::string typeStr = "parametricEq";
                        switch (ins.type)
                        {
                        case project::DspNodeType::parametricEq: typeStr = "parametricEq"; break;
                        case project::DspNodeType::dynamicsCompressor: typeStr = "dynamicsCompressor"; break;
                        case project::DspNodeType::brickwallLimiter: typeStr = "brickwallLimiter"; break;
                        case project::DspNodeType::tnhcSynth: typeStr = "tnhcSynth"; break;
                        case project::DspNodeType::clapPlugin: typeStr = "clapPlugin"; break;
                        }
                        inserts.push_back({
                            {"slotIndex", ins.slotIndex},
                            {"type", typeStr},
                            {"bypassed", ins.bypassed},
                            {"name", ins.name},
                            {"pluginId", ins.pluginId},
                            {"parameters", ins.parameters}
                        });
                    }
                    trackJson["inserts"] = std::move(inserts);
                }
            }
            tracks.push_back(std::move(trackJson));
        }
        Json assets = Json::array();
        for (const auto& asset : snapshot.assets)
            assets.push_back({{"id", asset.id}, {"path", asset.path},
                              {"pathKind", encodePathKind(asset.pathKind)},
                              {"sampleRate", asset.sampleRate},
                              {"channelCount", asset.channelCount},
                              {"frameCount", asset.frameCount}});
        Json clips = Json::array();
        for (const auto& clip : snapshot.clips)
        {
            Json clipJson {{"id", clip.id}, {"assetId", clip.assetId},
                           {"startSample", clip.startSample},
                           {"sourceOffset", clip.sourceOffset},
                           {"lengthSamples", clip.lengthSamples}};
            if (snapshot.formatVersion >= 3)
            {
                clipJson["trackId"] = clip.trackId;
                clipJson["gainLinear"] = clip.gainLinear;
                clipJson["fadeInSamples"] = clip.fadeInSamples;
                clipJson["fadeOutSamples"] = clip.fadeOutSamples;
                clipJson["warpingEnabled"] = clip.warpingEnabled;
                if (! clip.warpMarkers.empty())
                {
                    Json markers = Json::array();
                    for (const auto& m : clip.warpMarkers)
                    {
                        markers.push_back({
                            {"id", m.id},
                            {"sourceSample", m.sourceSample},
                            {"targetSample", m.targetSample}
                        });
                    }
                    clipJson["warpMarkers"] = std::move(markers);
                }
            }
            clips.push_back(std::move(clipJson));
        }
        Json document {{"formatVersion", snapshot.formatVersion}, {"projectId", snapshot.id},
                       {"name", snapshot.name}, {"tracks", std::move(tracks)},
                       {"assets", std::move(assets)}, {"clips", std::move(clips)}};
        if (snapshot.formatVersion >= 3)
        {
            document["tempo"] = {
                {"bpm", snapshot.tempo.bpm},
                {"timeSigNumerator", snapshot.tempo.timeSigNumerator},
                {"timeSigDenominator", snapshot.tempo.timeSigDenominator},
                {"metronomeEnabled", snapshot.tempo.metronomeEnabled}
            };
            document["loop"] = {
                {"enabled", snapshot.loop.enabled},
                {"startSample", snapshot.loop.startSample},
                {"endSample", snapshot.loop.endSample}
            };
            if (! snapshot.midiClips.empty())
            {
                Json mClips = Json::array();
                for (const auto& mc : snapshot.midiClips)
                {
                    Json mcJson {
                        {"id", mc.id},
                        {"trackId", mc.trackId},
                        {"startSample", mc.startSample},
                        {"lengthSamples", mc.lengthSamples}
                    };
                    Json notesJson = Json::array();
                    for (const auto& n : mc.notes)
                    {
                        notesJson.push_back({
                            {"id", n.id},
                            {"pitch", n.pitch},
                            {"velocity", n.velocity},
                            {"startSample", n.startSample},
                            {"lengthSamples", n.lengthSamples}
                        });
                    }
                    mcJson["notes"] = std::move(notesJson);
                    mClips.push_back(std::move(mcJson));
                }
                document["midiClips"] = std::move(mClips);
            }
            if (! snapshot.automationLanes.empty())
            {
                Json aLanes = Json::array();
                for (const auto& al : snapshot.automationLanes)
                {
                    std::string kindStr = "trackVolume";
                    switch (al.target.kind)
                    {
                    case project::AutomationTargetKind::trackVolume: kindStr = "trackVolume"; break;
                    case project::AutomationTargetKind::trackPan: kindStr = "trackPan"; break;
                    case project::AutomationTargetKind::trackSend: kindStr = "trackSend"; break;
                    case project::AutomationTargetKind::dspParameter: kindStr = "dspParameter"; break;
                    }
                    Json points = Json::array();
                    for (const auto& pt : al.points)
                    {
                        points.push_back({
                            {"id", pt.id},
                            {"samplePosition", pt.samplePosition},
                            {"value", pt.value},
                            {"curveTension", pt.curveTension}
                        });
                    }
                    aLanes.push_back({
                        {"id", al.id},
                        {"target", {
                            {"kind", kindStr},
                            {"trackId", al.target.trackId},
                            {"slotIndex", al.target.slotIndex},
                            {"parameterIndex", al.target.parameterIndex}
                        }},
                        {"points", std::move(points)}
                    });
                }
                document["automationLanes"] = std::move(aLanes);
            }
        }
        // Serialize completely before creating a temporary file (e.g. invalid UTF-8).
        return writeTransaction(document.dump(2) + '\n', path);
    }
    catch (const Json::exception& error)
    {
        return failure(Code::invalidFormat, error.what());
    }
    catch (const fs::filesystem_error& error)
    {
        return failure(Code::ioFailure, error.what());
    }
}

std::expected<project::ProjectSnapshot, ProjectFileError> loadProject(const fs::path& path)
{
    try
    {
        if (! validPath(path))
            return failure(Code::invalidPath, "Expected a nonempty project file path without NUL characters");
        std::error_code error;
        if (! fs::is_regular_file(path, error) || error)
            return failure(Code::ioFailure, "Cannot read project file as a regular file");
        std::ifstream input(path, std::ios::binary);
        if (! input.is_open())
            return failure(Code::ioFailure, "Cannot open project file");
        try
        {
            // parse() consumes the whole document and rejects trailing data.
            const auto document = Json::parse(input);
            if (input.bad())
                return failure(Code::ioFailure, "Cannot read project file");
            return decode(document);
        }
        catch (const Json::exception& exception)
        {
            return failure(input.bad() ? Code::ioFailure : Code::invalidFormat, exception.what());
        }
    }
    catch (const fs::filesystem_error& error)
    {
        return failure(Code::ioFailure, error.what());
    }
    catch (const std::ios_base::failure& error)
    {
        return failure(Code::ioFailure, error.what());
    }
}

} // namespace dawg::persistence
