#include "obs-plugin-preferences.h"

#include "cache-manager.h"
#include "system-memory.h"
#include "title-logger.h"
#include "title-preferences.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QVector>
#include <QUrl>

namespace fxm::obs_plugin {
namespace {
constexpr const char *kSettingsOrganization = "FluxMotion";
constexpr const char *kSettingsApplication = "Dock";
constexpr const char *kPluginGroup = "ObsPluginPreferences";
constexpr const char *kEditorExecutableKey = "FluxMotionEditorExecutable";
constexpr const char *kPrerenderStartModeKey = "Prerender/StartMode";
constexpr const char *kPrerenderPlaybackModeKey = "Prerender/PlaybackMode";
constexpr const char *kPrerenderPlayAfterRenderingKey = "Prerender/PlayAfterRendering";
constexpr const char *kPrerenderCadenceModeKey = "Prerender/CadenceMode";

bool is_editor_executable(const QString &path)
{
    const QFileInfo file(path.trimmed());
    if(!file.exists()||!file.isFile())return false;
#if defined(Q_OS_WIN)
    return file.suffix().compare(QStringLiteral("exe"),Qt::CaseInsensitive)==0;
#else
    return file.isExecutable();
#endif
}

void append_candidate(QStringList &candidates,const QString &path)
{
    const QString cleaned=QDir::cleanPath(path.trimmed());
    if(!cleaned.isEmpty()&&!candidates.contains(cleaned,Qt::CaseInsensitive))
        candidates.push_back(cleaned);
}

void append_installed_candidates(QStringList &candidates,QSettings::Scope scope)
{
    QSettings installation(QSettings::NativeFormat,scope,
                           QStringLiteral("FluxSuite"),QStringLiteral("Flux Motion"));
    append_candidate(candidates,installation.value(
        QStringLiteral("installation/executable")).toString());
    const QString root=installation.value(QStringLiteral("installation/path")).toString();
    if(!root.isEmpty()){
#if defined(Q_OS_WIN)
        append_candidate(candidates,QDir(root).filePath(QStringLiteral("Flux Motion.exe")));
        append_candidate(candidates,QDir(root).filePath(QStringLiteral("flux-motion-editor.exe")));
#else
        append_candidate(candidates,QDir(root).filePath(QStringLiteral("Flux Motion")));
        append_candidate(candidates,QDir(root).filePath(QStringLiteral("flux-motion-editor")));
#endif
    }
}

void show_logging_preferences(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("OBS Plugin Logging"));
    dialog.resize(720, 650);
    auto *root = new QVBoxLayout(&dialog);

    auto *enabled = new QCheckBox(QStringLiteral("Enable file logging"), &dialog);
    enabled->setChecked(TitlePreferences::logging_enabled());
    root->addWidget(enabled);

    auto *form = new QFormLayout;
    auto *level = new QComboBox(&dialog);
    level->addItem(QStringLiteral("Off"), 0);
    level->addItem(QStringLiteral("Error"), 1);
    level->addItem(QStringLiteral("Warning"), 2);
    level->addItem(QStringLiteral("Info"), 3);
    level->addItem(QStringLiteral("Debug"), 4);
    level->addItem(QStringLiteral("Trace"), 5);
    level->setCurrentIndex(qMax(0, level->findData(TitlePreferences::logging_level())));
    form->addRow(QStringLiteral("Level"), level);

    auto *pathRow = new QWidget(&dialog);
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    auto *path = new QLineEdit(TitlePreferences::logging_directory(), pathRow);
    auto *browse = new QPushButton(QStringLiteral("Browse…"), pathRow);
    pathLayout->addWidget(path, 1);
    pathLayout->addWidget(browse);
    form->addRow(QStringLiteral("Log folder"), pathRow);

    auto *session = new QLabel(TitleLogger::currentSessionFilePath(), &dialog);
    session->setWordWrap(true);
    session->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Current session"), session);

    auto *mirrorObs = new QCheckBox(QStringLiteral("Also write to the OBS log"), &dialog);
    mirrorObs->setChecked(TitlePreferences::logging_mirror_to_obs());
    form->addRow(mirrorObs);
    root->addLayout(form);

    auto *categoriesLabel = new QLabel(QStringLiteral("Logging categories"), &dialog);
    QFont categoriesFont = categoriesLabel->font();
    categoriesFont.setBold(true);
    categoriesLabel->setFont(categoriesFont);
    root->addWidget(categoriesLabel);
    auto *scopeHint = new QLabel(
        QStringLiteral("OBS source diagnostics are available only here. Shared plugin, editor, rendering, cache and media categories are also available in the editor preferences."),
        &dialog);
    scopeHint->setWordWrap(true);
    root->addWidget(scopeHint);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    auto *categoryBody = new QWidget(scroll);
    auto *categoryLayout = new QVBoxLayout(categoryBody);
    QVector<QPair<QString, QCheckBox *>> categoryChecks;
    QMap<QString, QVBoxLayout *> groups;
    for (const TitleLogCategory &category : TitleLogger::categories()) {
        QVBoxLayout *groupLayout = groups.value(category.group, nullptr);
        if (!groupLayout) {
            auto *group = new QGroupBox(category.group, categoryBody);
            groupLayout = new QVBoxLayout(group);
            categoryLayout->addWidget(group);
            groups.insert(category.group, groupLayout);
        }
        auto *check = new QCheckBox(category.display_name, categoryBody);
        check->setChecked(TitlePreferences::logging_category_enabled(
            category.key, category.default_enabled));
        check->setToolTip(category.description);
        groupLayout->addWidget(check);
        categoryChecks.push_back(qMakePair(category.key, check));
    }
    categoryLayout->addStretch();
    scroll->setWidget(categoryBody);
    root->addWidget(scroll, 1);

    auto *actions = new QHBoxLayout;
    auto *selectAll = new QPushButton(QStringLiteral("Select all"), &dialog);
    auto *selectNone = new QPushButton(QStringLiteral("Select none"), &dialog);
    auto *openFolder = new QPushButton(QStringLiteral("Open folder"), &dialog);
    auto *clearLog = new QPushButton(QStringLiteral("Clear log"), &dialog);
    actions->addWidget(selectAll);
    actions->addWidget(selectNone);
    actions->addStretch();
    actions->addWidget(openFolder);
    actions->addWidget(clearLog);
    root->addLayout(actions);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    root->addWidget(buttons);
    QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("Choose log folder"), path->text());
        if (!selected.isEmpty()) path->setText(selected);
    });
    QObject::connect(selectAll, &QPushButton::clicked, &dialog, [categoryChecks] {
        for (const auto &entry : categoryChecks) entry.second->setChecked(true);
    });
    QObject::connect(selectNone, &QPushButton::clicked, &dialog, [categoryChecks] {
        for (const auto &entry : categoryChecks) entry.second->setChecked(false);
    });
    QObject::connect(openFolder, &QPushButton::clicked, &dialog, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QFileInfo(TitleLogger::currentSessionFilePath()).absolutePath()));
    });
    QObject::connect(clearLog, &QPushButton::clicked, &dialog, [] {
        QFile file(TitleLogger::currentSessionFilePath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) file.close();
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    TitlePreferences::set_logging_enabled(enabled->isChecked());
    TitlePreferences::set_logging_level(level->currentData().toInt());
    TitlePreferences::set_logging_mirror_to_obs(mirrorObs->isChecked());
    const QString requestedPath = path->text().trimmed();
    if (!requestedPath.isEmpty()) {
        TitlePreferences::set_logging_directory(requestedPath);
        TitleLogger::relocateCurrentSession(TitlePreferences::logging_directory());
    }
    for (const auto &entry : categoryChecks)
        TitlePreferences::set_logging_category_enabled(entry.first, entry.second->isChecked());
}
}

QString configured_editor_executable()
{
    QSettings settings(QString::fromUtf8(kSettingsOrganization),
                       QString::fromUtf8(kSettingsApplication));
    settings.beginGroup(QString::fromUtf8(kPluginGroup));
    const QString path=settings.value(QString::fromUtf8(kEditorExecutableKey)).toString();
    settings.endGroup();
    return path.trimmed();
}

void set_configured_editor_executable(const QString &path)
{
    QSettings settings(QString::fromUtf8(kSettingsOrganization),
                       QString::fromUtf8(kSettingsApplication));
    settings.beginGroup(QString::fromUtf8(kPluginGroup));
    const QString cleaned=QDir::cleanPath(path.trimmed());
    if(cleaned.isEmpty())settings.remove(QString::fromUtf8(kEditorExecutableKey));
    else settings.setValue(QString::fromUtf8(kEditorExecutableKey),cleaned);
    settings.endGroup();settings.sync();
}

QString resolved_editor_executable()
{
    QStringList candidates;
    append_candidate(candidates,configured_editor_executable());
    append_candidate(candidates,qEnvironmentVariable("FLUX_MOTION_EDITOR_PATH"));
    append_installed_candidates(candidates,QSettings::UserScope);
    append_installed_candidates(candidates,QSettings::SystemScope);
#if defined(Q_OS_WIN)
    for(const char *variable:{"ProgramFiles","ProgramFiles(x86)","LOCALAPPDATA"}){
        const QString root=qEnvironmentVariable(variable);
        if(root.isEmpty())continue;
        append_candidate(candidates,QDir(root).filePath(
            QStringLiteral("Flux Suite/Flux Motion/Flux Motion.exe")));
    }
    append_candidate(candidates,QStandardPaths::findExecutable(
        QStringLiteral("flux-motion-editor.exe")));
#else
    append_candidate(candidates,QStandardPaths::findExecutable(
        QStringLiteral("flux-motion-editor")));
#endif
    for(const QString &candidate:candidates)
        if(is_editor_executable(candidate))return QFileInfo(candidate).absoluteFilePath();
    return {};
}

void show_plugin_preferences(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Flux Motion Plugin for OBS Preferences"));
    dialog.resize(660,500);
    auto *root=new QVBoxLayout(&dialog);

    auto *cacheGroup=new QGroupBox(QStringLiteral("Cache / Prerendering"),&dialog);
    auto *cacheForm=new QFormLayout(cacheGroup);
    auto *cacheEnabled=new QCheckBox(QStringLiteral("Enable caching and prerendering"),cacheGroup);
    cacheEnabled->setChecked(CacheManager::instance().cacheEnabled());
    cacheForm->addRow(cacheEnabled);
    auto *ramLimit=new QSpinBox(cacheGroup);
    ramLimit->setRange(fxm::system_memory::kMinimumCacheRamMb,
                       std::min(32768,fxm::system_memory::maximum_cache_ram_mb()));
    ramLimit->setSingleStep(128);ramLimit->setSuffix(QStringLiteral(" MB"));
    ramLimit->setValue(TitlePreferences::cache_ram_limit_mb());
    cacheForm->addRow(QStringLiteral("RAM cache limit"),ramLimit);

    auto *diskRow=new QWidget(cacheGroup);auto *diskLayout=new QHBoxLayout(diskRow);
    diskLayout->setContentsMargins(0,0,0,0);
    auto *diskPath=new QLineEdit(CacheManager::instance().diskCacheLocation(),diskRow);
    auto *browseDisk=new QPushButton(QStringLiteral("Browse…"),diskRow);
    diskLayout->addWidget(diskPath,1);diskLayout->addWidget(browseDisk);
    cacheForm->addRow(QStringLiteral("Disk cache folder"),diskRow);

    QSettings sharedSettings(QString::fromUtf8(kSettingsOrganization),
                             QString::fromUtf8(kSettingsApplication));
    auto *startMode=new QComboBox(cacheGroup);
    startMode->addItems({QStringLiteral("From current time"),QStringLiteral("From beginning")});
    startMode->setCurrentIndex(std::clamp(sharedSettings.value(
        QString::fromUtf8(kPrerenderStartModeKey),0).toInt(),0,startMode->count()-1));
    cacheForm->addRow(QStringLiteral("Prerender start"),startMode);
    auto *playbackMode=new QComboBox(cacheGroup);
    playbackMode->addItems({QStringLiteral("Loop"),QStringLiteral("Ping-pong loop"),
                            QStringLiteral("Play once"),QStringLiteral("Follow title playback mode")});
    playbackMode->setCurrentIndex(std::clamp(sharedSettings.value(
        QString::fromUtf8(kPrerenderPlaybackModeKey),0).toInt(),0,playbackMode->count()-1));
    cacheForm->addRow(QStringLiteral("Cached playback"),playbackMode);
    auto *cadenceMode=new QComboBox(cacheGroup);
    cadenceMode->addItems({QStringLiteral("Skip frames to maintain timing"),
                           QStringLiteral("Play every cached frame")});
    cadenceMode->setCurrentIndex(std::clamp(sharedSettings.value(
        QString::fromUtf8(kPrerenderCadenceModeKey),0).toInt(),0,cadenceMode->count()-1));
    cacheForm->addRow(QStringLiteral("Playback cadence"),cadenceMode);
    auto *cachedOnly=new QCheckBox(QStringLiteral("Play only after prerendering completes"),cacheGroup);
    cachedOnly->setChecked(sharedSettings.value(
        QString::fromUtf8(kPrerenderPlayAfterRenderingKey),false).toBool());
    cacheForm->addRow(cachedOnly);
    auto *clearCache=new QPushButton(QStringLiteral("Clear all cache now"),cacheGroup);
    cacheForm->addRow(clearCache);
    auto *clearOnExit=new QCheckBox(QStringLiteral("Clear cache when OBS exits"),cacheGroup);
    clearOnExit->setChecked(TitlePreferences::clear_cache_on_exit());
    cacheForm->addRow(clearOnExit);
    root->addWidget(cacheGroup);

    auto *editorGroup=new QGroupBox(QStringLiteral("Flux Motion"),&dialog);
    auto *editorForm=new QFormLayout(editorGroup);
    auto *editorRow=new QWidget(editorGroup);auto *editorLayout=new QHBoxLayout(editorRow);
    editorLayout->setContentsMargins(0,0,0,0);
    auto *editorPath=new QLineEdit(configured_editor_executable(),editorRow);
    editorPath->setPlaceholderText(QStringLiteral("Automatic discovery"));
    auto *browseEditor=new QPushButton(QStringLiteral("Browse…"),editorRow);
    editorLayout->addWidget(editorPath,1);editorLayout->addWidget(browseEditor);
    editorForm->addRow(QStringLiteral("Executable"),editorRow);
    const QString resolved=resolved_editor_executable();
    auto *resolvedLabel=new QLabel(resolved.isEmpty()
        ? QStringLiteral("No installed Flux Motion application was detected.")
        : QStringLiteral("Currently resolved: %1").arg(QDir::toNativeSeparators(resolved)),editorGroup);
    resolvedLabel->setWordWrap(true);editorForm->addRow(resolvedLabel);
    root->addWidget(editorGroup);

    auto *loggingGroup = new QGroupBox(QStringLiteral("OBS Plugin Logging"), &dialog);
    auto *loggingLayout = new QVBoxLayout(loggingGroup);
    auto *loggingHint = new QLabel(
        QStringLiteral("Configure the plugin log level, folder, OBS mirroring and diagnostic categories."),
        loggingGroup);
    loggingHint->setWordWrap(true);
    auto *configureLogging = new QPushButton(QStringLiteral("Logging settings…"), loggingGroup);
    loggingLayout->addWidget(loggingHint);
    loggingLayout->addWidget(configureLogging, 0, Qt::AlignLeft);
    root->addWidget(loggingGroup);

    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    root->addWidget(buttons);
    QObject::connect(browseDisk,&QPushButton::clicked,&dialog,[&]{
        const QString path=QFileDialog::getExistingDirectory(&dialog,
            QStringLiteral("Choose disk cache folder"),diskPath->text());
        if(!path.isEmpty())diskPath->setText(path);
    });
    QObject::connect(clearCache,&QPushButton::clicked,&dialog,[]{CacheManager::instance().clearAll();});
    QObject::connect(configureLogging,&QPushButton::clicked,&dialog,[&]{show_logging_preferences(&dialog);});
    QObject::connect(browseEditor,&QPushButton::clicked,&dialog,[&]{
#if defined(Q_OS_WIN)
        const QString filter=QStringLiteral("Applications (*.exe)");
#else
        const QString filter=QStringLiteral("All files (*)");
#endif
        const QString path=QFileDialog::getOpenFileName(&dialog,
            QStringLiteral("Choose Flux Motion executable"),editorPath->text(),filter);
        if(!path.isEmpty())editorPath->setText(QDir::toNativeSeparators(path));
    });
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;

    CacheManager::instance().setCacheEnabled(cacheEnabled->isChecked());
    CacheManager::instance().setRamCacheLimitMb(ramLimit->value());
    CacheManager::instance().setDiskCacheLocation(diskPath->text());
    TitlePreferences::set_clear_cache_on_exit(clearOnExit->isChecked());
    sharedSettings.setValue(QString::fromUtf8(kPrerenderStartModeKey),startMode->currentIndex());
    sharedSettings.setValue(QString::fromUtf8(kPrerenderPlaybackModeKey),playbackMode->currentIndex());
    sharedSettings.setValue(QString::fromUtf8(kPrerenderCadenceModeKey),cadenceMode->currentIndex());
    sharedSettings.setValue(QString::fromUtf8(kPrerenderPlayAfterRenderingKey),cachedOnly->isChecked());
    sharedSettings.sync();
    set_configured_editor_executable(editorPath->text());
}

} // namespace fxm::obs_plugin
