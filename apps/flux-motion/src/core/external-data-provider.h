#pragma once

#include "external-data-types.h"

#include <memory>
#include <string>
#include <vector>

struct ExternalDataProviderValidation {
    bool valid = true;
    std::string error_message;
};

class IExternalDataProvider {
public:
    virtual ~IExternalDataProvider() = default;

    virtual std::string source_id() const = 0;
    virtual ExternalDataProviderType provider_type() const = 0;
    virtual ExternalDataProviderValidation validate() const = 0;
    virtual ExternalDataConnectionState state() const = 0;
    virtual std::string error_message() const = 0;

    virtual void connect_provider() = 0;
    virtual void disconnect_provider() = 0;
    virtual void refresh() = 0;
    virtual void reconfigure(const ExternalDataSourceDefinition &definition) = 0;
};

/* Process-wide provider controller. Every provider object, timer, file read,
 * QNetworkAccessManager and QWebSocket lives on a dedicated worker thread.
 * Calls are safe from the UI, source and render threads and are always queued. */
class ExternalDataProviderService {
public:
    static ExternalDataProviderService &instance();

    void synchronize(const std::vector<ExternalDataSourceDefinition> &definitions);
    void configure_source(const ExternalDataSourceDefinition &definition);
    void connect_source(const std::string &source_id);
    void disconnect_source(const std::string &source_id);
    void refresh_source(const std::string &source_id);
    void refresh_all();
    void shutdown();

private:
    ExternalDataProviderService();
    ~ExternalDataProviderService();
    ExternalDataProviderService(const ExternalDataProviderService &) = delete;
    ExternalDataProviderService &operator=(const ExternalDataProviderService &) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

const char *external_data_provider_type_name(ExternalDataProviderType type);
const char *external_data_connection_state_name(ExternalDataConnectionState state);
