#include "standalone-editor-host.h"
#include "standalone-obs-graphics-runtime.h"
#include "standalone-welcome-screen.h"
#include "startup-launch.h"
#include "startup-progress-screen.h"
#include "startup-progress.h"
#include "editor-theme.h"
#include "recent-projects.h"
#include "project-loading-dialog.h"

#include "cache-manager.h"
#include "editor-host-interfaces.h"
#include "effect-extension-catalog.h"
#include "effect-runtime.h"
#include "title-assets.h"
#include "title-data.h"
#include "title-editor.h"
#include "title-localization.h"
#include "title-logger.h"
#include "title-preview-renderer.h"
#include "timecode-spinbox.h"
#include "obs-title-preview-renderer.h"
#include "title-source.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QFontDatabase>
#include <QImage>
#include <QFileDialog>
#include <QFileInfo>
#include <QLayout>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>
#include <QSplashScreen>
#include <QThread>

#include <util/base.h>

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

class StandaloneLogSession final {
public:
    StandaloneLogSession() { TitleLogger::startSession(); }
    ~StandaloneLogSession() { TitleLogger::endSession(); }
};

void register_startup_stages(fxm::editor::StartupProgress &progress)
{
    /* Modules may append their own stages through StartupProgress without any
     * change to StartupProgressScreen. Weights keep the overall bar useful
     * even when a subsystem has substantially more work than another. */
    progress.registerStage(QStringLiteral("application"),
                           QStringLiteral("Initializing application..."));
    progress.registerStage(QStringLiteral("configuration"),
                           QStringLiteral("Loading configuration..."));
    progress.registerStage(QStringLiteral("preferences"),
                           QStringLiteral("Loading user preferences..."));
    progress.registerStage(QStringLiteral("rendering"),
                           QStringLiteral("Initializing rendering engine..."),
                           3);
    progress.registerStage(QStringLiteral("media"),
                           QStringLiteral("Initializing media engine..."));
    progress.registerStage(QStringLiteral("plugins"),
                           QStringLiteral("Loading plugins..."), 2);
    progress.registerStage(QStringLiteral("fonts"),
                           QStringLiteral("Loading fonts..."));
    progress.registerStage(QStringLiteral("effects"),
                           QStringLiteral("Loading effects..."));
    progress.registerStage(QStringLiteral("workspace"),
                           QStringLiteral("Restoring workspace..."), 2);
    progress.registerStage(QStringLiteral("ui"),
                           QStringLiteral("Preparing UI..."), 2);
}

std::vector<fxm::editor::WelcomeProject> available_projects()
{
    std::vector<fxm::editor::WelcomeProject> projects;
    for (const auto &recent : fxm::editor::RecentProjects::entries()) {
        projects.push_back({
            recent.path,
            recent.name,
            recent.path,
            recent.path,
            recent.last_opened,
            recent.thumbnail_png_base64,
        });
    }
    return projects;
}

std::string first_or_new_project_id()
{
    for (const auto &title : TitleDataStore::instance().titles())
        if (title && !title->is_asset)
            return title->id;
    const auto created =
        TitleDataStore::instance().create_title("Untitled Project");
    return created ? created->id : std::string();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FluxMotion"));
    QCoreApplication::setApplicationName(QStringLiteral("Flux Motion"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FXM_VERSION_LABEL));

    // Publish the standalone installation independently of the OBS plugin.
    // The plugin resolves this registration and never needs to bundle Editor.
    QSettings installation(QSettings::NativeFormat,QSettings::UserScope,
                           QStringLiteral("FluxSuite"),QStringLiteral("Flux Motion"));
    installation.setValue(QStringLiteral("installation/executable"),
                          QCoreApplication::applicationFilePath());
    installation.setValue(QStringLiteral("installation/path"),
                          QCoreApplication::applicationDirPath());
    installation.sync();

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Flux Motion standalone graphics editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption title_option(
        QStringLiteral("title-id"),
        QStringLiteral("Open the title or graphic with this identifier."),
        QStringLiteral("id"));
    const QCommandLineOption hosted_option(
        QStringLiteral("hosted-by-obs"),
        QStringLiteral("Run in the OBS plugin-hosted launch workflow."));
    const QCommandLineOption config_option(
        QStringLiteral("config-root"),
        QStringLiteral("Use this project-store configuration directory."),
        QStringLiteral("path"));
    const QCommandLineOption scope_option(
        QStringLiteral("project-scope"),
        QStringLiteral("Use this OBS scene-collection/project scope."),
        QStringLiteral("name"));
    const QCommandLineOption data_option(
        QStringLiteral("data-root"),
        QStringLiteral("Use this Flux Motion resource directory."),
        QStringLiteral("path"));
    const QCommandLineOption preferences_option(
        QStringLiteral("preferences"),
        QStringLiteral("Open global Editor preferences."));
    const QCommandLineOption obs_bin_option(
        QStringLiteral("obs-bin-root"),
        QStringLiteral("Use this OBS bin directory for the graphics module."),
        QStringLiteral("path"));
    const QCommandLineOption graphics_smoke_option(
        QStringLiteral("graphics-smoke-test"),
        QStringLiteral("Initialize and shut down the Editor GPU runtime."));
    const QCommandLineOption host_width_option(
        QStringLiteral("host-width"),
        QStringLiteral("Use the host project's base canvas width."),
        QStringLiteral("pixels"));
    const QCommandLineOption host_height_option(
        QStringLiteral("host-height"),
        QStringLiteral("Use the host project's base canvas height."),
        QStringLiteral("pixels"));
    const QCommandLineOption host_fps_num_option(
        QStringLiteral("host-fps-num"),
        QStringLiteral("Use the host project's frame-rate numerator."),
        QStringLiteral("value"));
    const QCommandLineOption host_fps_den_option(
        QStringLiteral("host-fps-den"),
        QStringLiteral("Use the host project's frame-rate denominator."),
        QStringLiteral("value"));
    parser.addOptions({title_option, hosted_option, config_option, scope_option,
                       data_option, preferences_option, obs_bin_option,
                       graphics_smoke_option, host_width_option,
                       host_height_option, host_fps_num_option,
                       host_fps_den_option});
    parser.addPositionalArgument(
        QStringLiteral("project-file"),
        QStringLiteral("Flux Motion project, title or packed title to open."),
        QStringLiteral("[project-file]"));
    parser.process(application);

    QString startup_project_path;
    if (!parser.positionalArguments().isEmpty()) {
        const QFileInfo candidate(parser.positionalArguments().constFirst());
        const QString suffix = candidate.suffix().toLower();
        if (candidate.isFile() &&
            (suffix == QStringLiteral("fxmt") || suffix == QStringLiteral("fxmp") ||
             suffix == QStringLiteral("fxmproj"))) {
            startup_project_path = candidate.absoluteFilePath();
        }
    }

    const fxm::editor::LaunchRequest launch =
        fxm::editor::resolveLaunchRequest(
            parser.isSet(hosted_option), parser.isSet(preferences_option),
            parser.isSet(graphics_smoke_option), parser.value(title_option),
            startup_project_path);
    const bool plugin_hosted =
        launch.mode == fxm::editor::LaunchMode::PluginHosted;
    const bool host_format_supplied =
        parser.isSet(host_width_option) && parser.isSet(host_height_option) &&
        parser.isSet(host_fps_num_option) && parser.isSet(host_fps_den_option);
    const auto positive_option = [&parser](const QCommandLineOption &option,
                                           uint fallback) {
        bool valid = false;
        const uint parsed = parser.value(option).toUInt(&valid);
        return valid && parsed > 0 ? parsed : fallback;
    };
    const uint host_width = positive_option(host_width_option, 1920);
    const uint host_height = positive_option(host_height_option, 1080);
    const uint host_fps_num = positive_option(host_fps_num_option, 60);
    const uint host_fps_den = positive_option(host_fps_den_option, 1);
    const double host_frame_rate =
        static_cast<double>(host_fps_num) / host_fps_den;

    fxm::editor::StartupProgress startup_progress;
    register_startup_stages(startup_progress);
    startup_progress.beginStage(QStringLiteral("application"));

    const QString standalone_data_root =
        fxm::editor::resolve_standalone_data_root(parser.value(data_option));
    fxm::editor::StandaloneEditorHost editor_host(nullptr);
    fxm::editor::StandaloneAssetPathProvider asset_paths(
        standalone_data_root.toStdString());
    fxm::editor::StandaloneHostContextProvider host_context(
        parser.value(config_option).toStdString(),
        parser.value(scope_option).toStdString());
    fxm::editor::StandaloneFrameRateProvider frame_rate(
        plugin_hosted && host_format_supplied ? host_frame_rate : 30.0,
        !plugin_hosted || !host_format_supplied);
    fxm::editor::StandaloneLogger logger;
    fxm::editor::StandaloneTranslationProvider translations(
        standalone_data_root.toStdString());

    fxm::set_logger(&logger);

    /* A standalone installation must produce diagnostics on its first run.
     * Preserve an explicit user choice, but enable the existing logger when
     * the preference has never been configured. */
    QSettings logging_settings(QStringLiteral("FluxMotion"),
                               QStringLiteral("Dock"));
    logging_settings.beginGroup(QStringLiteral("Logging"));
    if (!logging_settings.contains(QStringLiteral("enabled")))
        logging_settings.setValue(QStringLiteral("enabled"), true);
    logging_settings.endGroup();
    logging_settings.sync();
    StandaloneLogSession log_session;
    fxm::set_host_context_provider(&host_context);
    fxm::set_asset_path_provider(&asset_paths);
    fxm_set_asset_path_provider(&asset_paths);
    fxm_set_translation_provider(&translations);
    fxm::editor_host::set_editor_host(&editor_host);
    fxm::set_frame_rate_provider(&frame_rate);
    TimecodeSpinBox::set_frame_rate_provider(&frame_rate);
    application.setFont(fxm_satoshi_ui_font());
    application.setWindowIcon(fxm_brand_icon());
    FXM_LOG_INFO("Plugin",
                 QStringLiteral("Flux Motion standalone resources: %1")
                     .arg(standalone_data_root));

    if (launch.mode == fxm::editor::LaunchMode::GraphicsSmokeTest) {
        const QString translation_key = QStringLiteral("OBSTitles.About");
        const QString translated_about = fxm_tr("OBSTitles.About");
        const bool locale_ready = !translated_about.isEmpty() &&
                                  translated_about != translation_key;
        const bool icon_ready = !fxm_brand_icon().isNull();
        const bool about_ready = !fxm_about_graphic_pixmap(
            QSize(760, 292), 1.0).isNull();
        if (!locale_ready || !icon_ready || !about_ready) {
            const QByteArray message = QStringLiteral(
                "Standalone deployment resources failed "
                "(root=%1 locale=%2 icon=%3 about=%4).")
                .arg(standalone_data_root)
                .arg(locale_ready)
                .arg(icon_ready)
                .arg(about_ready)
                .toUtf8();
            std::fprintf(stderr, "%s\n", message.constData());
            FXM_LOG_ERROR("Plugin", QString::fromUtf8(message));
            fxm::set_frame_rate_provider(nullptr);
            fxm::editor_host::set_editor_host(nullptr);
            fxm::set_asset_path_provider(nullptr);
            fxm::set_host_context_provider(nullptr);
            fxm::set_logger(nullptr);
            return 4;
        }
    }

    std::unique_ptr<QSplashScreen> splash;
    fxm::editor::StartupProgressScreen *progress_screen = nullptr;
    if (launch.mode != fxm::editor::LaunchMode::GraphicsSmokeTest) {
        const qreal dpr = application.primaryScreen()
            ? application.primaryScreen()->devicePixelRatio() : 1.0;
        const QPixmap artwork = fxm_about_graphic_pixmap(QSize(760, 292), dpr);
        if (!artwork.isNull()) {
            auto progress_splash =
                std::make_unique<fxm::editor::StartupProgressScreen>(artwork);
            progress_screen = progress_splash.get();
            splash = std::move(progress_splash);
            progress_screen->bind(startup_progress);
            splash->show();
            application.processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    startup_progress.completeStage(QStringLiteral("application"));

    startup_progress.beginStage(QStringLiteral("configuration"));
    startup_progress.completeStage(QStringLiteral("configuration"));

    startup_progress.beginStage(QStringLiteral("preferences"));
    fxm::editor::apply_editor_theme(application,
                                    fxm::editor::current_editor_theme());
    startup_progress.completeStage(QStringLiteral("preferences"));

    startup_progress.beginStage(QStringLiteral("rendering"));
    auto obs_graphics = std::make_unique<fxm::editor::StandaloneObsGraphicsRuntime>(
        parser.value(obs_bin_option),
        plugin_hosted && host_format_supplied ? host_width : 1920,
        plugin_hosted && host_format_supplied ? host_height : 1080,
        plugin_hosted && host_format_supplied ? host_fps_num : 60,
        plugin_hosted && host_format_supplied ? host_fps_den : 1);
    if (!obs_graphics->ready()) {
        if (splash)
            splash->close();
        if (launch.mode == fxm::editor::LaunchMode::GraphicsSmokeTest) {
            const QByteArray message = obs_graphics->error().toUtf8();
            std::fprintf(stderr, "%s\n", message.constData());
        } else {
            QMessageBox::critical(nullptr, QStringLiteral("Flux Motion"),
                                  obs_graphics->error());
        }
        fxm::set_frame_rate_provider(nullptr);
        fxm::editor_host::set_editor_host(nullptr);
        fxm::set_asset_path_provider(nullptr);
        fxm::set_host_context_provider(nullptr);
        fxm::set_logger(nullptr);
        return 2;
    }
    startup_progress.completeStage(QStringLiteral("rendering"));

    if (launch.mode == fxm::editor::LaunchMode::GraphicsSmokeTest) {
        Title smoke_title;
        smoke_title.id = "standalone-gpu-smoke";
        smoke_title.name = "Standalone GPU smoke";
        smoke_title.width = 320;
        smoke_title.height = 180;
        smoke_title.frame_rate = 24.0;
        smoke_title.duration = 1.0;
        /* Exercise a real layer-copy pass. An empty title only validates the
         * render target and would miss shader compilation regressions such as
         * the multi-minute D3D optimizer stall fixed in Development 420. */
        auto smoke_layer = std::make_shared<Layer>();
        smoke_layer->id = "standalone-gpu-smoke-layer";
        smoke_layer->name = "GPU smoke rectangle";
        smoke_layer->type = LayerType::SolidRect;
        smoke_layer->position.static_value = {32.0, 24.0};
        smoke_layer->rect_width = 128.0f;
        smoke_layer->rect_height = 72.0f;
        smoke_layer->size.static_value = {128.0, 72.0};
        smoke_layer->fill_color = 0xFFFFFFFF;
        smoke_layer->fill_color_a.static_value = 255.0;
        smoke_layer->fill_color_r.static_value = 255.0;
        smoke_layer->fill_color_g.static_value = 255.0;
        smoke_layer->fill_color_b.static_value = 255.0;
        smoke_layer->out_time = smoke_title.duration;
        smoke_title.layers.push_back(smoke_layer);
        TitleGpuRenderSession *session = title_gpu_render_session_create();
        bool rendered = session != nullptr;
        QSize first_size;
        QSize second_size;
        const auto render_extent = [&](uint32_t width, uint32_t height,
                                       double sample_duration,
                                       QSize &resolved_size) {
            if (!session)
                return false;
            title_gpu_render_session_set_render_request(
                session, TitleGpuRenderRequest{width, height,
                                               sample_duration});
            for (int attempt = 0; attempt < 200; ++attempt) {
                title_gpu_render_session_update(session, smoke_title, 0.25, 1);
                const QImage image =
                    title_gpu_render_session_readback(session);
                resolved_size = image.size();
                if (resolved_size == QSize(static_cast<int>(width),
                                           static_cast<int>(height)))
                    return true;
                QThread::msleep(25);
            }
            return false;
        };
        if (rendered)
            rendered = render_extent(320, 180, 1.0 / 24.0, first_size);
        if (rendered)
            rendered = render_extent(640, 360, 1.0 / 60.0, second_size);
        const std::string render_error = session
            ? title_gpu_render_session_last_error(session) : std::string();
        title_gpu_render_session_destroy(session);
        if (!rendered) {
            std::fprintf(stderr,
                         "Standalone GPU compositor render smoke failed "
                         "(first=%dx%d second=%dx%d error=%s).\n",
                         first_size.width(), first_size.height(),
                         second_size.width(), second_size.height(),
                         render_error.c_str());
            return 3;
        }
        return 0;
    }

    startup_progress.beginStage(QStringLiteral("media"));
    fxm::rendering::set_title_preview_renderer(
        &fxm::obs_plugin::obs_title_preview_renderer());
    startup_progress.completeStage(QStringLiteral("media"));

    startup_progress.beginStage(QStringLiteral("plugins"));
    FxmEffectExtensionCatalog::instance().reload();
    startup_progress.completeStage(QStringLiteral("plugins"));

    startup_progress.beginStage(QStringLiteral("fonts"));
    QFontDatabase font_database;
    (void)font_database.families();
    startup_progress.completeStage(QStringLiteral("fonts"));

    startup_progress.beginStage(QStringLiteral("effects"));
    (void)builtin_effect_descriptors();
    startup_progress.completeStage(QStringLiteral("effects"));

    startup_progress.beginStage(QStringLiteral("workspace"));
    TitleDataStore::instance().load();
    startup_progress.completeStage(QStringLiteral("workspace"));

    int result = 0;
    std::unique_ptr<fxm::editor::StandaloneApplicationWindow>
        application_window;
    std::unique_ptr<TitleEditor> editor;
    std::unique_ptr<fxm::editor::StandaloneWelcomeScreen> welcome;

    startup_progress.beginStage(QStringLiteral("ui"));
    if (launch.mode == fxm::editor::LaunchMode::Preferences) {
        TitleEditor::show_global_preferences(nullptr);
        startup_progress.completeStage(QStringLiteral("ui"));
        startup_progress.setReady();
        if (splash)
            splash->close();
        result = application.exec();
    } else {
        bool run_event_loop = true;
        const auto open_editor = [&](const QString &requested_id,
                                     const QString &project_path,
                                     bool unsaved_project) -> bool {
            const std::string title_id = requested_id.toStdString();
            if (title_id.empty() ||
                !TitleDataStore::instance().get_title(title_id)) {
                QMessageBox::warning(
                    application_window
                        ? static_cast<QWidget *>(application_window.get())
                        : static_cast<QWidget *>(welcome.get()),
                    QStringLiteral("Flux Motion"),
                    QStringLiteral("The selected project is no longer available."));
                return false;
            }
            fxm::editor::ProjectLoadingDialog editor_loading(
                application_window
                    ? static_cast<QWidget *>(application_window.get())
                    : static_cast<QWidget *>(welcome.get()));
            editor_loading.setWindowTitle(
                QStringLiteral("Preparing Flux Motion Editor"));
            editor_loading.setCancellable(false);
            editor_loading.updateEditorProgress(
                QStringLiteral("Building editor interface"), -1);
            const auto context = launch.mode == fxm::editor::LaunchMode::PluginHosted
                ? fxm::editor::EditorExecutionContext::obsPlugin(
                      host_format_supplied ? static_cast<int>(host_width) : 0,
                      host_format_supplied ? static_cast<int>(host_height) : 0,
                      host_format_supplied ? host_frame_rate : 0.0)
                : fxm::editor::EditorExecutionContext::standalone();
            editor = application_window
                ? std::make_unique<TitleEditor>(application_window.get(),
                                                Qt::Widget, context)
                : std::make_unique<TitleEditor>(nullptr, Qt::Window, context);
            editor->setObjectName(QStringLiteral("FluxMotionStandaloneEditor"));
            editor_loading.updateEditorProgress(
                QStringLiteral("Loading title"), 72);
            editor_host.set_main_window(application_window
                ? static_cast<QMainWindow *>(application_window.get())
                : static_cast<QMainWindow *>(editor.get()));
            editor->open_title(title_id);
            editor_loading.updateEditorProgress(
                QStringLiteral("Finalizing editor layout"), 92);
            if (application_window) {
                const auto stored = TitleDataStore::instance().get_title(title_id);
                application_window->showEditor(
                    editor.get(), stored
                        ? QString::fromStdString(stored->name) : QString());
                if (unsaved_project)
                    editor->begin_unsaved_project();
                else if (!project_path.isEmpty())
                    editor->set_project_file_path(project_path);
                QObject::connect(
                    editor.get(), &TitleEditor::editor_closed,
                    application_window.get(), [&]() {
                        if (welcome) {
                            welcome->setProjects(available_projects());
                            application_window->showHome(welcome.get());
                        }
                    });
                QObject::connect(
                    editor.get(), &TitleEditor::recent_projects_changed,
                    application_window.get(), [&]() {
                        if (welcome)
                            welcome->setProjects(available_projects());
                    });
            } else {
                if (unsaved_project)
                    editor->begin_unsaved_project();
                else if (!project_path.isEmpty())
                    editor->set_project_file_path(project_path);
                editor->show();
            }
            editor->ensurePolished();
            if (editor->layout())
                editor->layout()->activate();
            editor_loading.updateEditorProgress(
                QStringLiteral("Editor ready"), 100);
            editor_loading.close();
            return true;
        };

        if (launch.mode == fxm::editor::LaunchMode::StandaloneWelcome) {
            application_window = std::make_unique<
                fxm::editor::StandaloneApplicationWindow>();
            welcome =
                std::make_unique<fxm::editor::StandaloneWelcomeScreen>(
                    application_window.get());
            welcome->setProjects(available_projects());
            welcome->setCreateProjectHandler([&]() {
                fxm::editor::StandaloneNewTitleSettings settings;
                if (!fxm::editor::prompt_standalone_new_title_settings(
                        application_window.get(), settings))
                    return;
                const auto created = TitleDataStore::instance().create_title(
                    "Title 1");
                if (!created) {
                    QMessageBox::warning(
                        application_window.get(), QStringLiteral("Flux Motion"),
                        QStringLiteral("Could not create a new project."));
                    return;
                }
                fxm::editor::apply_standalone_new_title_settings(
                    created, settings);
                TitleProject project;
                project.id = TitleDataStore::make_uuid();
                project.name = "Untitled Project";
                project.active_title_id = created->id;
                project.titles = {created};
                TitleDataStore::instance().activate_project(project);
                open_editor(QString::fromStdString(created->id), QString(), true);
            });
            welcome->setOpenProjectHandler(
                [&](const QString &path) {
                    QString selected_path = path;
                    if (selected_path.isEmpty()) {
                        selected_path = QFileDialog::getOpenFileName(
                            application_window.get(),
                            QStringLiteral("Open Flux Motion Project"), QString(),
                            QStringLiteral("Flux Motion Projects (*.fxmproj *.fxmt *.fxmp);;All Files (*)"));
                    }
                    if (selected_path.isEmpty())
                        return;
                    fxm::editor::ProjectLoadingDialog loading(
                        application_window.get());
                    TitleProject project;
                    std::string error;
                    const bool loaded = TitleDataStore::instance().read_project(
                        selected_path.toStdString(), &project, &error,
                        [&loading](const ProjectLoadProgress &state) {
                            return loading.updateProgress(state);
                        });
                    loading.close();
                    if (!loaded ||
                        !TitleDataStore::instance().activate_project(project)) {
                        fxm::editor::RecentProjects::remove(selected_path);
                        QMessageBox::warning(
                            application_window.get(), QStringLiteral("Flux Motion"),
                            error.empty() ? QStringLiteral("Could not open the selected project.")
                                          : QString::fromStdString(error));
                        welcome->setProjects(available_projects());
                        return;
                    }
                    TitleDataStore::instance().save_async();
                    const std::string active_id = project.active_title_id.empty()
                        ? project.titles.front()->id : project.active_title_id;
                    open_editor(QString::fromStdString(active_id),
                                QFileInfo(selected_path).absoluteFilePath(), false);
                    if (editor)
                        editor->set_project_context(
                            project, QFileInfo(selected_path).absoluteFilePath());
                });
            application_window->showHome(welcome.get());
            editor_host.set_main_window(application_window.get());
            application_window->show();
            startup_progress.completeStage(QStringLiteral("ui"));
            startup_progress.setReady();
            if (splash)
                splash->finish(application_window.get());
        } else if (launch.mode == fxm::editor::LaunchMode::StandaloneFile) {
            application_window = std::make_unique<
                fxm::editor::StandaloneApplicationWindow>();
            editor_host.set_main_window(application_window.get());
            fxm::editor::ProjectLoadingDialog loading(application_window.get());
            TitleProject project;
            std::string error;
            const bool loaded = TitleDataStore::instance().read_project(
                launch.project_path.toStdString(), &project, &error,
                [&loading](const ProjectLoadProgress &state) {
                    return loading.updateProgress(state);
                });
            loading.close();
            if (!loaded || project.titles.empty() ||
                !TitleDataStore::instance().activate_project(project)) {
                if (splash)
                    splash->close();
                QMessageBox::warning(
                    application_window.get(), QStringLiteral("Flux Motion"),
                    error.empty() ? QStringLiteral("Could not open the selected project.")
                                  : QString::fromStdString(error));
                run_event_loop = false;
            } else {
                TitleDataStore::instance().save_async();
                const std::string active_id = project.active_title_id.empty()
                    ? project.titles.front()->id : project.active_title_id;
                run_event_loop = open_editor(
                    QString::fromStdString(active_id), launch.project_path, false);
                if (editor)
                    editor->set_project_context(project, launch.project_path);
                application_window->show();
                startup_progress.completeStage(QStringLiteral("ui"));
                startup_progress.setReady();
                if (splash)
                    splash->finish(application_window.get());
            }
        } else {
            QString requested_id = launch.project_id;
            if (requested_id.isEmpty() &&
                launch.mode == fxm::editor::LaunchMode::PluginHosted) {
                requested_id = QString::fromStdString(first_or_new_project_id());
            }
            run_event_loop = open_editor(requested_id, QString(), false);
            startup_progress.completeStage(QStringLiteral("ui"));
            startup_progress.setReady();
            if (splash && editor)
                splash->finish(editor.get());
            else if (splash)
                splash->close();
        }
        result = run_event_loop ? application.exec() : 1;
        editor_host.set_main_window(nullptr);
        editor.reset();
        welcome.reset();
        application_window.reset();
    }

    startup_progress.setUpdateCallback({});
    CacheManager::instance().shutdownWorker();
    TitleDataStore::instance().shutdownSaveWorker();
    TitleDataStore::instance().save();
    FxmEffectExtensionCatalog::instance().shutdown();
    fxm::rendering::set_title_preview_renderer(nullptr);
    fxm::editor_host::set_editor_host(nullptr);
    fxm::set_asset_path_provider(nullptr);
    fxm::set_host_context_provider(nullptr);
    fxm::set_logger(nullptr);
    return result;
}
