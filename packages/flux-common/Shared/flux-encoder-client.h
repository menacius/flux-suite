#pragma once

#include <QJsonObject>
#include <QString>

namespace fxm::encoder {

/* Stable Flux Suite local-IPC contract. Messages are UTF-8 JSON objects,
 * delimited by a newline, and responses echo request_id. Keeping the protocol
 * name and major version explicit lets future Motion and Encoder releases
 * negotiate new queue/batch commands without coupling their UI code. */
inline constexpr const char *kProtocolName = "com.fluxsuite.encoder.ipc";
inline constexpr int kProtocolVersion = 1;
inline constexpr const char *kLocalServerName =
    "com.fluxsuite.encoder.ipc.v1";

struct Availability {
    bool service_running = false;
    QString executable_path;

    bool available() const noexcept
    {
        return service_running || !executable_path.isEmpty();
    }
};

struct ExportRequest {
    QString project_id;
    QString project_name;
    QString project_store_path;
    QString project_scope;
};

class FluxEncoderClient {
public:
    static FluxEncoderClient &instance();

    /* Re-probes the service as well as installation locations. It is cheap
     * enough to call at startup and whenever the File menu is opened. */
    Availability refreshAvailability();
    Availability availability() const;

    /* Temporary Editor preference override. The key intentionally matches the
     * deployment environment variable so it can be removed without changing
     * the discovery contract used by installers and automation. */
    static QString configuredExecutablePath();
    static void setConfiguredExecutablePath(const QString &path);

    /* Reuses a running Encoder, or starts the discovered executable and waits
     * for its IPC endpoint. On success Encoder has acknowledged export.open. */
    bool openExport(const ExportRequest &request, QString *error = nullptr);

    /* Public for protocol conformance tests and future non-UI clients. */
    static QJsonObject buildOpenExportRequest(const ExportRequest &request,
                                              const QString &request_id);

private:
    FluxEncoderClient() = default;

    Availability availability_;
};

} // namespace fxm::encoder
