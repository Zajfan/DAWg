#include <dawg/plugins/ClapPluginLoader.h>

#include <dlfcn.h>

#include <utility>

namespace dawg::plugins {

namespace {

static constexpr clap_version_t kHostVersion = CLAP_VERSION_INIT;

const clap_host g_dawgClapHost {
    .clap_version = kHostVersion,
    .host_data = nullptr,
    .name = "DAWg",
    .vendor = "The No-hands Company",
    .url = "https://github.com/Zajfan/DAWg",
    .version = "1.0.0",
    .get_extension = [](const clap_host*, const char*) noexcept -> const void* {
        return nullptr;
    },
    .request_restart = [](const clap_host*) noexcept {},
    .request_process = [](const clap_host*) noexcept {},
    .request_callback = [](const clap_host*) noexcept {}
};

} // namespace

ClapInstance::ClapInstance(void* sharedLibraryHandle,
                           const clap_plugin_entry* entry,
                           const clap_plugin* plugin) noexcept
    : m_libHandle(sharedLibraryHandle)
    , m_entry(entry)
    , m_plugin(plugin)
{
    if (m_plugin != nullptr && m_plugin->get_extension != nullptr)
    {
        m_paramsExt = static_cast<const clap_plugin_params*>(
            m_plugin->get_extension(m_plugin, CLAP_EXT_PARAMS));
    }
}

ClapInstance::~ClapInstance()
{
    if (m_plugin != nullptr)
    {
        if (m_plugin->deactivate != nullptr)
            m_plugin->deactivate(m_plugin);
        if (m_plugin->destroy != nullptr)
            m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
    }

    if (m_entry != nullptr && m_entry->deinit != nullptr)
    {
        m_entry->deinit();
        m_entry = nullptr;
    }

    if (m_libHandle != nullptr)
    {
        dlclose(m_libHandle);
        m_libHandle = nullptr;
    }
}

ClapInstance::ClapInstance(ClapInstance&& other) noexcept
    : m_libHandle(std::exchange(other.m_libHandle, nullptr))
    , m_entry(std::exchange(other.m_entry, nullptr))
    , m_plugin(std::exchange(other.m_plugin, nullptr))
    , m_paramsExt(std::exchange(other.m_paramsExt, nullptr))
    , m_crashed(other.m_crashed)
{
}

ClapInstance& ClapInstance::operator=(ClapInstance&& other) noexcept
{
    if (this != &other)
    {
        if (m_plugin != nullptr)
        {
            if (m_plugin->deactivate != nullptr)
                m_plugin->deactivate(m_plugin);
            if (m_plugin->destroy != nullptr)
                m_plugin->destroy(m_plugin);
        }
        if (m_entry != nullptr && m_entry->deinit != nullptr)
            m_entry->deinit();
        if (m_libHandle != nullptr)
            dlclose(m_libHandle);

        m_libHandle = std::exchange(other.m_libHandle, nullptr);
        m_entry = std::exchange(other.m_entry, nullptr);
        m_plugin = std::exchange(other.m_plugin, nullptr);
        m_paramsExt = std::exchange(other.m_paramsExt, nullptr);
        m_crashed = other.m_crashed;
    }
    return *this;
}

std::string ClapInstance::pluginName() const
{
    if (m_plugin != nullptr && m_plugin->desc != nullptr && m_plugin->desc->name != nullptr)
        return m_plugin->desc->name;
    return "Unknown Plugin";
}

std::string ClapInstance::pluginVendor() const
{
    if (m_plugin != nullptr && m_plugin->desc != nullptr && m_plugin->desc->vendor != nullptr)
        return m_plugin->desc->vendor;
    return "Unknown Vendor";
}

std::string ClapInstance::pluginVersion() const
{
    if (m_plugin != nullptr && m_plugin->desc != nullptr && m_plugin->desc->version != nullptr)
        return m_plugin->desc->version;
    return "1.0.0";
}

std::string ClapInstance::pluginId() const
{
    if (m_plugin != nullptr && m_plugin->desc != nullptr && m_plugin->desc->id != nullptr)
        return m_plugin->desc->id;
    return "";
}

std::vector<ClapParamDescriptor> ClapInstance::getParameters() const
{
    std::vector<ClapParamDescriptor> params;
    if (m_plugin == nullptr || m_paramsExt == nullptr || m_paramsExt->count == nullptr || m_paramsExt->get_info == nullptr)
        return params;

    const auto count = m_paramsExt->count(m_plugin);
    params.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i)
    {
        clap_param_info info {};
        if (m_paramsExt->get_info(m_plugin, i, &info))
        {
            double currentVal = info.default_value;
            if (m_paramsExt->get_value != nullptr)
                (void) m_paramsExt->get_value(m_plugin, info.id, &currentVal);

            params.push_back(ClapParamDescriptor {
                .id = info.id,
                .name = info.name,
                .module = info.module,
                .minValue = info.min_value,
                .maxValue = info.max_value,
                .defaultValue = info.default_value,
                .currentValue = currentVal
            });
        }
    }

    return params;
}

bool ClapInstance::getParameterValue(const std::uint32_t paramId, double& outValue) const
{
    if (m_plugin == nullptr || m_paramsExt == nullptr || m_paramsExt->get_value == nullptr)
        return false;
    return m_paramsExt->get_value(m_plugin, paramId, &outValue);
}

bool ClapInstance::setParameterValue(const std::uint32_t /*paramId*/, const double /*value*/)
{
    if (m_plugin == nullptr)
        return false;
    return true;
}

bool ClapInstance::restart(const double sampleRate, const std::size_t maxBlockSize)
{
    if (m_plugin == nullptr)
        return false;

    if (m_plugin->stop_processing != nullptr)
        m_plugin->stop_processing(m_plugin);
    if (m_plugin->deactivate != nullptr)
        m_plugin->deactivate(m_plugin);
    if (m_plugin->reset != nullptr)
        m_plugin->reset(m_plugin);

    if (m_plugin->activate != nullptr)
    {
        if (! m_plugin->activate(m_plugin, sampleRate, 1, static_cast<std::uint32_t>(maxBlockSize)))
            return false;
    }

    if (m_plugin->start_processing != nullptr)
    {
        if (! m_plugin->start_processing(m_plugin))
            return false;
    }

    m_crashed = false;
    return true;
}

ClapLoadResult
ClapPluginLoader::loadPlugin(const std::string& libraryPath, const std::string& pluginId)
{
    if (libraryPath.empty())
        return ClapLoadResult {nullptr, "Empty plugin library path"};

    void* handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        const char* err = dlerror();
        return ClapLoadResult {nullptr, err != nullptr ? err : "Failed to open shared library"};
    }

    const auto* entry = static_cast<const clap_plugin_entry*>(dlsym(handle, "clap_entry"));
    if (entry == nullptr)
    {
        dlclose(handle);
        return ClapLoadResult {nullptr, "Symbol 'clap_entry' not found in plugin library"};
    }

    if (entry->init != nullptr && ! entry->init(libraryPath.c_str()))
    {
        dlclose(handle);
        return ClapLoadResult {nullptr, "clap_plugin_entry::init failed"};
    }

    if (entry->get_factory == nullptr)
    {
        if (entry->deinit != nullptr)
            entry->deinit();
        dlclose(handle);
        return ClapLoadResult {nullptr, "clap_plugin_entry::get_factory is null"};
    }

    const auto* factory = static_cast<const clap_plugin_factory*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || factory->create_plugin == nullptr)
    {
        if (entry->deinit != nullptr)
            entry->deinit();
        dlclose(handle);
        return ClapLoadResult {nullptr, "CLAP plugin factory not found"};
    }

    std::string targetId = pluginId;
    if (targetId.empty())
    {
        if (factory->get_plugin_count == nullptr || factory->get_plugin_descriptor == nullptr)
        {
            if (entry->deinit != nullptr)
                entry->deinit();
            dlclose(handle);
            return ClapLoadResult {nullptr, "Cannot enumerate plugin descriptors in factory"};
        }

        const auto count = factory->get_plugin_count(factory);
        if (count == 0)
        {
            if (entry->deinit != nullptr)
                entry->deinit();
            dlclose(handle);
            return ClapLoadResult {nullptr, "Factory contains zero plugins"};
        }

        const auto* desc = factory->get_plugin_descriptor(factory, 0);
        if (desc == nullptr || desc->id == nullptr)
        {
            if (entry->deinit != nullptr)
                entry->deinit();
            dlclose(handle);
            return ClapLoadResult {nullptr, "Invalid plugin descriptor at index 0"};
        }
        targetId = desc->id;
    }

    const auto* plugin = factory->create_plugin(factory, &g_dawgClapHost, targetId.c_str());
    if (plugin == nullptr)
    {
        if (entry->deinit != nullptr)
            entry->deinit();
        dlclose(handle);
        return ClapLoadResult {nullptr, "Failed to instantiate CLAP plugin with id: " + targetId};
    }

    if (plugin->init != nullptr && ! plugin->init(plugin))
    {
        if (plugin->destroy != nullptr)
            plugin->destroy(plugin);
        if (entry->deinit != nullptr)
            entry->deinit();
        dlclose(handle);
        return ClapLoadResult {nullptr, "clap_plugin::init failed"};
    }

    return ClapLoadResult {std::make_unique<ClapInstance>(handle, entry, plugin), ""};
}

} // namespace dawg::plugins
