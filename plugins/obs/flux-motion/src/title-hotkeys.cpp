#include "title-hotkeys.h"
#include "title-data.h"
#include "live-text-cue-utils.h"
#include "cache/cache-manager.h"
#include "image-layer-utils.h"
#include "ticker-runtime.h"
#include "external-data.h"
#include "external-data-provider.h"
#include "title-logger.h"
#include <obs-module.h>
#include <QSettings>
#include <QTimer>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

using fxm::live_text::exposed_text_layers;
using fxm::live_text::live_cue_layer_value;
using fxm::live_text::normalize_live_text_rows;

enum class HotkeyAction {
    CueRow,
    NextCue,
    PreviousCue,
    CueToProgram,
    Uncue,
    CueLast,
    TickerTogglePause,
    TickerStop,
};

struct HotkeyDescriptor {
    std::string name;
    std::string description;
    std::string title_id;
    HotkeyAction action = HotkeyAction::CueRow;
    int row = -1;
    std::string layer_id;
};

struct HotkeyRegistration {
    obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
    HotkeyDescriptor descriptor;
};

struct HotkeySection {
    std::string title_id;
    std::string display_name;
    /* Lightweight source used only as the OBS Hotkeys settings section. */
    obs_source_t *source = nullptr;
};

std::vector<HotkeySection> g_sections;
std::vector<HotkeyRegistration> g_hotkeys;
std::map<std::string, obs_source_t *> g_section_sources;
std::string g_hotkey_signature;
std::map<std::string, std::string> g_persisted_hotkey_bindings;
bool g_hotkeys_active = false;
uint64_t g_change_callback_id = 0;
bool g_hotkey_section_source_registered = false;
QSet<QString> g_pending_cue_hydration;
TitleProgramHotkeyHandler g_program_command_handler;
std::map<std::string, int> g_last_cued_rows;

constexpr const char *kHotkeySectionSourceId = "flux_motion_hotkey_section";
constexpr const char *kDockSettingsGroup = "TitleDock";
constexpr const char *kBackgroundPersistenceKey = "backgroundPersistence";
constexpr const char *kTextPersistenceKey = "textPersistence";
constexpr const char *kHotkeySettingsGroup = "Hotkeys";

static void load_persisted_hotkey_bindings()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kHotkeySettingsGroup));
    g_persisted_hotkey_bindings.clear();
    for (const auto &key : settings.childKeys())
        g_persisted_hotkey_bindings[key.toStdString()] = settings.value(key).toString().toStdString();
    settings.endGroup();
}

static void save_persisted_hotkey_bindings()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kHotkeySettingsGroup));
    settings.remove(QString());
    for (const auto &[name, json] : g_persisted_hotkey_bindings)
        settings.setValue(QString::fromStdString(name), QString::fromStdString(json));
    settings.endGroup();
}

static void remember_hotkey_binding(const HotkeyRegistration &hotkey)
{
    if (hotkey.id == OBS_INVALID_HOTKEY_ID) return;
    obs_data_array_t *bindings = obs_hotkey_save(hotkey.id);
    if (!bindings) return;

    obs_data_t *wrapper = obs_data_create();
    obs_data_set_array(wrapper, "bindings", bindings);
    const char *json = obs_data_get_json(wrapper);
    if (json && *json)
        g_persisted_hotkey_bindings[hotkey.descriptor.name] = json;

    obs_data_release(wrapper);
    obs_data_array_release(bindings);
}

static void restore_hotkey_binding(const HotkeyRegistration &hotkey)
{
    if (hotkey.id == OBS_INVALID_HOTKEY_ID) return;
    auto it = g_persisted_hotkey_bindings.find(hotkey.descriptor.name);
    if (it == g_persisted_hotkey_bindings.end() || it->second.empty()) return;

    obs_data_t *wrapper = obs_data_create_from_json(it->second.c_str());
    if (!wrapper) return;
    obs_data_array_t *bindings = obs_data_get_array(wrapper, "bindings");
    if (bindings) {
        obs_hotkey_load(hotkey.id, bindings);
        obs_data_array_release(bindings);
    }
    obs_data_release(wrapper);
}

static void apply_live_text_row(const std::shared_ptr<Title> &title, int row,
                                const std::vector<std::shared_ptr<Layer>> &exposed)
{
    if (!title || row < 0 || row >= (int)title->live_text_rows.size()) return;
    for (const auto &source : title->external_data_sources) {
        if (source.provider.enabled &&
            source.provider.refresh_mode == ExternalDataRefreshMode::RefreshOnCue) {
            ExternalDataProviderService::instance().configure_source(source);
            ExternalDataProviderService::instance().connect_source(source.id);
            ExternalDataProviderService::instance().refresh_source(source.id);
        }
    }
    for (int col = 0; col < (int)exposed.size(); ++col) {
        auto &target = exposed[col];
        if (!target)
            continue;
        const int value_row = target->exposed_single_value ? 0 : row;
        if (value_row < 0 || value_row >= (int)title->live_text_rows.size() ||
            col >= (int)title->live_text_rows[value_row].size())
            continue;
        apply_live_text_runtime_binding(*title, value_row, *target);
        const std::string cue_value = effective_live_text_cue_value(
            *title, value_row, *target, title->live_text_rows[value_row][col]);
        target->live_cue_hidden_if_empty = target->exposed_hide_if_empty && cue_value.empty();
        if (target->type == LayerType::Image) {
            fxm::apply_exposed_image_cue_value(*target, cue_value);
            continue;
        }
        if (target->type == LayerType::Chart) {
            fxm::live_text::apply_chart_cue_value(*target, cue_value);
            continue;
        }
        target->text_content = cue_value;
        if (target->rich_text.empty())
            target->rich_text = rich_text_document_from_layer_defaults(*target);
        RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(target->rich_text);
        rich_text_document_replace_text(target->rich_text, target->text_content, insertion_format,
                                        target->rich_text.has_typing_format
                                            ? target->rich_text.typing_format_mask : 0);
    }
}

static std::string hotkey_safe_id(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch))
            out.push_back((char)std::tolower(ch));
        else
            out.push_back('_');
    }
    return out.empty() ? std::string("untitled") : out;
}

static std::string title_display_name(const std::shared_ptr<Title> &title)
{
    return title && !title->name.empty() ? title->name : std::string("Untitled");
}

static std::string program_display_name()
{
    const char *name = obs_module_text("OBSTitles.DockName");
    return name && *name ? std::string(name) : std::string("Flux Motion");
}

static std::string title_section_name(const std::shared_ptr<Title> &title,
                                      const std::map<std::string, int> &name_counts)
{
    std::string name = title_display_name(title);
    auto it = name_counts.find(name);
    if (it != name_counts.end() && it->second > 1) {
        std::string suffix = title && !title->id.empty()
            ? title->id.substr(0, std::min<size_t>(8, title->id.size()))
            : hotkey_safe_id(name).substr(0, 8);
        name += " [" + suffix + "]";
    }

    return program_display_name() + " - " + name;
}

static std::string cue_description(int cue_number)
{
    return std::string(obs_module_text("OBSTitles.Cue")) + " " + std::to_string(cue_number);
}

static const char *hotkey_section_source_get_name(void *)
{
    return obs_module_text("OBSTitles.DockName");
}

static void register_hotkey_section_source_type()
{
    if (g_hotkey_section_source_registered) return;

    static obs_source_info si = {};
    si.id = kHotkeySectionSourceId;
    si.type = OBS_SOURCE_TYPE_INPUT;
    si.output_flags = OBS_SOURCE_CAP_DISABLED;
    si.get_name = hotkey_section_source_get_name;

    obs_register_source(&si);
    g_hotkey_section_source_registered = true;
}

static void load_persistence_settings(bool &background_persistence, bool &text_persistence)
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kDockSettingsGroup));
    background_persistence = settings.value(QString::fromUtf8(kBackgroundPersistenceKey), false).toBool();
    text_persistence = background_persistence &&
        settings.value(QString::fromUtf8(kTextPersistenceKey), false).toBool();
    settings.endGroup();
}

static void apply_persistence_settings_to_title(const std::shared_ptr<Title> &title,
                                                const std::vector<std::shared_ptr<Layer>> &exposed)
{
    if (!title) return;
    bool background_persistence = false;
    bool text_persistence = false;
    load_persistence_settings(background_persistence, text_persistence);
    const bool has_exposed = !exposed.empty();
    title->cue_background_persistence = background_persistence && has_exposed;
    title->cue_text_persistence = title->cue_background_persistence && text_persistence;
    if (!title->cue_background_persistence)
        title->cue_persistence_transition = false;
    if (!title->cue_text_persistence)
        title->cue_persistent_text_columns.clear();
}

static void cue_title_row(const std::shared_ptr<Title> &title, int row,
                          bool toggle_active = true)
{
    if (!title) return;

    auto exposed = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed);

    if (exposed.empty()) {
        const bool uncue_active = toggle_active &&
            (title->current_cue_row == 0 || title->pending_cue_row == 0);
        /* The title-level hotkey toggles the same synthetic row used by the
         * dock. Preserve current_cue_row while the outro runs. */
        title->current_cue_row = 0;
        title->pending_cue_row = -1;
        title->cue_uncue_requested = uncue_active;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
    } else {
        if (row < 0 || row >= (int)title->live_text_rows.size()) return;
        apply_persistence_settings_to_title(title, exposed);

        CacheManager &cache = CacheManager::instance();
        if (cache.cacheEnabled() &&
            cache.titleCacheability(title) != TitleCacheability::NonCacheable &&
            !cache.prepareLiveCueForPlayback(title, row)) {
            const QString hydration_key = QStringLiteral("%1:%2")
                .arg(QString::fromStdString(title->id)).arg(row);
            if (!g_pending_cue_hydration.contains(hydration_key)) {
                g_pending_cue_hydration.insert(hydration_key);
                const std::string title_id = title->id;
                QTimer::singleShot(25, [title_id, row, hydration_key,
                                        toggle_active]() {
                    g_pending_cue_hydration.remove(hydration_key);
                    cue_title_row(TitleDataStore::instance().get_title(title_id),
                                  row, toggle_active);
                });
            }
            return;
        }
        const bool is_active_cue = title->current_cue_row == row;
        const bool is_pending_cue = title->pending_cue_row == row;
        const int previous_row = title->current_cue_row >= 0 ? title->current_cue_row : title->pending_cue_row;
        const bool can_persist_transition = title->cue_background_persistence &&
            (title->playback_mode == 1 || title->playback_mode == 2) &&
            previous_row >= 0 && previous_row != row;
        const bool needs_outro_before_cue =
            (title->playback_mode == 1 || title->playback_mode == 2) &&
            title->current_cue_row >= 0 && title->current_cue_row != row;

        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.assign(exposed.size(), false);

        if (toggle_active && (is_active_cue || is_pending_cue)) {
            title->pending_cue_row = -1;
            title->cue_uncue_requested = true;
            title->cue_persistence_transition = false;
            title->cue_persistent_text_columns.clear();
        } else if (can_persist_transition) {
            title->cue_uncue_requested = false;
            for (int col = 0; col < (int)exposed.size(); ++col) {
                auto &target = exposed[col];
                const int previous_value_row = target && target->exposed_single_value ? 0 : previous_row;
                const int next_value_row = target && target->exposed_single_value ? 0 : row;
                if (title->cue_text_persistence &&
                    previous_value_row >= 0 && previous_value_row < (int)title->live_text_rows.size() &&
                    next_value_row >= 0 && next_value_row < (int)title->live_text_rows.size() &&
                    col < (int)title->live_text_rows[previous_value_row].size() &&
                    col < (int)title->live_text_rows[next_value_row].size() &&
                    title->live_text_rows[previous_value_row][col] == title->live_text_rows[next_value_row][col])
                    title->cue_persistent_text_columns[col] = true;
            }
            title->pending_cue_row = row;
            title->cue_persistence_transition = true;
        } else if (needs_outro_before_cue) {
            title->cue_uncue_requested = false;
            title->pending_cue_row = row;
        } else {
            title->cue_uncue_requested = false;
            apply_live_text_row(title, row, exposed);
            title->current_cue_row = row;
            title->pending_cue_row = -1;
        }
    }

    ++title->cue_revision;
    if (title->current_cue_row >= 0 || title->pending_cue_row >= 0) {
        g_last_cued_rows[title->id] = std::max(0, row);
        title->last_cue_row = std::max(0, row);
    }
    TitleDataStore::instance().touch_runtime_change();
}

static int last_cued_row_for_title(const std::shared_ptr<Title> &title)
{
    if (!title)
        return -1;
    const auto it = g_last_cued_rows.find(title->id);
    if (it != g_last_cued_rows.end())
        return it->second;
    if (title->last_cue_row >= 0)
        return title->last_cue_row;
    if (title->pending_cue_row >= 0)
        return title->pending_cue_row;
    if (title->current_cue_row >= 0)
        return title->current_cue_row;
    return 0;
}

static void uncue_title(const std::shared_ptr<Title> &title)
{
    if (!title)
        return;
    const int active_row = title->pending_cue_row >= 0
        ? title->pending_cue_row : title->current_cue_row;
    if (active_row < 0)
        return;
    title->pending_cue_row = -1;
    title->cue_uncue_requested = true;
    title->cue_persistence_transition = false;
    title->cue_persistent_text_columns.clear();
    ++title->cue_revision;
    TitleDataStore::instance().touch_runtime_change();
}

static bool dispatch_program_command(TitleProgramHotkeyCommand command,
                                     const std::shared_ptr<Title> &title)
{
    if (!title || !g_program_command_handler)
        return false;
    g_program_command_handler(command, title->id);
    return true;
}

static void cue_relative(const std::shared_ptr<Title> &title, int delta)
{
    if (!title || delta == 0) return;

    auto exposed = exposed_text_layers(title);
    if (exposed.empty()) return;
    normalize_live_text_rows(title, exposed);
    const int row_count = (int)title->live_text_rows.size();
    if (row_count <= 0) return;

    int base = title->pending_cue_row >= 0 ? title->pending_cue_row : title->current_cue_row;
    int row = 0;
    if (base >= 0 && base < row_count)
        row = (base + delta + row_count) % row_count;
    else if (delta < 0)
        row = row_count - 1;

    cue_title_row(title, row);
}

static void hotkey_callback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    if (!pressed || !data) return;

    const auto *descriptor = static_cast<const HotkeyDescriptor *>(data);
    auto title = TitleDataStore::instance().get_title(descriptor->title_id);
    if (!title) return;

    switch (descriptor->action) {
    case HotkeyAction::CueRow:
        cue_title_row(title, descriptor->row);
        break;
    case HotkeyAction::NextCue:
        if (!dispatch_program_command(TitleProgramHotkeyCommand::NextCue,
                                      title))
            cue_relative(title, 1);
        break;
    case HotkeyAction::PreviousCue:
        if (!dispatch_program_command(TitleProgramHotkeyCommand::PreviousCue,
                                      title))
            cue_relative(title, -1);
        break;
    case HotkeyAction::CueToProgram:
        if (!dispatch_program_command(TitleProgramHotkeyCommand::CueToProgram,
                                      title))
            cue_title_row(title, last_cued_row_for_title(title), false);
        break;
    case HotkeyAction::Uncue:
        uncue_title(title);
        break;
    case HotkeyAction::CueLast:
        cue_title_row(title, last_cued_row_for_title(title), false);
        break;
    case HotkeyAction::TickerTogglePause: {
        auto layer = title->find_layer(descriptor->layer_id);
        if (!layer || layer->type != LayerType::Ticker)
            break;
        const bool title_cued = title->current_cue_row >= 0 || title->pending_cue_row >= 0;
        fxm::ticker_runtime::toggle_pause(title->id, *layer, title_cued);
        const TickerRuntimeSnapshot state =
            fxm::ticker_runtime::status(title->id, *layer, title_cued);
        FXM_LOG_INFO("Ticker", QStringLiteral(
            "action=toggle source=hotkey title=%1 layer=%2 paused=%3")
            .arg(QString::fromStdString(title->id))
            .arg(QString::fromStdString(layer->id))
            .arg(state.paused ? 1 : 0));
        TitleDataStore::instance().touch_runtime_change();
        break;
    }
    case HotkeyAction::TickerStop: {
        auto layer = title->find_layer(descriptor->layer_id);
        if (!layer || layer->type != LayerType::Ticker)
            break;
        const bool title_cued = title->current_cue_row >= 0 || title->pending_cue_row >= 0;
        fxm::ticker_runtime::stop(title->id, *layer, title_cued);
        FXM_LOG_INFO("Ticker", QStringLiteral(
            "action=stop source=hotkey title=%1 layer=%2")
            .arg(QString::fromStdString(title->id))
            .arg(QString::fromStdString(layer->id)));
        TitleDataStore::instance().touch_runtime_change();
        break;
    }
    }
}

static std::vector<HotkeyDescriptor> build_descriptors(std::vector<HotkeySection> &sections)
{
    std::vector<HotkeyDescriptor> descriptors;
    std::map<std::string, int> name_counts;
    const auto titles = TitleDataStore::instance().title_snapshots();

    for (const auto &title : titles) {
        if (title && !title->is_asset)
            ++name_counts[title_display_name(title)];
    }

    for (const auto &title : titles) {
        if (!title || title->is_asset) continue;

        const std::string safe_title_id = hotkey_safe_id(title->id);
        const std::string section_name = title_section_name(title, name_counts);
        sections.push_back({title->id, section_name, nullptr});

        descriptors.push_back({
            "flux_motion." + safe_title_id + ".program.cue",
            obs_module_text("OBSTitles.CueToProgramHotkey"),
            title->id,
            HotkeyAction::CueToProgram,
            -1,
        });
        descriptors.push_back({
            "flux_motion." + safe_title_id + ".program.uncue",
            obs_module_text("OBSTitles.UncueHotkey"),
            title->id,
            HotkeyAction::Uncue,
            -1,
        });
        descriptors.push_back({
            "flux_motion." + safe_title_id + ".program.cue_last",
            obs_module_text("OBSTitles.CueLastHotkey"),
            title->id,
            HotkeyAction::CueLast,
            -1,
        });

        for (const auto &layer : title->layers) {
            if (!layer || layer->type != LayerType::Ticker)
                continue;
            const std::string safe_layer_id = hotkey_safe_id(layer->id);
            const std::string ticker_name = layer->name.empty()
                ? std::string(obs_module_text("OBSTitles.Ticker"))
                : layer->name;
            descriptors.push_back({
                "flux_motion." + safe_title_id + ".ticker." +
                    safe_layer_id + ".toggle",
                ticker_name + " - " + obs_module_text("OBSTitles.TickerToggleHotkey"),
                title->id,
                HotkeyAction::TickerTogglePause,
                -1,
                layer->id,
            });
            descriptors.push_back({
                "flux_motion." + safe_title_id + ".ticker." +
                    safe_layer_id + ".stop",
                ticker_name + " - " + obs_module_text("OBSTitles.TickerStopHotkey"),
                title->id,
                HotkeyAction::TickerStop,
                -1,
                layer->id,
            });
        }

        auto exposed = exposed_text_layers(title);
        normalize_live_text_rows(title, exposed);

        if (exposed.empty()) {
            descriptors.push_back({
                "flux_motion." + safe_title_id + ".cue.title",
                obs_module_text("OBSTitles.Cue"),
                title->id,
                HotkeyAction::CueRow,
                -1,
            });
            continue;
        }

        descriptors.push_back({
            "flux_motion." + safe_title_id + ".cue.next",
            obs_module_text("OBSTitles.NextCue"),
            title->id,
            HotkeyAction::NextCue,
            -1,
        });
        descriptors.push_back({
            "flux_motion." + safe_title_id + ".cue.previous",
            obs_module_text("OBSTitles.PreviousCue"),
            title->id,
            HotkeyAction::PreviousCue,
            -1,
        });

        for (int row = 0; row < (int)title->live_text_rows.size(); ++row) {
            descriptors.push_back({
                "flux_motion." + safe_title_id + ".cue." + std::to_string(row + 1),
                cue_description(row + 1),
                title->id,
                HotkeyAction::CueRow,
                row,
            });
        }
    }

    return descriptors;
}

static std::string descriptor_signature(const std::vector<HotkeyDescriptor> &descriptors,
                                        const std::vector<HotkeySection> &sections)
{
    std::ostringstream out;
    for (const auto &section : sections) {
        out << "section" << '\t'
            << section.title_id << '\t'
            << section.display_name << '\n';
    }
    for (const auto &descriptor : descriptors) {
        out << descriptor.name << '\t'
            << descriptor.description << '\t'
            << descriptor.title_id << '\t'
            << (int)descriptor.action << '\t'
            << descriptor.row << '\t'
            << descriptor.layer_id << '\n';
    }
    return out.str();
}

static void unregister_all_hotkeys()
{
    for (auto &hotkey : g_hotkeys) {
        if (hotkey.id != OBS_INVALID_HOTKEY_ID) {
            remember_hotkey_binding(hotkey);
            obs_hotkey_unregister(hotkey.id);
        }
    }
    save_persisted_hotkey_bindings();
    g_hotkeys.clear();
    g_hotkey_signature.clear();
}

static void release_hotkey_section_sources()
{
    for (auto &[title_id, source] : g_section_sources) {
        (void)title_id;
        if (source) {
            obs_source_remove(source);
            obs_source_release(source);
        }
    }
    g_section_sources.clear();
    g_sections.clear();
}

static obs_source_t *hotkey_section_source_for(const HotkeySection &section)
{
    auto existing = g_section_sources.find(section.title_id);
    if (existing != g_section_sources.end())
        return existing->second;

    obs_source_t *source = obs_source_create(kHotkeySectionSourceId,
                                             section.display_name.c_str(),
                                             nullptr,
                                             nullptr);
    if (source) {
        obs_source_set_hidden(source, true);
        g_section_sources[section.title_id] = source;
    }
    return source;
}

static void refresh_hotkeys()
{
    if (!g_hotkeys_active) return;

    std::vector<HotkeySection> sections;
    auto descriptors = build_descriptors(sections);
    std::string signature = descriptor_signature(descriptors, sections);
    if (signature == g_hotkey_signature) return;

    unregister_all_hotkeys();
    g_sections = std::move(sections);

    std::map<std::string, obs_source_t *> section_sources;
    for (auto &section : g_sections) {
        section.source = hotkey_section_source_for(section);
        if (section.source)
            section_sources[section.title_id] = section.source;
    }

    g_hotkeys.reserve(descriptors.size());
    for (auto &descriptor : descriptors) {
        auto source_it = section_sources.find(descriptor.title_id);
        if (source_it == section_sources.end() || !source_it->second)
            continue;

        g_hotkeys.push_back({OBS_INVALID_HOTKEY_ID, std::move(descriptor)});
        auto &registration = g_hotkeys.back();
        registration.id = obs_hotkey_register_source(
            source_it->second,
            registration.descriptor.name.c_str(),
            registration.descriptor.description.c_str(),
            hotkey_callback,
            &registration.descriptor);
        restore_hotkey_binding(registration);
    }
    g_hotkey_signature = std::move(signature);
}

} // namespace

void title_hotkeys_register()
{
    if (g_hotkeys_active) return;
    register_hotkey_section_source_type();
    load_persisted_hotkey_bindings();
    g_hotkeys_active = true;
    refresh_hotkeys();
    if (g_change_callback_id == 0)
        g_change_callback_id = TitleDataStore::instance().on_change([]() { refresh_hotkeys(); });
}

void title_hotkeys_unregister()
{
    g_hotkeys_active = false;
    TitleDataStore::instance().remove_change_callback(g_change_callback_id);
    g_change_callback_id = 0;
    unregister_all_hotkeys();
    release_hotkey_section_sources();
}

void title_hotkeys_set_program_command_handler(TitleProgramHotkeyHandler handler)
{
    g_program_command_handler = std::move(handler);
}
