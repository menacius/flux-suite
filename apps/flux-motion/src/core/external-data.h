#pragma once

#include "external-data-types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct Layer;
struct Title;

namespace ExternalPropertyPaths {
inline constexpr const char *TextContent = "text.content";
inline constexpr const char *ImagePath = "image.path";
}

struct ExternalDataFieldState {
    ExternalDataFieldDefinition definition;
    ExternalDataValue current_value;
    bool has_current_value = false;
    bool authored_definition = false;
    int64_t last_update_timestamp_ms = 0;
};

struct ExternalDataSourceState {
    ExternalDataSourceDefinition definition;
    ExternalDataConnectionState connection_state = ExternalDataConnectionState::Disconnected;
    std::string error_message;
    int64_t last_update_timestamp_ms = 0;
    std::map<std::string, ExternalDataFieldState> fields;
    std::map<std::string, ExternalDataTableSnapshot> tables;
};

struct ExternalDataUpdate {
    uint64_t sequence = 0;
    std::string source_id;
    std::string field_path;
    bool connection_state_changed = false;
    bool table_changed = false;
    std::string table_path;
    int64_t timestamp_ms = 0;
};

enum class ExternalDataValueOrigin {
    Authored = 0,
    LiveExternal = 1,
    BindingFallback = 2,
    FieldDefault = 3,
};

struct ExternalDataResolution {
    ExternalDataValue value;
    ExternalDataValueOrigin origin = ExternalDataValueOrigin::Authored;
};

class ExternalDataManager {
public:
    using ChangeCallback = std::function<void(const ExternalDataUpdate &)>;

    static ExternalDataManager &instance();

    void register_source(const ExternalDataSourceDefinition &definition);
    /* Apply a complete, already-merged schema snapshot. Unlike register_source(),
     * this also releases authored overrides that are no longer present while
     * preserving discovered current values. */
    void synchronize_source_definition(const ExternalDataSourceDefinition &definition);
    void register_title_sources(const Title &title);
    void unregister_source(const std::string &source_id);

    bool set_connection_state(const std::string &source_id,
                              ExternalDataConnectionState state,
                              const std::string &error_message = {},
                              int64_t timestamp_ms = 0);
    bool update_value(const std::string &source_id,
                      const std::string &field_path,
                      const ExternalDataValue &value,
                      int64_t timestamp_ms = 0);
    bool clear_value(const std::string &source_id,
                     const std::string &field_path,
                     int64_t timestamp_ms = 0);
    bool update_table(const std::string &source_id,
                      ExternalDataTableSnapshot table,
                      int64_t timestamp_ms = 0);
    bool synchronize_tables(
        const std::string &source_id,
        std::map<std::string, ExternalDataTableSnapshot> tables,
        int64_t timestamp_ms = 0);
    std::vector<ExternalDataTableSnapshot> table_snapshots(
        const std::string &source_id) const;
    ExternalDataTableSnapshot table_snapshot(const std::string &source_id,
                                              const std::string &table_path) const;

    /* Provider-free acceptance/testing path. Creates the source/field lazily,
     * marks it connected and submits the value through the same update queue. */
    bool update_mock_value(const std::string &source_id,
                           const std::string &field_path,
                           const ExternalDataValue &value,
                           int64_t timestamp_ms = 0);

    ExternalDataSourceState source_state(const std::string &source_id) const;
    std::vector<ExternalDataSourceState> source_states() const;

    uint64_t on_change(ChangeCallback callback);
    void remove_change_callback(uint64_t callback_id);

    /* MPSC provider queue consumed by the OBS render thread. Repeated changes
     * to the same field are coalesced to the newest update. */
    std::vector<ExternalDataUpdate> take_render_updates();
    uint64_t revision() const { return revision_.load(std::memory_order_acquire); }

    ExternalDataResolution resolve(const ExternalPropertyBinding &binding,
                                   const ExternalDataValue &authored_value) const;
    ExternalDataValue resolve_value(const ExternalPropertyBinding &binding,
                                    const ExternalDataValue &authored_value,
                                    bool *using_live_value = nullptr,
                                    bool *using_fallback_value = nullptr) const;

private:
    ExternalDataManager() = default;
    ExternalDataManager(const ExternalDataManager &) = delete;
    ExternalDataManager &operator=(const ExternalDataManager &) = delete;

    struct Observer {
        uint64_t id = 0;
        ChangeCallback callback;
    };

    static int64_t now_ms();
    ExternalDataSourceState &ensure_source_locked(const std::string &source_id);
    void publish_change(ExternalDataUpdate update);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ExternalDataSourceState> sources_;
    std::vector<Observer> observers_;
    std::vector<ExternalDataUpdate> render_queue_;
    std::unordered_map<std::string, size_t> render_queue_index_;
    uint64_t next_observer_id_ = 1;
    std::atomic<uint64_t> revision_ { 0 };
    std::atomic<uint64_t> next_sequence_ { 1 };
};

/* Returns the union of runtime-discovered fields and authored schema overrides.
 * Authored fields win by path so aliases, type corrections and defaults remain
 * stable while newly discovered provider fields are immediately bindable. */
std::vector<ExternalDataFieldDefinition> available_external_data_fields(
    const ExternalDataSourceDefinition &source);
ExternalDataType external_data_field_type(
    const ExternalDataSourceDefinition &source, const std::string &field_path);

/* Persist a runtime-discovered field as an authored schema entry. Binding paths
 * call this automatically so they remain selectable while the provider is
 * offline or temporarily returns a response without that field. */
bool pin_external_data_field(ExternalDataSourceDefinition &source,
                             const std::string &field_path);
bool pin_external_data_field(Title &title, const std::string &source_id,
                             const std::string &field_path);

const ExternalPropertyBinding *external_binding_for_property(
    const Layer &layer, const std::string &property_path);
void set_external_binding(Layer &layer, ExternalPropertyBinding binding);
bool remove_external_binding(Layer &layer, const std::string &property_path);
bool layer_has_external_binding(const Layer &layer,
                                const std::string &property_path = {});
bool title_uses_external_update(const Title &title,
                                const ExternalDataUpdate &update);

std::string external_data_value_to_string(const ExternalDataValue &value);
bool external_data_value_is_empty(const ExternalDataValue &value);
std::string format_external_data_value(const ExternalDataValue &value,
                                       const std::string &formatter);
std::string format_external_data_value(const ExternalDataValue &value,
                                       const ExternalDataFormatterConfig &formatter,
                                       const std::string &legacy_formatter = {});
std::string effective_external_string(const Layer &layer,
                                      const std::string &property_path,
                                      const std::string &authored_value,
                                      bool *using_live_value = nullptr,
                                      bool *using_fallback_value = nullptr);
double effective_external_number(const Layer &layer,
                                 const std::string &property_path,
                                 double authored_value);
bool effective_external_boolean(const Layer &layer,
                                const std::string &property_path,
                                bool authored_value);
uint32_t effective_external_color(const Layer &layer,
                                  const std::string &property_path,
                                  uint32_t authored_value);

const LiveTextExternalBinding *live_text_external_binding_for_cell(
    const Title &title, const std::string &row_id, const std::string &layer_id);
LiveTextCueCellState live_text_cue_cell_state(
    const Title &title, const std::string &row_id, const std::string &layer_id);
void set_live_text_external_binding(Title &title, LiveTextExternalBinding binding);
bool remove_live_text_external_binding(Title &title, const std::string &row_id,
                                       const std::string &layer_id);
bool detach_live_text_table_cell(Title &title, const std::string &row_id,
                                 const std::string &layer_id);
bool restore_live_text_table_cell(Title &title, const std::string &row_id,
                                  const std::string &layer_id);
std::string effective_live_text_cue_value(const Title &title, int row,
                                          const Layer &layer,
                                          const std::string &authored_value,
                                          bool *using_live_value = nullptr,
                                          bool *using_fallback_value = nullptr);
void apply_live_text_runtime_binding(Title &title, int row, Layer &layer);

/* Materialize source-managed table rows as normal cue rows and cell bindings.
 * Returns true only when the row structure or generated bindings changed. */
bool synchronize_live_text_table_bindings(Title &title,
                                           const std::string &source_id = {});
bool remove_live_text_table_binding(Title &title, const std::string &binding_id,
                                    bool remove_managed_rows = true);
