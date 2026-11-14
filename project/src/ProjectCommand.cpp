#include <dawg/project/Project.h>

#include <type_traits>

namespace dawg::project {

bool Project::apply(const ProjectCommand& command)
{
    return std::visit([this](const auto& operation) -> bool
    {
        using Operation = std::decay_t<decltype(operation)>;
        if constexpr (std::is_same_v<Operation, AddAudioTrack>)
            return addAudioTrack(operation.name) != 0;
        else if constexpr (std::is_same_v<Operation, RemoveTrack>)
            return removeTrack(operation.id);
        else if constexpr (std::is_same_v<Operation, AddAudioClip>)
            return addAudioClip(operation.clip) != 0;
        else if constexpr (std::is_same_v<Operation, SetTrackMute>)
            return setTrackMute(operation.id, operation.muted);
        else if constexpr (std::is_same_v<Operation, SetTrackSolo>)
            return setTrackSolo(operation.id, operation.solo);
        else if constexpr (std::is_same_v<Operation, SetTrackGainPan>)
            return setTrackGainPan(operation.id, operation.gainLinear, operation.pan, operation.phaseInvert);
        else
            return renameTrack(operation.id, operation.name);
    }, command);
}

} // namespace dawg::project
