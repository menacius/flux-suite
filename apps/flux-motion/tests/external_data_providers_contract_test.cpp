#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require(const std::string &text, const std::string &needle,
             const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing External Data Providers contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle,
                    const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Forbidden External Data Providers behavior: " << label
              << " (" << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 11) {
        std::cerr << "usage: external_data_providers_contract_test <types> <provider-h> "
                     "<provider-cpp> <json-path-h> <manager-cpp> <title-data-cpp> <dialog-cpp> "
                     "<dock> <plugin-main> <cmake>\n";
        return 2;
    }

    const std::string types = read_file(argv[1]);
    const std::string provider_h = read_file(argv[2]);
    const std::string provider_cpp = read_file(argv[3]);
    const std::string json_path_h = read_file(argv[4]);
    const std::string manager_cpp = read_file(argv[5]);
    const std::string title_data_cpp = read_file(argv[6]);
    const std::string dialog_cpp = read_file(argv[7]);
    const std::string dock = read_file(argv[8]);
    const std::string plugin_main = read_file(argv[9]);
    const std::string cmake = read_file(argv[10]);

    bool ok = true;
    ok &= require(types, "enum class ExternalDataProviderType", "provider type enum");
    ok &= require(types, "JsonFile = 1", "JSON file provider");
    ok &= require(types, "CsvFile = 2", "CSV file provider");
    ok &= require(types, "HttpJson = 3", "HTTP JSON provider");
    ok &= require(types, "WebSocket = 4", "WebSocket provider");
    ok &= require(types, "LocalTextFile = 5", "local text provider");
    ok &= require(types, "ManualTable = 6", "manual table provider");
    ok &= require(types, "Updating = 4", "updating state");
    ok &= require(types, "Stale = 5", "stale state");
    ok &= require(types, "polling_interval_ms", "optional polling interval");
    ok &= require(types, "keep_last_value", "last-known-value option");
    ok &= require(types, "headers", "HTTP headers");
    ok &= require(types, "authentication_token", "authentication token");
    ok &= require(types, "csv_row_index", "CSV row selection");
    ok &= require(types, "csv_column_mapping", "CSV column mapping");
    ok &= require(types, "manual_values", "manual/internal values");

    ok &= require(provider_h, "class IExternalDataProvider", "common provider interface");
    ok &= require(provider_h, "connect_provider", "connect operation");
    ok &= require(provider_h, "disconnect_provider", "disconnect operation");
    ok &= require(provider_h, "refresh", "refresh operation");
    ok &= require(provider_h, "validate", "provider validation");
    ok &= require(provider_h, "error_message", "error reporting");
    ok &= require(provider_h, "class ExternalDataProviderService", "provider service");

    ok &= require(provider_cpp, "class JsonFileProvider", "JSON implementation");
    ok &= require(provider_cpp, "class CsvFileProvider", "CSV implementation");
    ok &= require(provider_cpp, "class HttpJsonProvider", "HTTP implementation");
    ok &= require(provider_cpp, "class WebSocketProvider", "WebSocket implementation");
    ok &= require(provider_cpp, "class LocalTextFileProvider", "text implementation");
    ok &= require(provider_cpp, "class ManualTableProvider", "manual implementation");
    ok &= require(provider_cpp, "resolve_json_path", "nested JSON path resolution");
    ok &= require(provider_cpp, "Discovery is always performed", "JSON discovery with schema overrides");
    ok &= require(provider_cpp, "Native columns are always discovered first", "CSV discovery with mappings");
    ok &= require(json_path_h, "token.index", "JSON array indexing");
    ok &= require(json_path_h, "Array indexes must be non-negative integers",
                  "JSON index validation");
    ok &= require(provider_cpp, "csv_first_row_headers", "CSV header support");
    ok &= require(provider_cpp, "QNetworkAccessManager", "asynchronous HTTP transport");
    ok &= require(provider_cpp, "timeout_timer_", "HTTP timeout");
    ok &= require(provider_cpp, "retry_or_fail", "HTTP retry policy");
    ok &= require(provider_cpp, "schedule_reconnect", "WebSocket automatic reconnect");
    ok &= require(provider_cpp, "QThread thread", "dedicated provider worker thread");
    ok &= require(provider_cpp, "Qt::QueuedConnection", "non-blocking cross-thread dispatch");
    ok &= require(provider_cpp, "pending_values_", "provider update coalescing");
    ok &= require(provider_cpp, "rate_limit_ms", "provider rate limiting");
    ok &= require(provider_cpp, "known_source_ids_", "provider-neutral source cleanup");
    ok &= require(provider_cpp, "state() != ExternalDataConnectionState::Error",
                  "WebSocket preserves actionable errors during reconnect");
    ok &= require(provider_cpp, "std::mutex mutex", "thread-safe service shutdown/dispatch");
    ok &= require_absent(provider_cpp, "obs_enter_graphics", "provider cannot enter render context");

    ok &= require(manager_cpp, "state_allows_current_value", "last-known resolution policy");
    ok &= require(manager_cpp, "ExternalDataConnectionState::Stale", "stale values supported");
    ok &= require(title_data_cpp, "external_provider_to_json", "provider serialization");
    ok &= require(title_data_cpp, "external_provider_from_json", "provider deserialization");
    ok &= require(title_data_cpp, "ExternalDataProviderService::instance().synchronize",
                  "title store synchronizes providers");
    ok &= require(dialog_cpp, "External Data Providers", "provider settings UI");
    ok &= require(dialog_cpp, "Runtime state", "state UI");
    ok &= require(dialog_cpp, "error_value_", "error UI");
    ok &= require(dialog_cpp, "Bindings", "provider-to-layer binding UI");
    ok &= require(dialog_cpp, "save_bindings_for_source", "binding validation/save path");
    ok &= require(dialog_cpp, "external_bindings.push_back", "bindings applied to layers");
    ok &= require(dialog_cpp, "can only have one external-data binding",
                  "duplicate property bindings rejected");
    ok &= require(dock, "refresh_source", "dock refresh invokes providers");
    ok &= require(plugin_main, "ExternalDataProviderService::instance().shutdown",
                  "provider thread shutdown");
    ok &= require(cmake, "Qt${QT_VERSION_MAJOR}::Network", "Qt Network linked");
    ok &= require(cmake, "Qt${QT_VERSION_MAJOR}::WebSockets", "Qt WebSockets linked");
    ok &= require(cmake, "src/core/external-data-provider.cpp", "provider runtime built");
    ok &= require(cmake, "external_data_providers_contract_test", "provider audit registered");

    return ok ? 0 : 1;
}
