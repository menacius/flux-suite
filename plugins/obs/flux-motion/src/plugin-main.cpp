/*
 * Flux Motion Plugin - plugin-main.cpp
 * Entry point: registers the source, dock, and module lifecycle.
 */

#include "plugin-main.h"
#include "title-source.h"
#include "stinger-transition.h"
#include "title-audio-runtime.h"
#if !defined(FXM_OBS_PLUGIN_WITH_DOCK)
#define FXM_OBS_PLUGIN_WITH_DOCK 1
#endif
#if FXM_OBS_PLUGIN_WITH_DOCK
#include "title-dock.h"
#include "scene-mask-dock.h"
#include "title-assets.h"
#include "obs-title-preview-renderer.h"
#include "obs-editor-host.h"
#include "timecode-spinbox.h"
#endif
#include "title-hotkeys.h"
#include "title-data.h"
#include "external-data-provider.h"
#include "external-data-log.h"
#include "title-localization.h"
#include "title-logger.h"
#include "title-preferences.h"
#include "title-text-layout.h"
#include "title-text-layout-qt-font-registry.h"
#include "pattern-resource-cache.h"
#include "performance-counters.h"
#include "obs-asset-path-provider.h"
#include "asset-path-provider.h"
#include "obs-frame-rate-provider.h"
#include "obs-translation-provider.h"
#include "obs-host-context-provider.h"
#include "obs-logger.h"
#include "obs-plugin-preferences.h"
#include "cache-manager.h"
#include "build-info.h"
#include "extensions/effect-extension-catalog.h"
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QString>
#if FXM_OBS_PLUGIN_WITH_DOCK
#include <QByteArray>
#include <QMainWindow>
#include <QMessageBox>
#include <QAction>
#include <QDockWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

/* ── forward declarations ───────────────────────────────────────── */
static void on_frontend_event(obs_frontend_event event, void *priv);

/* ── module globals ─────────────────────────────────────────────── */
static bool g_frontend_ready = false;
static bool g_frontend_exiting = false;
#if FXM_OBS_PLUGIN_WITH_DOCK
static TitleDock *g_dock = nullptr;
static SceneMaskDock *g_scene_mask_dock = nullptr;
static QAction *g_dock_menu_action = nullptr;
static QAction *g_scene_mask_dock_menu_action = nullptr;
constexpr int kObsDockLayoutStateVersion = 1;
constexpr const char *kObsDockLayoutSettingsGroup = "ObsDockLayout";
constexpr const char *kObsMainWindowStateKey = "mainWindowState";

static void open_preferences_from_tools_menu(void *)
{
    QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
    fxm::obs_plugin::show_plugin_preferences(main);
}

static void save_obs_dock_layout(QMainWindow *main)
{
    if (!main || (!g_dock && !g_scene_mask_dock))
        return;

    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kObsDockLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kObsMainWindowStateKey),
                      main->saveState(kObsDockLayoutStateVersion));
    settings.endGroup();
    settings.sync();
}

static void restore_obs_dock_layout(QMainWindow *main)
{
    if (!main || (!g_dock && !g_scene_mask_dock))
        return;

    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kObsDockLayoutSettingsGroup));
    const QByteArray state = settings.value(QString::fromUtf8(kObsMainWindowStateKey)).toByteArray();
    settings.endGroup();
    if (!state.isEmpty())
        main->restoreState(state, kObsDockLayoutStateVersion);
}


static QMenu *find_docks_menu(QMainWindow *main)
{
    if (!main || !main->menuBar()) return nullptr;
    for (auto *menu : main->menuBar()->findChildren<QMenu *>()) {
        QString title = menu->title();
        title.remove('&');
        if (title.compare(fxm_tr("OBSTitles.DocksMenu"), Qt::CaseInsensitive) == 0)
            return menu;
    }
    return nullptr;
}

static void destroy_dock_ui(bool frontend_api_available = true)
{
    if (frontend_api_available && g_dock) {
        if (auto *main = qobject_cast<QMainWindow *>(g_dock->parentWidget()))
            save_obs_dock_layout(main);
    }

    if (g_dock_menu_action) {
        QObject::disconnect(g_dock_menu_action, nullptr, nullptr, nullptr);
        if (QWidget *owner = qobject_cast<QWidget *>(g_dock_menu_action->parent()))
            owner->removeAction(g_dock_menu_action);
        delete g_dock_menu_action;
        g_dock_menu_action = nullptr;
    }

    if (g_scene_mask_dock_menu_action) {
        QObject::disconnect(g_scene_mask_dock_menu_action, nullptr, nullptr, nullptr);
        if (QWidget *owner = qobject_cast<QWidget *>(
                g_scene_mask_dock_menu_action->parent()))
            owner->removeAction(g_scene_mask_dock_menu_action);
        delete g_scene_mask_dock_menu_action;
        g_scene_mask_dock_menu_action = nullptr;
    }

    if (g_dock) {
        QObject::disconnect(g_dock, nullptr, nullptr, nullptr);
        if (frontend_api_available)
            obs_frontend_remove_dock("flux-motion-dock");
        delete g_dock;
        g_dock = nullptr;
    }
    if (g_scene_mask_dock) {
        QObject::disconnect(g_scene_mask_dock, nullptr, nullptr, nullptr);
        if (frontend_api_available)
            obs_frontend_remove_dock("flux-motion-scene-masks-dock");
        delete g_scene_mask_dock;
        g_scene_mask_dock = nullptr;
    }
}

static void add_docks_menu_entry(QMainWindow *main)
{
    QMenu *docks_menu = find_docks_menu(main);
    if (!docks_menu || !g_dock || g_dock_menu_action) return;

    g_dock_menu_action = docks_menu->addAction(fxm_brand_icon(), fxm_tr("OBSTitles.DockName"));
    g_dock_menu_action->setObjectName("flux-motion-docks-menu-action");
    g_dock_menu_action->setCheckable(true);
    g_dock_menu_action->setChecked(g_dock->isVisible());
    QObject::connect(g_dock_menu_action, &QAction::triggered, g_dock,
                     [](bool visible) { if (g_dock) g_dock->setVisible(visible); });
    QObject::connect(g_dock, &QDockWidget::visibilityChanged, g_dock_menu_action,
                     [](bool visible) {
                         if (!g_dock_menu_action) return;
                         QSignalBlocker blocker(g_dock_menu_action);
                         g_dock_menu_action->setChecked(visible);
                     });

    if (!g_scene_mask_dock || g_scene_mask_dock_menu_action)
        return;
    g_scene_mask_dock_menu_action = docks_menu->addAction(
        fxm_brand_icon(), QStringLiteral("Flux Motion — Scene Masks"));
    g_scene_mask_dock_menu_action->setObjectName(
        "flux-motion-scene-masks-docks-menu-action");
    g_scene_mask_dock_menu_action->setCheckable(true);
    g_scene_mask_dock_menu_action->setChecked(g_scene_mask_dock->isVisible());
    QObject::connect(g_scene_mask_dock_menu_action, &QAction::triggered,
                     g_scene_mask_dock, [](bool visible) {
                         if (g_scene_mask_dock)
                             g_scene_mask_dock->setVisible(visible);
                     });
    QObject::connect(g_scene_mask_dock, &QDockWidget::visibilityChanged,
                     g_scene_mask_dock_menu_action, [](bool visible) {
                         if (!g_scene_mask_dock_menu_action)
                             return;
                         QSignalBlocker blocker(g_scene_mask_dock_menu_action);
                         g_scene_mask_dock_menu_action->setChecked(visible);
                     });
}
#endif

/* ── module load ────────────────────────────────────────────────── */
bool obs_module_load(void)
{
    g_frontend_exiting = false;
    fxm::set_logger(&fxm::obs_plugin::obs_logger());
    fxm::set_host_context_provider(
        &fxm::obs_plugin::obs_host_context_provider());
    fxm_set_translation_provider(
        &fxm::obs_plugin::obs_translation_provider());
    fxm::set_asset_path_provider(
        &fxm::obs_plugin::obs_asset_path_provider());
#if FXM_OBS_PLUGIN_WITH_DOCK
    fxm::editor_host::set_editor_host(
        &fxm::obs_plugin::obs_editor_host());
    title_source_set_scene_mask_controls_opener(
        &open_scene_mask_source_controls);
    TimecodeSpinBox::set_frame_rate_provider(
        &fxm::obs_plugin::obs_frame_rate_provider());
    fxm::rendering::set_title_preview_renderer(
        &fxm::obs_plugin::obs_title_preview_renderer());
    TimecodeSpinBox::set_default_tooltip(
        fxm_tr("OBSTitles.TimecodeTooltip"));
#endif
    TitleLogger::startSession();
    ExternalDataLog::set_sink(
        [](ExternalDataLogLevel level, const std::string &component,
           const std::string &message) {
            const QString formatted = QStringLiteral("component=%1 %2")
                .arg(QString::fromStdString(component), QString::fromStdString(message));
            switch (level) {
            case ExternalDataLogLevel::Error:
                FXM_LOG_ERROR("ExternalData", formatted);
                break;
            case ExternalDataLogLevel::Warning:
                FXM_LOG_WARNING("ExternalData", formatted);
                break;
            case ExternalDataLogLevel::Info:
                FXM_LOG_INFO("ExternalData", formatted);
                break;
            case ExternalDataLogLevel::Debug:
                FXM_LOG_DEBUG("ExternalData", formatted);
                break;
            case ExternalDataLogLevel::Trace:
                FXM_LOG_TRACE("ExternalData", formatted);
                break;
            }
        },
        [](ExternalDataLogLevel level) {
            return TitlePreferences::logging_enabled() &&
                   TitleLogger::categoryEnabled(QStringLiteral("ExternalData")) &&
                   static_cast<int>(level) <=
                       static_cast<int>(TitlePreferences::logging_level());
        });
    FXM_LOG_INFO("ExternalData", QStringLiteral(
        "External data diagnostics attached; values are fingerprinted and credentials are redacted"));
    blog(LOG_INFO, "[Flux Motion] Loading plugin %s", FXM_BUILD_DISPLAY);
    FXM_LOG_INFO("Plugin", QStringLiteral("Loading plugin %1").arg(QStringLiteral(FXM_BUILD_DISPLAY)));
    FxmEffectExtensionCatalog::instance().reload();

    /* OBS has not selected its scene collection yet. Loading here may scan an
     * unrelated store and blocks module initialization on first launch. The
     * FINISHED_LOADING handler restores the active collection; source ticks
     * tolerate the intervening deferred state. */

    /* 2. Register the renderable source type and title cue hotkeys */
    title_source_register();
    stinger_transition_register();
    title_hotkeys_register();

    /* 3. Add global preferences entry and defer dock/hotkey creation until the OBS UI is ready */
#if FXM_OBS_PLUGIN_WITH_DOCK
    obs_frontend_add_tools_menu_item("Flux Motion Preferences", open_preferences_from_tools_menu, nullptr);
#endif
    obs_frontend_add_event_callback(on_frontend_event, nullptr);

    blog(LOG_INFO, "[Flux Motion] Plugin loaded.");
    FXM_LOG_INFO("Plugin", QStringLiteral("Plugin loaded"));
    return true;
}

/* ── module unload ──────────────────────────────────────────────── */
void obs_module_unload(void)
{
    title_hotkeys_unregister();
    ExternalDataProviderService::instance().shutdown();
    FXM_LOG_INFO("ExternalData", QStringLiteral("External data diagnostics detached"));
    ExternalDataLog::clear_sink();
    TitleDataStore::instance().shutdownSaveWorker();
    TitleDataStore::instance().save();
    /* OBS_FRONTEND_EVENT_EXIT is the final point at which frontend API calls
     * are permitted.  A normal OBS shutdown has already removed the dock in
     * that callback, so module unload must not call remove_event_callback() or
     * remove_dock() against a frontend that is being dismantled.  Manual plugin
     * unload while OBS is still running keeps the normal frontend cleanup. */
    if (!g_frontend_exiting)
        obs_frontend_remove_event_callback(on_frontend_event, nullptr);
#if FXM_OBS_PLUGIN_WITH_DOCK
    destroy_dock_ui(!g_frontend_exiting);
    title_source_set_scene_mask_controls_opener(nullptr);
    fxm::rendering::set_title_preview_renderer(nullptr);
    fxm::editor_host::set_editor_host(nullptr);
#endif
    fxm::set_host_context_provider(nullptr);
    fxm::set_asset_path_provider(nullptr);
    fxm::set_logger(nullptr);
    /* Stop publication before rotating cache generations. This prevents an
     * in-flight prerender job from holding cache locks while shutdown clears the
     * index, and avoids deleting files on the OBS frontend thread. */
    CacheManager::instance().shutdownWorker();
    if (TitlePreferences::clear_cache_on_exit()) {
        FXM_LOG_INFO("Plugin", QStringLiteral("Detaching frame cache on module unload"));
        CacheManager::instance().clearAll();
    }
    release_title_gpu_render_resources();
    fxm::audio::SourceAudioRuntime::clear_shared_cache();
    fxm::text::clear_pattern_resource_cache();
    shared_text_layout_cache().clear();
    text_layout_clear_raw_font_registry();
#ifndef NDEBUG
    const std::string performance_snapshot = fxm::perf::snapshot_text();
    if (!performance_snapshot.empty())
        FXM_LOG_INFO("Performance", QString::fromStdString(performance_snapshot));
    fxm::perf::reset();
#endif
    FxmEffectExtensionCatalog::instance().shutdown();
    blog(LOG_INFO, "[Flux Motion] Plugin unloaded.");
    FXM_LOG_INFO("Plugin", QStringLiteral("Plugin unloaded"));
    TitleLogger::endSession();
}

/* ── frontend event handler ─────────────────────────────────────── */
static void on_frontend_event(obs_frontend_event event, void * /*priv*/)
{
    if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
        FXM_LOG_INFO("Plugin", QStringLiteral("Frontend finished loading"));
        TitleDataStore::instance().load();

#if FXM_OBS_PLUGIN_WITH_DOCK
        QMainWindow *main =
            static_cast<QMainWindow *>(obs_frontend_get_main_window());

        if (g_dock || g_scene_mask_dock)
            destroy_dock_ui();

        g_dock = new TitleDock(main);
        QObject::connect(g_dock, &QObject::destroyed, []() {
            g_dock = nullptr;
        });
        g_dock->setObjectName("FluxMotionDock");
        g_dock->setWindowTitle(fxm_tr("OBSTitles.DockName") + QStringLiteral(" — ") + QStringLiteral(FXM_BUILD_DISPLAY));

        obs_frontend_add_custom_qdock("flux-motion-dock", g_dock);
        g_scene_mask_dock = new SceneMaskDock(main);
        QObject::connect(g_scene_mask_dock, &QObject::destroyed, []() {
            g_scene_mask_dock = nullptr;
        });
        obs_frontend_add_custom_qdock("flux-motion-scene-masks-dock",
                                      g_scene_mask_dock);
        QTimer::singleShot(0, g_dock,
                           [main]() { restore_obs_dock_layout(main); });
        QObject::connect(g_dock, &QDockWidget::topLevelChanged, g_dock,
                         [main]() { save_obs_dock_layout(main); });
        QObject::connect(g_dock, &QDockWidget::dockLocationChanged, g_dock,
                         [main]() { save_obs_dock_layout(main); });
        QObject::connect(g_dock, &QDockWidget::visibilityChanged, g_dock,
                         [main]() { save_obs_dock_layout(main); });
        QObject::connect(g_scene_mask_dock, &QDockWidget::topLevelChanged,
                         g_scene_mask_dock,
                         [main]() { save_obs_dock_layout(main); });
        QObject::connect(g_scene_mask_dock, &QDockWidget::dockLocationChanged,
                         g_scene_mask_dock,
                         [main]() { save_obs_dock_layout(main); });
        QObject::connect(g_scene_mask_dock, &QDockWidget::visibilityChanged,
                         g_scene_mask_dock,
                         [main]() { save_obs_dock_layout(main); });
        add_docks_menu_entry(main);
#endif
        g_frontend_ready = true;
        title_hotkeys_register();
        blog(LOG_INFO, "[Flux Motion] Dock and title cue hotkeys registered.");
        FXM_LOG_INFO("Plugin", QStringLiteral("Dock and title cue hotkeys registered"));
    }

    if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP) {
        FXM_LOG_INFO("Plugin", QStringLiteral("Scene collection cleanup"));
        /* OBS emits a cleanup event during initial startup before the scene
         * collection has finished loading. The title store has not changed at
         * that point, so writing it is both unnecessary and vulnerable to
         * transient filesystem/antivirus locks. Real collection switches occur
         * after the frontend is ready and still save the outgoing collection. */
        if (g_frontend_ready) {
            /* Keep source output blocked for the whole cleanup→changed gap;
             * otherwise a video tick in that interval can rebuild and publish
             * the outgoing collection after it was just invalidated. */
            title_source_begin_scene_collection_transition();
            TitleDataStore::instance().save();
        } else {
            title_source_invalidate_all_presentations();
        }
        title_hotkeys_unregister();
    }

    if (event == OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED && g_frontend_ready) {
        FXM_LOG_INFO("Plugin", QStringLiteral("Scene collection changed"));
        TitleDataStore::instance().load();
        title_source_end_scene_collection_transition();
        title_hotkeys_register();
#if FXM_OBS_PLUGIN_WITH_DOCK
        if (g_dock)
            g_dock->update_scene_collection_title();
#endif
    }

    if (event == OBS_FRONTEND_EVENT_EXIT) {
        FXM_LOG_INFO("Plugin", QStringLiteral("Frontend exit"));
        g_frontend_exiting = true;
        title_source_begin_shutdown();
        /* Cache shutdown/rotation is performed once from obs_module_unload(),
         * after the prerender worker has stopped. Doing it here as well caused
         * duplicate clears while sources and the worker were still active. */
        g_frontend_ready = false;
        title_hotkeys_unregister();
        TitleDataStore::instance().shutdownSaveWorker();
        TitleDataStore::instance().save();
#if FXM_OBS_PLUGIN_WITH_DOCK
        destroy_dock_ui(true);
#endif
    }
}
