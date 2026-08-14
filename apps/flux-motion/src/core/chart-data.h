#pragma once

#include "layer-model.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fxm::chart {

struct Datum {
    std::string category;
    std::string series_id;
    std::string series_name;
    std::string metadata;
    double value = 0.0;
    double secondary_value = 0.0;
    bool has_secondary_value = false;
    uint32_t color = 0xFF4C8DFFu;
    uint32_t fill_color = 0x804C8DFFu;
    bool fill_enabled = true;
    bool outline_enabled = true;
    double line_thickness = -1.0;
};

struct DataSet {
    std::vector<std::string> categories;
    std::vector<ChartSeries> series;
    std::vector<Datum> values;
    uint64_t source_revision = 0;
    int64_t source_timestamp_ms = 0;
    bool source_available = false;
};

/* Cache/prerender jobs resolve the current provider snapshot without the
 * wall-clock live-update transition, while normal playback remains live. */
class ScopedDeterministicDataResolution {
public:
    ScopedDeterministicDataResolution();
    ~ScopedDeterministicDataResolution();

    ScopedDeterministicDataResolution(
        const ScopedDeterministicDataResolution &) = delete;
    ScopedDeterministicDataResolution &operator=(
        const ScopedDeterministicDataResolution &) = delete;
};

/* Provider-neutral table mapping. It reads only ExternalDataManager snapshots;
 * provider implementations and their configuration never enter this layer. */
DataSet map_layer_data(const Layer &layer);

/* Resolve the live data transition and the authored/keyframed value-transition
 * property. now_ms <= 0 selects the monotonic runtime clock. */
DataSet resolved_layer_data(const Layer &layer, double local_time,
                            int64_t now_ms = 0);

/* Stable visual identity for cache keys. Includes only mapped values and the
 * selected chart binding, not unrelated fields from the provider. */
std::string data_fingerprint(const Layer &layer);
bool transition_active(const Layer &layer, int64_t now_ms = 0);

void clear_transition_state(const std::string &layer_id = {});

} // namespace fxm::chart
