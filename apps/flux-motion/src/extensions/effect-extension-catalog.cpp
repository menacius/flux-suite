#include "effect-extension-catalog.h"
#include "fxm-plugin-api.h"
#include "effects/effect-runtime.h"
#include "asset-path-provider.h"
#include "host-context-provider.h"
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>

#include <algorithm>
#include <future>
#include <exception>

namespace {

QString executionSpaceName(LayerEffectSpace space)
{
    switch (space) {
    case LayerEffectSpace::LayerSpace: return QStringLiteral("layer");
    case LayerEffectSpace::PostTransform: return QStringLiteral("post-transform");
    case LayerEffectSpace::ScreenSpace: return QStringLiteral("screen");
    }
    return QStringLiteral("layer");
}

QString backendName(EffectExecutionBackend backend)
{
    switch (backend) {
    case EffectExecutionBackend::Gpu: return QStringLiteral("gpu");
    case EffectExecutionBackend::Cpu: return QStringLiteral("cpu");
    case EffectExecutionBackend::Hybrid: return QStringLiteral("hybrid");
    }
    return QStringLiteral("gpu");
}

QString colorContractName(EffectColorContract contract)
{
    switch (contract) {
    case EffectColorContract::PreserveInput: return QStringLiteral("preserve-input");
    case EffectColorContract::LinearLight: return QStringLiteral("linear-light");
    case EffectColorContract::DisplayReferred: return QStringLiteral("display-referred");
    }
    return QStringLiteral("preserve-input");
}

QString alphaContractName(EffectAlphaContract contract)
{
    switch (contract) {
    case EffectAlphaContract::PremultipliedPreserve: return QStringLiteral("premultiplied-preserve");
    case EffectAlphaContract::PremultipliedExpand: return QStringLiteral("premultiplied-expand");
    case EffectAlphaContract::PremultipliedReplace: return QStringLiteral("premultiplied-replace");
    }
    return QStringLiteral("premultiplied-preserve");
}

QString parameterKindName(EffectParameterKind kind)
{
    switch (kind) {
    case EffectParameterKind::Boolean: return QStringLiteral("bool");
    case EffectParameterKind::Integer: return QStringLiteral("int");
    case EffectParameterKind::Scalar: return QStringLiteral("float");
    case EffectParameterKind::Angle: return QStringLiteral("angle");
    case EffectParameterKind::Color: return QStringLiteral("color");
    case EffectParameterKind::Enumeration: return QStringLiteral("enum");
    case EffectParameterKind::Point: return QStringLiteral("point");
    }
    return QStringLiteral("float");
}

QString parameterLabel(const char *path)
{
    QString label = QString::fromUtf8(path ? path : "");
    if (label.startsWith(QStringLiteral("effect_")))
        label.remove(0, 7);
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

void appendBuiltIns(std::vector<FxmEffectExtensionDefinition> &effects)
{
    for (const auto &meta : builtin_effect_descriptors()) {
        FxmEffectExtensionDefinition def;
        def.id = QString::fromUtf8(meta.stable_id);
        def.displayName = QString::fromUtf8(meta.display_name);
        def.category = QString::fromUtf8(meta.category);
        def.providerId = QStringLiteral("fxm.core");
        def.providerVersion = QStringLiteral(PLUGIN_VERSION);
        def.builtIn = true;
        def.builtInType = meta.type;
        def.schemaVersion = meta.schema_version;
        def.capabilities = QJsonObject{
            {QStringLiteral("executionSpace"), executionSpaceName(meta.execution_space)},
            {QStringLiteral("backend"), backendName(meta.backend)},
            {QStringLiteral("colorContract"), colorContractName(meta.color_contract)},
            {QStringLiteral("alphaContract"), alphaContractName(meta.alpha_contract)},
            {QStringLiteral("minimumRenderPasses"), static_cast<int>(meta.minimum_render_passes)},
            {QStringLiteral("supportsHdr"), meta.supports_hdr},
            {QStringLiteral("expandsBounds"), meta.expands_bounds},
            {QStringLiteral("cacheableWhenStatic"), meta.cacheable_when_static},
            {QStringLiteral("requiresBackground"), meta.requires_background}
        };
        for (std::size_t i = 0; i < meta.parameter_count; ++i) {
            const EffectParameterDescriptor &parameter = meta.parameters[i];
            const QString path = QString::fromUtf8(parameter.path ? parameter.path : "");
            if (path.isEmpty())
                continue;
            QJsonObject schema{
                {QStringLiteral("type"), parameterKindName(parameter.kind)},
                {QStringLiteral("label"), parameterLabel(parameter.path)},
                {QStringLiteral("min"), parameter.minimum},
                {QStringLiteral("max"), parameter.maximum},
                {QStringLiteral("step"), parameter.step},
                {QStringLiteral("default"), parameter.default_value},
                {QStringLiteral("animatable"), parameter.keyframeable}
            };
            def.parameterSchema.insert(path, schema);
            if (parameter.kind != EffectParameterKind::Color &&
                parameter.kind != EffectParameterKind::Point)
                def.defaults.insert(path, parameter.default_value);
        }
        if (meta.type == LayerEffectType::FourColorGradient) {
            def.defaults = QJsonObject{
                {QStringLiteral("point1"), QJsonArray{0.25, 0.25}},
                {QStringLiteral("color1"), QJsonArray{1.0, 0.16, 0.08, 1.0}},
                {QStringLiteral("point2"), QJsonArray{0.75, 0.25}},
                {QStringLiteral("color2"), QJsonArray{1.0, 0.82, 0.08, 1.0}},
                {QStringLiteral("point3"), QJsonArray{0.25, 0.75}},
                {QStringLiteral("color3"), QJsonArray{0.08, 0.42, 1.0, 1.0}},
                {QStringLiteral("point4"), QJsonArray{0.75, 0.75}},
                {QStringLiteral("color4"), QJsonArray{0.72, 0.10, 1.0, 1.0}},
                {QStringLiteral("blend"), 50.0},
                {QStringLiteral("jitter"), 0.0},
                {QStringLiteral("opacity"), 1.0},
                {QStringLiteral("blendMode"), 0}
            };
            const auto point = [](const QString &label, double x, double y) {
                return QJsonObject{{QStringLiteral("type"), QStringLiteral("point")},
                                   {QStringLiteral("label"), label},
                                   {QStringLiteral("minX"), -2.0},
                                   {QStringLiteral("maxX"), 3.0},
                                   {QStringLiteral("minY"), -2.0},
                                   {QStringLiteral("maxY"), 3.0},
                                   {QStringLiteral("step"), 0.01},
                                   {QStringLiteral("default"), QJsonArray{x, y}},
                                   {QStringLiteral("animatable"), true}};
            };
            const auto color = [](const QString &label, const QJsonArray &value) {
                return QJsonObject{{QStringLiteral("type"), QStringLiteral("color")},
                                   {QStringLiteral("label"), label},
                                   {QStringLiteral("default"), value},
                                   {QStringLiteral("animatable"), true}};
            };
            def.parameterSchema.insert(QStringLiteral("point1"), point(QStringLiteral("Point 1"), 0.25, 0.25));
            def.parameterSchema.insert(QStringLiteral("color1"), color(QStringLiteral("Color 1"), QJsonArray{1.0, 0.16, 0.08, 1.0}));
            def.parameterSchema.insert(QStringLiteral("point2"), point(QStringLiteral("Point 2"), 0.75, 0.25));
            def.parameterSchema.insert(QStringLiteral("color2"), color(QStringLiteral("Color 2"), QJsonArray{1.0, 0.82, 0.08, 1.0}));
            def.parameterSchema.insert(QStringLiteral("point3"), point(QStringLiteral("Point 3"), 0.25, 0.75));
            def.parameterSchema.insert(QStringLiteral("color3"), color(QStringLiteral("Color 3"), QJsonArray{0.08, 0.42, 1.0, 1.0}));
            def.parameterSchema.insert(QStringLiteral("point4"), point(QStringLiteral("Point 4"), 0.75, 0.75));
            def.parameterSchema.insert(QStringLiteral("color4"), color(QStringLiteral("Color 4"), QJsonArray{0.72, 0.10, 1.0, 1.0}));
            def.parameterSchema.insert(QStringLiteral("blend"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("float")}, {QStringLiteral("label"), QStringLiteral("Blend")},
                {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1000.0}, {QStringLiteral("step"), 1.0},
                {QStringLiteral("default"), 50.0}, {QStringLiteral("animatable"), true}});
            def.parameterSchema.insert(QStringLiteral("jitter"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("float")}, {QStringLiteral("label"), QStringLiteral("Jitter")},
                {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 100.0}, {QStringLiteral("step"), 1.0},
                {QStringLiteral("default"), 0.0}, {QStringLiteral("animatable"), true}});
            def.parameterSchema.insert(QStringLiteral("opacity"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("float")}, {QStringLiteral("label"), QStringLiteral("Opacity")},
                {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}, {QStringLiteral("step"), 0.01},
                {QStringLiteral("default"), 1.0}, {QStringLiteral("animatable"), true}});
            def.parameterSchema.insert(QStringLiteral("blendMode"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("enum")}, {QStringLiteral("label"), QStringLiteral("Blending Mode")},
                {QStringLiteral("default"), 0},
                {QStringLiteral("options"), QJsonArray{
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Normal")}, {QStringLiteral("value"), 0}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Multiply")}, {QStringLiteral("value"), 1}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Add")}, {QStringLiteral("value"), 2}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Screen")}, {QStringLiteral("value"), 3}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Overlay")}, {QStringLiteral("value"), 4}},
                    QJsonObject{{QStringLiteral("label"), QStringLiteral("Color")}, {QStringLiteral("value"), 5}}
                }}});
            def.canvasHandles = QJsonArray{
                QJsonObject{{QStringLiteral("path"), QStringLiteral("point1")}, {QStringLiteral("label"), QStringLiteral("1")}, {QStringLiteral("color"), QStringLiteral("#ff4a22")}},
                QJsonObject{{QStringLiteral("path"), QStringLiteral("point2")}, {QStringLiteral("label"), QStringLiteral("2")}, {QStringLiteral("color"), QStringLiteral("#ffd11a")}},
                QJsonObject{{QStringLiteral("path"), QStringLiteral("point3")}, {QStringLiteral("label"), QStringLiteral("3")}, {QStringLiteral("color"), QStringLiteral("#2178ff")}},
                QJsonObject{{QStringLiteral("path"), QStringLiteral("point4")}, {QStringLiteral("label"), QStringLiteral("4")}, {QStringLiteral("color"), QStringLiteral("#b82bff")}}
            };
        }
        effects.push_back(std::move(def));
    }
}

struct LoadedNativeLibrary {
    QLibrary *library = nullptr;
    QString providerId;
    QString providerVersion;
    fxm_plugin_can_unload_v4_fn canUnload = nullptr;
    fxm_plugin_before_unload_v4_fn beforeUnload = nullptr;
};

QList<LoadedNativeLibrary> &loadedLibraries()
{
    static QList<LoadedNativeLibrary> libs;
    return libs;
}

void unloadNativeLibraries(const QHash<QString, int> *activeInstances = nullptr)
{
    auto &libraries = loadedLibraries();
    for (auto it = libraries.begin(); it != libraries.end();) {
        QLibrary *library = it->library;
        if (!library) {
            it = libraries.erase(it);
            continue;
        }
        bool canUnload = !activeInstances || activeInstances->value(it->providerId) <= 0;
        if (!canUnload) {
            ++it;
            continue;
        }
        if (it->canUnload) {
            try {
                canUnload = it->canUnload(it->providerId.toUtf8().constData()) != 0;
            } catch (...) {
                canUnload = false;
            }
        }
        if (!canUnload) {
            ++it;
            continue;
        }
        if (it->beforeUnload) {
            try { it->beforeUnload(it->providerId.toUtf8().constData()); } catch (...) {}
        }
        library->unload();
        delete library;
        it = libraries.erase(it);
    }
}

QString moduleDataRoot()
{
    return QString::fromStdString(fxm::resolve_asset_path("extensions"));
}

QString moduleConfigRoot()
{
    auto *provider = fxm::host_context_provider();
    return provider
        ? QString::fromStdString(provider->config_path("extensions"))
        : QString();
}
QString normalizedExtensionPath(const QString &path)
{
    QFileInfo info(path);
    const QString canonical = info.exists() ? info.canonicalFilePath() : QString();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QString extensionStateFile(const QString &name)
{
    const QString root = moduleConfigRoot();
    if (root.isEmpty())
        return {};
    QDir dir(root);
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(name);
}

QStringList loadPathListFile(const QString &name)
{
    const QString path = extensionStateFile(name);
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return {};
    QStringList values;
    for (const QJsonValue &value : doc.array()) {
        const QString pathValue = normalizedExtensionPath(value.toString());
        if (!pathValue.isEmpty() && !values.contains(pathValue))
            values << pathValue;
    }
    return values;
}

void savePathListFile(const QString &name, const QStringList &values)
{
    const QString path = extensionStateFile(name);
    if (path.isEmpty())
        return;
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}

QStringList extensionSearchRoots()
{
    QStringList roots;
    auto addRoot = [&roots](const QString &path) {
        const QString clean = QDir::cleanPath(path);
        if (!clean.isEmpty() && !roots.contains(clean))
            roots << clean;
    };
    addRoot(moduleDataRoot());
    addRoot(moduleConfigRoot());
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty())
        addRoot(QDir(appData).filePath(QStringLiteral("Effects")));
    const QByteArray env = qgetenv("FXM_EFFECT_PLUGIN_PATH");
    if (!env.isEmpty()) {
#if defined(Q_OS_WIN)
        const QChar separator = QLatin1Char(';');
#else
        const QChar separator = QLatin1Char(':');
#endif
        for (const QString &part : QString::fromUtf8(env).split(separator, Qt::SkipEmptyParts))
            addRoot(part);
    }
    return roots;
}


constexpr qint64 kMaxExtensionJsonBytes = 4ll * 1024ll * 1024ll;
constexpr int kMaxExtensionScanDepth = 8;
constexpr std::size_t kMaxCatalogEffects = 1024;
constexpr uint32_t kMaxNativeEffectsPerPlugin = 256;

QString containedExtensionPath(const QString &basePath, const QString &candidate)
{
    if (basePath.isEmpty() || candidate.trimmed().isEmpty())
        return {};
    const QString base = QDir(basePath).canonicalPath().isEmpty()
        ? QDir(basePath).absolutePath()
        : QDir(basePath).canonicalPath();
    QFileInfo info(QFileInfo(candidate).isAbsolute()
                       ? candidate
                       : QDir(basePath).filePath(candidate));
    const QString resolved = info.exists() && !info.canonicalFilePath().isEmpty()
        ? info.canonicalFilePath()
        : info.absoluteFilePath();
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    const QString prefix = QDir::cleanPath(base) + QLatin1Char('/');
    const QString cleanResolved = QDir::cleanPath(resolved);
    if (cleanResolved.compare(QDir::cleanPath(base), sensitivity) != 0 &&
        !cleanResolved.startsWith(prefix, sensitivity))
        return {};
    return cleanResolved;
}

QJsonObject loadJsonObjectFile(const QString &path, QStringList *diagnostics = nullptr)
{
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (diagnostics) diagnostics->push_back(QStringLiteral("Cannot read extension index %1").arg(path));
        return {};
    }
    if (file.size() < 0 || file.size() > kMaxExtensionJsonBytes) {
        if (diagnostics) diagnostics->push_back(QStringLiteral("Extension index is too large: %1").arg(path));
        return {};
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (diagnostics) diagnostics->push_back(QStringLiteral("Invalid extension index %1: %2").arg(path, error.errorString()));
        return {};
    }
    return document.object();
}

QJsonObject resolveManifestIndex(const QJsonValue &value, const QString &basePath,
                                 QStringList *diagnostics)
{
    if (value.isString()) {
        const QString path = containedExtensionPath(basePath, value.toString());
        if (path.isEmpty()) {
            if (diagnostics) diagnostics->push_back(QStringLiteral("Extension index escapes its package: %1").arg(value.toString()));
            return {};
        }
        return loadJsonObjectFile(path, diagnostics);
    }
    if (!value.isObject()) return {};
    const QJsonObject object = value.toObject();
    const QString index = object.value(QStringLiteral("index")).toString();
    if (index.isEmpty()) return object;
    const QString indexPath = containedExtensionPath(basePath, index);
    if (indexPath.isEmpty()) {
        if (diagnostics) diagnostics->push_back(QStringLiteral("Extension index escapes its package: %1").arg(index));
        return {};
    }
    QJsonObject resolved = loadJsonObjectFile(indexPath, diagnostics);
    const QString indexDir = QFileInfo(index).path() == QStringLiteral(".")
        ? QString() : QFileInfo(index).path();
    if (!resolved.contains(QStringLiteral("items"))) {
        if (resolved.value(QStringLiteral("presets")).isArray())
            resolved.insert(QStringLiteral("items"), resolved.value(QStringLiteral("presets")));
        else if (resolved.value(QStringLiteral("assets")).isArray())
            resolved.insert(QStringLiteral("items"), resolved.value(QStringLiteral("assets")));
    }
    QJsonArray items = resolved.value(QStringLiteral("items")).toArray();
    QJsonArray safeItems;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject item = items.at(i).toObject();
        const QString file = item.value(QStringLiteral("file")).toString();
        if (!file.isEmpty()) {
            const QString relative = !indexDir.isEmpty() && QFileInfo(file).isRelative()
                ? QDir(indexDir).filePath(file) : file;
            const QString resolvedFile = containedExtensionPath(basePath, relative);
            if (resolvedFile.isEmpty()) {
                if (diagnostics) diagnostics->push_back(
                    QStringLiteral("Extension asset escapes its package: %1").arg(file));
                continue;
            }
            item.insert(QStringLiteral("file"), resolvedFile);
        }
        safeItems.append(item);
    }
    items = safeItems;
    if (!items.isEmpty()) resolved.insert(QStringLiteral("items"), items);
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.key() != QStringLiteral("index")) resolved.insert(it.key(), it.value());
    resolved.insert(QStringLiteral("_indexPath"), index);
    return resolved;
}

void hostLog(int level, const char *component, const char *message)
{
    (void)level;
    fxm::log(fxm::LogLevel::Info,
             component ? component : "Extension",
             message ? message : "");
}
}

FxmEffectExtensionCatalog &FxmEffectExtensionCatalog::instance()
{
    static FxmEffectExtensionCatalog catalog;
    return catalog;
}

void FxmEffectExtensionCatalog::reload()
{
    unloadNativeLibraries(&active_instances_);
    effects_.clear();
    diagnostics_.clear();
    quarantine_ = loadPathListFile(QStringLiteral("quarantine.json"));
    blacklist_ = loadPathListFile(QStringLiteral("blacklist.json"));
    appendBuiltIns(effects_);

    const QStringList roots = extensionSearchRoots();
    /* Discovery/scanning can touch disk, load native libraries and parse plugin
     * metadata. Keep that work off the caller/UI thread; the caller waits for
     * a consistent catalog snapshot before using the result. */
    auto scanFuture = std::async(std::launch::async, [this, roots]() {
        for (const QString &root : roots)
            scanManifestRoot(root);
        for (const QString &root : roots)
            scanNativeRoot(root);
    });
    scanFuture.get();
}

void FxmEffectExtensionCatalog::shutdown()
{
    unloadNativeLibraries(&active_instances_);
    effects_.clear();
    diagnostics_.clear();
}

QStringList FxmEffectExtensionCatalog::quarantineEntries() const
{
    return quarantine_;
}

QStringList FxmEffectExtensionCatalog::blacklistEntries() const
{
    return blacklist_;
}

void FxmEffectExtensionCatalog::clearQuarantine()
{
    quarantine_.clear();
    savePathListFile(QStringLiteral("quarantine.json"), quarantine_);
}

void FxmEffectExtensionCatalog::clearBlacklist()
{
    blacklist_.clear();
    savePathListFile(QStringLiteral("blacklist.json"), blacklist_);
}

void FxmEffectExtensionCatalog::blacklistPath(const QString &path)
{
    const QString clean = normalizedExtensionPath(path);
    if (clean.isEmpty() || blacklist_.contains(clean))
        return;
    blacklist_ << clean;
    savePathListFile(QStringLiteral("blacklist.json"), blacklist_);
}

void FxmEffectExtensionCatalog::retainPluginInstance(const QString &providerId)
{
    if (providerId.isEmpty())
        return;
    active_instances_[providerId] = active_instances_.value(providerId) + 1;
}

void FxmEffectExtensionCatalog::releasePluginInstance(const QString &providerId)
{
    if (providerId.isEmpty())
        return;
    const int next = active_instances_.value(providerId) - 1;
    if (next <= 0)
        active_instances_.remove(providerId);
    else
        active_instances_[providerId] = next;
}

bool FxmEffectExtensionCatalog::canUnloadProvider(const QString &providerId) const
{
    return providerId.isEmpty() || active_instances_.value(providerId) <= 0;
}

bool FxmEffectExtensionCatalog::pathIsQuarantinedOrBlacklisted(const QString &path) const
{
    const QString clean = normalizedExtensionPath(path);
    return !clean.isEmpty() && (quarantine_.contains(clean) || blacklist_.contains(clean));
}

void FxmEffectExtensionCatalog::quarantinePluginPath(const QString &path, const QString &reason,
                                                     const QString &pluginName,
                                                     const QString &pluginVersion)
{
    const QString clean = normalizedExtensionPath(path);
    if (clean.isEmpty())
        return;
    if (!quarantine_.contains(clean)) {
        quarantine_ << clean;
        savePathListFile(QStringLiteral("quarantine.json"), quarantine_);
    }
    diagnostics_ << QStringLiteral("Quarantined extension %1: %2")
                        .arg(QFileInfo(clean).fileName(), reason);

    const QString root = moduleConfigRoot();
    if (root.isEmpty())
        return;
    QDir crashDir(QDir(root).filePath(QStringLiteral("crash-reports")));
    crashDir.mkpath(QStringLiteral("."));
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    QFile report(crashDir.filePath(QStringLiteral("plugin-%1.json").arg(stamp)));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    const QJsonObject payload{
        {QStringLiteral("timeUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("path"), clean},
        {QStringLiteral("pluginName"), pluginName},
        {QStringLiteral("pluginVersion"), pluginVersion},
        {QStringLiteral("reason"), reason}
    };
    report.write(QJsonDocument(payload).toJson(QJsonDocument::Indented));
}

const FxmEffectExtensionDefinition *FxmEffectExtensionCatalog::find(const QString &id) const
{
    for (const auto &effect : effects_)
        if (effect.id == id) return &effect;
    return nullptr;
}

const FxmEffectExtensionDefinition *FxmEffectExtensionCatalog::find(LayerEffectType type) const
{
    for (const auto &effect : effects_)
        if (effect.builtIn && effect.builtInType == type) return &effect;
    return nullptr;
}

QString FxmEffectExtensionCatalog::builtInId(LayerEffectType type)
{
    if (const auto *definition = effect_descriptor(type))
        return QString::fromUtf8(definition->stable_id);
    return {};
}

bool FxmEffectExtensionCatalog::builtInTypeForId(const QString &id, LayerEffectType *type)
{
    for (const auto &definition : builtin_effect_descriptors()) {
        if (id == QString::fromUtf8(definition.stable_id) ||
            id == QString::fromUtf8(definition.legacy_id)) {
            if (type)
                *type = definition.type;
            return true;
        }
    }
    return false;
}

void FxmEffectExtensionCatalog::scanManifestRoot(const QString &root, int depth)
{
    if (root.isEmpty() || depth > kMaxExtensionScanDepth ||
        effects_.size() >= kMaxCatalogEffects)
        return;
    if (pathIsQuarantinedOrBlacklisted(root))
        return;
    QDir dir(root);
    const QStringList manifests = dir.entryList({QStringLiteral("*.fxm-effect.json"), QStringLiteral("manifest.json")}, QDir::Files);
    for (const QString &name : manifests) {
        const QString manifestPath = dir.filePath(name);
        if (!pathIsQuarantinedOrBlacklisted(manifestPath))
            loadManifest(manifestPath);
    }
    const QFileInfoList children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &child : children) {
        if (effects_.size() >= kMaxCatalogEffects)
            break;
        scanManifestRoot(child.absoluteFilePath(), depth + 1);
    }
}

void FxmEffectExtensionCatalog::loadManifest(const QString &manifestPath)
{
    QFile file(manifestPath);
    if (pathIsQuarantinedOrBlacklisted(manifestPath))
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        diagnostics_ << QStringLiteral("Cannot read %1").arg(manifestPath);
        quarantinePluginPath(manifestPath, QStringLiteral("manifest could not be read"));
        return;
    }
    if (file.size() < 0 || file.size() > kMaxExtensionJsonBytes) {
        diagnostics_ << QStringLiteral("Extension manifest is too large: %1").arg(manifestPath);
        quarantinePluginPath(manifestPath, QStringLiteral("manifest is too large"));
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        diagnostics_ << QStringLiteral("Invalid extension manifest %1: %2").arg(manifestPath, error.errorString());
        quarantinePluginPath(manifestPath, QStringLiteral("manifest JSON parse failed: %1").arg(error.errorString()));
        return;
    }
    const QJsonObject root = document.object();
    const int manifestApi = root.value(QStringLiteral("apiVersion")).toInt();
    if (manifestApi < 1 || manifestApi > static_cast<int>(FXM_PLUGIN_API_VERSION)) {
        diagnostics_ << QStringLiteral("Unsupported API version in %1").arg(manifestPath);
        quarantinePluginPath(manifestPath, QStringLiteral("unsupported API version"));
        return;
    }
    FxmEffectExtensionDefinition def;
    def.id = root.value(QStringLiteral("id")).toString().trimmed();
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{2,127}$"));
    if (!validId.match(def.id).hasMatch()) {
        diagnostics_ << QStringLiteral("Invalid extension id in %1").arg(manifestPath);
        quarantinePluginPath(manifestPath, QStringLiteral("invalid stable effect id"));
        return;
    }
    def.displayName = root.value(QStringLiteral("name")).toString(def.id).trimmed();
    def.category = root.value(QStringLiteral("category")).toString(QStringLiteral("Extensions"));
    def.providerId = root.value(QStringLiteral("provider")).toString(QStringLiteral("manifest"));
    def.providerVersion = root.value(QStringLiteral("version")).toString(QStringLiteral("1.0.0"));
    def.manifestPath = manifestPath;
    def.basePath = QFileInfo(manifestPath).absolutePath();
    def.shaderPath = containedExtensionPath(def.basePath, root.value(QStringLiteral("shader")).toString());
    def.parameterSchema = root.value(QStringLiteral("parameters")).toObject();
    def.defaults = root.value(QStringLiteral("defaults")).toObject();
    def.editorSchema = root.value(QStringLiteral("editor")).toObject();
    def.presetIndex = resolveManifestIndex(root.value(QStringLiteral("presets")), def.basePath, &diagnostics_);
    def.assetIndex = resolveManifestIndex(root.value(QStringLiteral("assets")), def.basePath, &diagnostics_);
    def.capabilities = root.value(QStringLiteral("capabilities")).toObject();
    def.animationSchema = root.value(QStringLiteral("animation")).toObject();
    def.canvasHandles = root.value(QStringLiteral("canvasHandles")).toArray();
    if (def.canvasHandles.isEmpty())
        def.canvasHandles = def.editorSchema.value(QStringLiteral("canvasHandles")).toArray();
    def.parameterMetadata = root.value(QStringLiteral("parameterMetadata")).toObject();
    if (def.parameterMetadata.isEmpty())
        def.parameterMetadata = def.parameterSchema;
    def.customPropertyWidgets = root.value(QStringLiteral("customPropertyWidgets")).toObject();
    def.requirements = root.value(QStringLiteral("requirements")).toObject();
    def.stateSerialization = root.value(QStringLiteral("stateSerialization")).toObject();
    def.renderPasses = root.value(QStringLiteral("renderPasses")).toArray();
    def.inputs = root.value(QStringLiteral("inputs")).toArray();
    def.auxiliaryInputs = root.value(QStringLiteral("auxiliaryInputs")).toArray();
    def.layerReferences = root.value(QStringLiteral("layerReferences")).toArray();
    const int manifestInputCount = root.value(QStringLiteral("inputCount")).toInt(
        std::max(1, static_cast<int>(def.inputs.size())));
    def.declaredInputCount = std::max(1, manifestInputCount);
    def.declaredColorSpace = root.value(QStringLiteral("colorSpace")).toString(
        def.requirements.value(QStringLiteral("colorSpace")).toString(QStringLiteral("preserve-input")));
    def.declaredAlphaContract = root.value(QStringLiteral("alpha")).toString(
        def.requirements.value(QStringLiteral("alpha")).toString(QStringLiteral("premultiplied-preserve")));
    def.backend = root.value(QStringLiteral("backend")).toString(
        def.capabilities.value(QStringLiteral("backend")).toString(QStringLiteral("gpu")));
    def.cpuWorkerOnly = def.backend == QStringLiteral("cpu-worker-only") ||
        def.backend == QStringLiteral("cpu") ||
        def.capabilities.value(QStringLiteral("cpuWorkerOnly")).toBool(false);
    def.multiPass = def.renderPasses.size() > 1 ||
        def.backend == QStringLiteral("gpu-multi-pass") ||
        def.capabilities.value(QStringLiteral("multiPass")).toBool(false);
    def.pluginPath = manifestPath;
    def.schemaVersion = static_cast<uint32_t>(std::clamp(
        root.value(QStringLiteral("schemaVersion")).toInt(1), 1, 65535));
    for (const QJsonValue &value : root.value(QStringLiteral("techniques")).toArray())
        def.techniques << value.toString();
    if (def.id.isEmpty() || (def.shaderPath.isEmpty() && !def.cpuWorkerOnly)) {
        diagnostics_ << QStringLiteral("Extension manifest is missing a valid in-package shader: %1").arg(manifestPath);
        quarantinePluginPath(manifestPath, QStringLiteral("missing shader for GPU effect"), def.displayName, def.providerVersion);
        return;
    }
    if (!def.shaderPath.isEmpty()) {
        const QFileInfo shaderInfo(def.shaderPath);
        if (!shaderInfo.isFile() || !shaderInfo.isReadable()) {
            diagnostics_ << QStringLiteral("Extension shader is missing or unreadable: %1").arg(def.shaderPath);
            quarantinePluginPath(manifestPath, QStringLiteral("shader is missing or unreadable"), def.displayName, def.providerVersion);
            return;
        }
    }
    if (find(def.id)) {
        diagnostics_ << QStringLiteral("Duplicate effect id ignored: %1").arg(def.id);
        return;
    }
    effects_.push_back(std::move(def));
}

void FxmEffectExtensionCatalog::scanNativeRoot(const QString &root, int depth)
{
    if (root.isEmpty() || depth > kMaxExtensionScanDepth ||
        effects_.size() >= kMaxCatalogEffects)
        return;
    if (pathIsQuarantinedOrBlacklisted(root))
        return;
    QDir dir(root);
#if defined(Q_OS_WIN)
    const QStringList filters{QStringLiteral("*.dll")};
#elif defined(Q_OS_MACOS)
    const QStringList filters{QStringLiteral("*.dylib")};
#else
    const QStringList filters{QStringLiteral("*.so")};
#endif
    for (const QFileInfo &file : dir.entryInfoList(filters, QDir::Files | QDir::NoSymLinks)) {
        if (pathIsQuarantinedOrBlacklisted(file.absoluteFilePath()))
            continue;
        auto *library = new QLibrary(file.absoluteFilePath());
        if (!library->load()) {
            const QString reason = QStringLiteral("load failed: %1").arg(library->errorString());
            diagnostics_ << QStringLiteral("Cannot load native extension %1: %2")
                                .arg(file.fileName(), library->errorString());
            quarantinePluginPath(file.absoluteFilePath(), reason);
            delete library;
            continue;
        }

        QString pluginName = file.fileName();
        QString pluginProviderId;
        QString pluginVersion;
        fxm_plugin_can_unload_v4_fn canUnload = nullptr;
        fxm_plugin_before_unload_v4_fn beforeUnload = nullptr;
        uint32_t accepted = 0;
        try {
            const fxm_host_api_v1 host{FXM_PLUGIN_API_VERSION, hostLog};
            const auto query4 = reinterpret_cast<fxm_plugin_query_v4_fn>(library->resolve("fxm_plugin_query_v4"));
            const auto query3 = reinterpret_cast<fxm_plugin_query_v3_fn>(library->resolve("fxm_plugin_query_v3"));
            const auto query2 = reinterpret_cast<fxm_plugin_query_v2_fn>(library->resolve("fxm_plugin_query_v2"));
            const auto query1 = reinterpret_cast<fxm_plugin_query_v1_fn>(library->resolve("fxm_plugin_query_v1"));

            const fxm_plugin_descriptor_v4 *plugin4 = query4 ? query4(&host) : nullptr;
            const bool validV4Descriptor = plugin4 &&
                plugin4->descriptor_size >= sizeof(fxm_plugin_descriptor_v4);
            const fxm_plugin_descriptor_v3 *plugin3 = validV4Descriptor
                ? &plugin4->v3 : (query3 ? query3(&host) : nullptr);
            const fxm_plugin_descriptor_v2 *queriedPlugin2 = plugin3
                ? &plugin3->v2 : (query2 ? query2(&host) : nullptr);
            const bool validV2Descriptor = queriedPlugin2 &&
                queriedPlugin2->descriptor_size >= sizeof(fxm_plugin_descriptor_v2);
            const fxm_plugin_descriptor_v2 *plugin2 = validV2Descriptor
                ? queriedPlugin2 : nullptr;
            const fxm_plugin_descriptor_v1 *plugin = queriedPlugin2
                ? &queriedPlugin2->v1 : (query1 ? query1(&host) : nullptr);
            if (!plugin || plugin->api_version < FXM_PLUGIN_API_VERSION_1 ||
                plugin->api_version > FXM_PLUGIN_API_VERSION || !plugin->id) {
                const QString reason = QStringLiteral("incompatible or missing plugin descriptor");
                diagnostics_ << QStringLiteral("Rejected incompatible native extension: %1").arg(file.fileName());
                quarantinePluginPath(file.absoluteFilePath(), reason, pluginName, pluginVersion);
                library->unload();
                delete library;
                continue;
            }
            pluginProviderId = QString::fromUtf8(plugin->id);
            pluginName = QString::fromUtf8(plugin->name ? plugin->name : plugin->id);
            pluginVersion = QString::fromUtf8(plugin->version ? plugin->version : "");
            canUnload = validV4Descriptor ? plugin4->can_unload : nullptr;
            beforeUnload = validV4Descriptor ? plugin4->before_unload : nullptr;

            const bool hasV4Effects = validV4Descriptor && plugin4->effects_v4 && plugin4->effect_v4_count > 0;
            const bool hasV3Effects = plugin3 && validV2Descriptor && plugin3->effects_v3 && plugin3->effect_v3_count > 0;
            const bool hasV2Effects = plugin2 && plugin2->effects_v2 && plugin2->effect_v2_count > 0;
            const bool hasV1Effects = plugin->effects && plugin->effect_count > 0;
            const uint32_t declaredCount = hasV4Effects ? plugin4->effect_v4_count
                                          : hasV3Effects ? plugin3->effect_v3_count
                                          : hasV2Effects ? plugin2->effect_v2_count
                                                         : plugin->effect_count;
            const uint32_t count = std::min(declaredCount, kMaxNativeEffectsPerPlugin);
            if (!hasV4Effects && !hasV3Effects && !hasV2Effects && !hasV1Effects) {
                const QString reason = QStringLiteral("native descriptor exposes no effects");
                diagnostics_ << QStringLiteral("Native extension has no effect descriptors: %1").arg(file.fileName());
                quarantinePluginPath(file.absoluteFilePath(), reason, pluginName, pluginVersion);
                library->unload();
                delete library;
                continue;
            }

            auto parseObject = [](const char *json) {
                if (!json)
                    return QJsonObject{};
                QJsonParseError error{};
                const auto doc = QJsonDocument::fromJson(QByteArray(json), &error);
                return error.error == QJsonParseError::NoError && doc.isObject()
                    ? doc.object() : QJsonObject{};
            };
            auto parseArray = [](const char *json) {
                if (!json)
                    return QJsonArray{};
                QJsonParseError error{};
                const auto doc = QJsonDocument::fromJson(QByteArray(json), &error);
                return error.error == QJsonParseError::NoError && doc.isArray()
                    ? doc.array() : QJsonArray{};
            };
            auto backendNameV4 = [](fxm_effect_backend_v4 backend) {
                switch (backend) {
                case FXM_EFFECT_BACKEND_GPU_SHADER: return QStringLiteral("gpu");
                case FXM_EFFECT_BACKEND_GPU_MULTI_PASS: return QStringLiteral("gpu-multi-pass");
                case FXM_EFFECT_BACKEND_CPU_WORKER_ONLY: return QStringLiteral("cpu-worker-only");
                case FXM_EFFECT_BACKEND_HYBRID_WORKER_AND_GPU: return QStringLiteral("hybrid-worker-gpu");
                }
                return QStringLiteral("gpu");
            };
            auto colorNameV4 = [](fxm_effect_color_space_v4 color) {
                switch (color) {
                case FXM_COLOR_SPACE_PRESERVE_INPUT: return QStringLiteral("preserve-input");
                case FXM_COLOR_SPACE_SCENE_LINEAR: return QStringLiteral("scene-linear");
                case FXM_COLOR_SPACE_DISPLAY_REFERRED: return QStringLiteral("display-referred");
                case FXM_COLOR_SPACE_HDR_LINEAR: return QStringLiteral("hdr-linear");
                }
                return QStringLiteral("preserve-input");
            };
            auto alphaNameV4 = [](fxm_effect_alpha_contract_v4 alpha) {
                switch (alpha) {
                case FXM_ALPHA_PREMULTIPLIED_PRESERVE: return QStringLiteral("premultiplied-preserve");
                case FXM_ALPHA_PREMULTIPLIED_EXPAND: return QStringLiteral("premultiplied-expand");
                case FXM_ALPHA_PREMULTIPLIED_REPLACE: return QStringLiteral("premultiplied-replace");
                case FXM_ALPHA_STRAIGHT_INPUT_REQUIRED: return QStringLiteral("straight-input-required");
                }
                return QStringLiteral("premultiplied-preserve");
            };

            for (uint32_t i = 0; i < count; ++i) {
                const fxm_effect_descriptor_v4 *v4 = hasV4Effects ? &plugin4->effects_v4[i] : nullptr;
                const fxm_effect_descriptor_v3 *v3 = v4 ? &v4->v3
                    : (hasV3Effects ? &plugin3->effects_v3[i] : nullptr);
                const fxm_effect_descriptor_v2 *v2 = v3 ? &v3->v2
                    : (hasV2Effects ? &plugin2->effects_v2[i] : nullptr);
                const fxm_effect_descriptor_v1 &src = v2 ? v2->v1 : plugin->effects[i];
                const bool cpuOnly = v4 && v4->backend == FXM_EFFECT_BACKEND_CPU_WORKER_ONLY;
                if (!src.id || (!src.shader_path && !cpuOnly)) {
                    diagnostics_ << QStringLiteral("Native extension %1 has an incomplete effect descriptor")
                                        .arg(file.fileName());
                    continue;
                }

                const QString effectId = QString::fromUtf8(src.id).trimmed();
                static const QRegularExpression validNativeId(
                    QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{2,127}$"));
                if (!validNativeId.match(effectId).hasMatch() || find(effectId)) {
                    diagnostics_ << QStringLiteral("Duplicate or invalid native effect id ignored: %1").arg(effectId);
                    continue;
                }
                QString shaderPath;
                if (src.shader_path) {
                    shaderPath = containedExtensionPath(
                        file.absolutePath(), QString::fromUtf8(src.shader_path));
                    if (shaderPath.isEmpty() || !QFileInfo(shaderPath).isFile() ||
                        !QFileInfo(shaderPath).isReadable()) {
                        diagnostics_ << QStringLiteral("Native extension shader is missing or outside its package: %1")
                                            .arg(QString::fromUtf8(src.shader_path));
                        continue;
                    }
                }

                FxmEffectExtensionDefinition def;
                def.id = effectId;
                def.displayName = QString::fromUtf8(src.display_name ? src.display_name : src.id);
                def.category = QString::fromUtf8(src.category ? src.category : "Extensions");
                def.shaderPath = shaderPath;
                def.providerId = QString::fromUtf8(plugin->id);
                def.providerVersion = pluginVersion;
                def.nativeProvider = true;
                def.basePath = file.absolutePath();
                def.pluginPath = file.absoluteFilePath();
                if (v2) {
                    def.schemaVersion = std::clamp<uint32_t>(v2->schema_version, 1u, 65535u);
                    def.editorSchema = parseObject(v2->editor_schema_json);
                    def.presetIndex = parseObject(v2->preset_index_json);
                    def.assetIndex = parseObject(v2->asset_index_json);
                    def.capabilities = parseObject(v2->capabilities_json);
                    def.animationSchema = parseObject(v2->animation_schema_json);
                    def.validateState = plugin2->validate_state;
                    def.migrateState = plugin2->migrate_state;
                    def.releaseString = plugin2->release_string;
                }
                if (v3)
                    def.canvasHandles = parseArray(v3->canvas_handles_schema_json);
                if (v4) {
                    def.declaredInputCount = std::max(1, static_cast<int>(v4->input_count));
                    def.backend = backendNameV4(v4->backend);
                    def.declaredColorSpace = colorNameV4(v4->color_space);
                    def.declaredAlphaContract = alphaNameV4(v4->alpha_contract);
                    def.parameterMetadata = parseObject(v4->parameter_metadata_json);
                    def.customPropertyWidgets = parseObject(v4->custom_property_widgets_json);
                    def.renderPasses = parseArray(v4->render_passes_json);
                    def.inputs = parseArray(v4->inputs_json);
                    def.auxiliaryInputs = parseArray(v4->auxiliary_inputs_json);
                    def.layerReferences = parseArray(v4->layer_references_json);
                    def.requirements = parseObject(v4->requirements_json);
                    def.stateSerialization = parseObject(v4->state_serialization_json);
                    def.cpuWorkerOnly = v4->backend == FXM_EFFECT_BACKEND_CPU_WORKER_ONLY;
                    def.multiPass = v4->backend == FXM_EFFECT_BACKEND_GPU_MULTI_PASS || def.renderPasses.size() > 1;
                }
                if (src.manifest_json) {
                    const QJsonObject metadata = parseObject(src.manifest_json);
                    def.parameterSchema = metadata.value(QStringLiteral("parameters")).toObject();
                    def.defaults = metadata.value(QStringLiteral("defaults")).toObject();
                    if (def.parameterMetadata.isEmpty())
                        def.parameterMetadata = metadata.value(QStringLiteral("parameterMetadata")).toObject();
                    if (def.canvasHandles.isEmpty())
                        def.canvasHandles = metadata.value(QStringLiteral("canvasHandles")).toArray();
                    if (def.customPropertyWidgets.isEmpty())
                        def.customPropertyWidgets = metadata.value(QStringLiteral("customPropertyWidgets")).toObject();
                }
                if (def.parameterMetadata.isEmpty())
                    def.parameterMetadata = def.parameterSchema;
                if (def.backend.isEmpty())
                    def.backend = def.capabilities.value(QStringLiteral("backend")).toString(QStringLiteral("gpu"));
                effects_.push_back(std::move(def));
                ++accepted;
            }
        } catch (const std::exception &ex) {
            quarantinePluginPath(file.absoluteFilePath(), QStringLiteral("scan exception: %1").arg(QString::fromUtf8(ex.what())), pluginName, pluginVersion);
            library->unload();
            delete library;
            continue;
        } catch (...) {
            quarantinePluginPath(file.absoluteFilePath(), QStringLiteral("scan exception: unknown native plugin exception"), pluginName, pluginVersion);
            library->unload();
            delete library;
            continue;
        }

        if (accepted == 0) {
            quarantinePluginPath(file.absoluteFilePath(), QStringLiteral("no accepted effects after scan"), pluginName, pluginVersion);
            library->unload();
            delete library;
        } else {
            loadedLibraries().push_back({library, pluginProviderId, pluginVersion, canUnload, beforeUnload});
        }
    }
    for (const QFileInfo &child : dir.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        if (effects_.size() >= kMaxCatalogEffects)
            break;
        scanNativeRoot(child.absoluteFilePath(), depth + 1);
    }
}
