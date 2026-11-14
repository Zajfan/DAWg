#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// CLAP 1.2 core definitions
struct clap_version
{
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t revision;
};

static constexpr clap_version CLAP_VERSION_INIT = {1, 2, 0};

struct clap_plugin_descriptor
{
    clap_version clap_version;
    const char* id;
    const char* name;
    const char* vendor;
    const char* url;
    const char* manual_url;
    const char* support_url;
    const char* version;
    const char* description;
    const char** features;
};

struct clap_audio_buffer
{
    float** data32;
    double** data64;
    std::uint32_t channel_count;
    std::uint32_t latency;
    std::uint64_t constant_mask;
};

enum clap_process_status
{
    CLAP_PROCESS_ERROR = 0,
    CLAP_PROCESS_CONTINUE = 1,
    CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
    CLAP_PROCESS_TAIL = 3,
    CLAP_PROCESS_SLEEP = 4,
};

struct clap_event_header
{
    std::size_t size;
    std::uint32_t time;
    std::uint16_t space_id;
    std::uint16_t type;
    std::uint32_t flags;
};

static constexpr std::uint16_t CLAP_CORE_EVENT_SPACE_ID = 0;

enum clap_event_type
{
    CLAP_EVENT_NOTE_ON = 0,
    CLAP_EVENT_NOTE_OFF = 1,
    CLAP_EVENT_NOTE_CHOKE = 2,
    CLAP_EVENT_NOTE_END = 3,
    CLAP_EVENT_NOTE_EXPRESSION = 4,
    CLAP_EVENT_PARAM_VALUE = 5,
    CLAP_EVENT_PARAM_MOD = 6,
    CLAP_EVENT_PARAM_GESTURE_BEGIN = 7,
    CLAP_EVENT_PARAM_GESTURE_END = 8,
    CLAP_EVENT_TRANSPORT = 9,
    CLAP_EVENT_MIDI = 10,
    CLAP_EVENT_MIDI_SYSEX = 11,
    CLAP_EVENT_MIDI2 = 12,
};

struct clap_event_note
{
    clap_event_header header;
    std::int32_t note_id;
    std::int16_t port_index;
    std::int16_t channel;
    std::int16_t key;
    double velocity;
};

struct clap_event_param_value
{
    clap_event_header header;
    std::uint32_t param_id;
    void* cookie;
    std::int32_t note_id;
    std::int16_t port_index;
    std::int16_t channel;
    std::int16_t key;
    double value;
};

struct clap_event_midi
{
    clap_event_header header;
    std::uint16_t port_index;
    std::uint8_t data[3];
};

struct clap_input_events
{
    void* ctx;
    std::uint32_t (*size)(const clap_input_events* list);
    const clap_event_header* (*get)(const clap_input_events* list, std::uint32_t index);
};

struct clap_output_events
{
    void* ctx;
    bool (*try_push)(const clap_output_events* list, const clap_event_header* event);
};

struct clap_process
{
    std::int64_t steady_time;
    std::uint32_t frames_count;
    const void* transport;
    const clap_audio_buffer* audio_inputs;
    clap_audio_buffer* audio_outputs;
    std::uint32_t audio_inputs_count;
    std::uint32_t audio_outputs_count;
    const clap_input_events* in_events;
    const clap_output_events* out_events;
};

struct clap_host
{
    clap_version clap_version;
    void* host_data;
    const char* name;
    const char* vendor;
    const char* url;
    const char* version;
    const void* (*get_extension)(const clap_host* host, const char* extension_id);
    void (*request_restart)(const clap_host* host);
    void (*request_process)(const clap_host* host);
    void (*request_callback)(const clap_host* host);
};

struct clap_plugin
{
    const clap_plugin_descriptor* desc;
    void* plugin_data;
    bool (*init)(const clap_plugin* plugin);
    void (*destroy)(const clap_plugin* plugin);
    bool (*activate)(const clap_plugin* plugin, double sample_rate, std::uint32_t min_frames_count, std::uint32_t max_frames_count);
    void (*deactivate)(const clap_plugin* plugin);
    bool (*start_processing)(const clap_plugin* plugin);
    void (*stop_processing)(const clap_plugin* plugin);
    void (*reset)(const clap_plugin* plugin);
    clap_process_status (*process)(const clap_plugin* plugin, const clap_process* process);
    const void* (*get_extension)(const clap_plugin* plugin, const char* id);
    void (*on_main_thread)(const clap_plugin* plugin);
};

// Extension: Latency
static constexpr const char* CLAP_EXT_LATENCY = "clap.latency";

struct clap_plugin_latency
{
    std::uint32_t (*get)(const clap_plugin* plugin);
};

struct clap_host_latency
{
    void (*changed)(const clap_host* host);
};

// Extension: Parameters
static constexpr const char* CLAP_EXT_PARAMS = "clap.params";

struct clap_param_info
{
    std::uint32_t id;
    std::uint32_t flags;
    void* cookie;
    char name[256];
    char module[1024];
    double min_value;
    double max_value;
    double default_value;
};

struct clap_plugin_params
{
    std::uint32_t (*count)(const clap_plugin* plugin);
    bool (*get_info)(const clap_plugin* plugin, std::uint32_t param_index, clap_param_info* param_info);
    bool (*get_value)(const clap_plugin* plugin, std::uint32_t param_id, double* value);
    bool (*value_to_text)(const clap_plugin* plugin, std::uint32_t param_id, double value, char* display, std::uint32_t size);
    bool (*text_to_value)(const clap_plugin* plugin, std::uint32_t param_id, const char* display, double* value);
    void (*flush)(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out);
};

// Entry factory
static constexpr const char* CLAP_PLUGIN_FACTORY_ID = "clap.plugin-factory";

struct clap_plugin_factory
{
    std::uint32_t (*get_plugin_count)(const clap_plugin_factory* factory);
    const clap_plugin_descriptor* (*get_plugin_descriptor)(const clap_plugin_factory* factory, std::uint32_t index);
    const clap_plugin* (*create_plugin)(const clap_plugin_factory* factory, const clap_host* host, const char* plugin_id);
};

struct clap_plugin_entry
{
    clap_version clap_version;
    bool (*init)(const char* plugin_path);
    void (*deinit)(void);
    const void* (*get_factory)(const char* factory_id);
};

#ifdef __cplusplus
}
#endif
