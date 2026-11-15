#pragma once

#include <dawg/engine/MidiControllerManager.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace dawg::ui {

class MidiLearnManager final
{
public:
    static MidiLearnManager& instance() noexcept
    {
        static MidiLearnManager s_instance;
        return s_instance;
    }

    using ParamCallback = std::function<void(float value)>;

    void registerParameter(std::uint32_t targetId, std::string name, ParamCallback cb)
    {
        m_callbacks[targetId] = std::move(cb);
        m_paramNames[targetId] = std::move(name);
    }

    void unregisterParameter(std::uint32_t targetId)
    {
        m_callbacks.erase(targetId);
        m_paramNames.erase(targetId);
    }

    void startLearning(std::uint32_t targetId, engine::MidiControllerManager* controllerManager)
    {
        m_learningTargetId = targetId;
        m_activeControllerManager = controllerManager;
        if (m_activeControllerManager != nullptr)
        {
            m_activeControllerManager->startLearn(targetId, 0.0f, 1.0f);
        }
    }

    void cancelLearning()
    {
        if (m_activeControllerManager != nullptr)
        {
            m_activeControllerManager->cancelLearn();
        }
        m_learningTargetId = 0;
        m_activeControllerManager = nullptr;
    }

    [[nodiscard]] bool isLearning() const noexcept
    {
        return m_learningTargetId != 0;
    }

    [[nodiscard]] std::string learningParamName() const
    {
        auto it = m_paramNames.find(m_learningTargetId);
        return it != m_paramNames.end() ? it->second : "Parameter";
    }

    void dispatchControl(const engine::ProcessedMidiControl& control)
    {
        if (control.learnedNewBinding)
        {
            m_learningTargetId = 0;
        }

        if (control.learnResult.triggered)
        {
            auto it = m_callbacks.find(control.learnResult.targetId);
            if (it != m_callbacks.end() && it->second)
            {
                it->second(control.learnResult.value);
            }
        }
    }

private:
    MidiLearnManager() = default;

    std::uint32_t m_learningTargetId {0};
    engine::MidiControllerManager* m_activeControllerManager {nullptr};
    std::unordered_map<std::uint32_t, ParamCallback> m_callbacks;
    std::unordered_map<std::uint32_t, std::string> m_paramNames;
};

} // namespace dawg::ui
