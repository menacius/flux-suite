#include "external-data-settings-dialog.h"
#include "fxm-modern-controls.h"

#include "external-data-binding-dialog.h"

#include "external-data.h"
#include "external-data-provider.h"
#include "external-data-log.h"
#include "title-localization.h"
#include "title-assets.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>
#include <cstdint>
#include <functional>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace {

static QString provider_label(ExternalDataProviderType type)
{
    return QString::fromUtf8(external_data_provider_type_name(type));
}

static QString state_label(ExternalDataConnectionState state)
{
    return QString::fromUtf8(external_data_connection_state_name(state));
}

static QString type_label(ExternalDataType type)
{
    switch (type) {
    case ExternalDataType::Integer: return QStringLiteral("Integer");
    case ExternalDataType::Float: return QStringLiteral("Float");
    case ExternalDataType::Boolean: return QStringLiteral("Boolean");
    case ExternalDataType::Color: return QStringLiteral("Color");
    case ExternalDataType::DateTime: return QStringLiteral("Date/time");
    case ExternalDataType::FilePath: return QStringLiteral("Image/file path");
    case ExternalDataType::Url: return QStringLiteral("URL");
    case ExternalDataType::String:
    default: return QStringLiteral("String");
    }
}

static QComboBox *make_type_combo(QWidget *parent, ExternalDataType selected)
{
    auto *combo = new QComboBox(parent);
    for (int value = static_cast<int>(ExternalDataType::String);
         value <= static_cast<int>(ExternalDataType::Url); ++value) {
        const auto type = static_cast<ExternalDataType>(value);
        combo->addItem(type_label(type), value);
    }
    combo->setCurrentIndex(combo->findData(static_cast<int>(selected)));
    return combo;
}

static std::map<std::string, std::string> parse_key_value_lines(const QString &text)
{
    std::map<std::string, std::string> result;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    for (const QString &raw_line : lines) {
        const QString line = raw_line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        int separator = line.indexOf(QLatin1Char('='));
        if (separator < 0)
            separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0)
            continue;
        const QString key = line.left(separator).trimmed();
        const QString value = line.mid(separator + 1).trimmed();
        if (!key.isEmpty())
            result[key.toStdString()] = value.toStdString();
    }
    return result;
}

static QString key_value_lines(const std::map<std::string, std::string> &values)
{
    QStringList lines;
    for (const auto &entry : values)
        lines.push_back(QString::fromStdString(entry.first) + QStringLiteral("=") +
                        QString::fromStdString(entry.second));
    return lines.join(QLatin1Char('\n'));
}

static bool parse_color(const QString &text, uint32_t &argb)
{
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('#')))
        value.remove(0, 1);
    if (value.size() != 6 && value.size() != 8)
        return false;
    bool ok = false;
    const qulonglong parsed = value.toULongLong(&ok, 16);
    if (!ok || parsed > UINT32_MAX)
        return false;
    argb = value.size() == 6
        ? 0xFF000000u | static_cast<uint32_t>(parsed)
        : static_cast<uint32_t>(parsed);
    return true;
}

static bool parse_value(const QString &text, ExternalDataType type,
                        ExternalDataValue &value)
{
    const QString trimmed = text.trimmed();
    switch (type) {
    case ExternalDataType::Integer: {
        bool ok = false;
        const qlonglong parsed = trimmed.toLongLong(&ok);
        if (!ok)
            return false;
        value = ExternalDataValue::integer(static_cast<int64_t>(parsed));
        return true;
    }
    case ExternalDataType::Float: {
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        if (!ok || !std::isfinite(parsed))
            return false;
        value = ExternalDataValue::floating(parsed);
        return true;
    }
    case ExternalDataType::Boolean: {
        const QString lower = trimmed.toLower();
        if (lower == QStringLiteral("true") || lower == QStringLiteral("yes") ||
            lower == QStringLiteral("on") || lower == QStringLiteral("1")) {
            value = ExternalDataValue::boolean(true);
            return true;
        }
        if (lower == QStringLiteral("false") || lower == QStringLiteral("no") ||
            lower == QStringLiteral("off") || lower == QStringLiteral("0")) {
            value = ExternalDataValue::boolean(false);
            return true;
        }
        return false;
    }
    case ExternalDataType::Color: {
        uint32_t color = 0;
        if (!parse_color(trimmed, color))
            return false;
        value = ExternalDataValue::color(color);
        return true;
    }
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    case ExternalDataType::String:
    default:
        value = ExternalDataValue::string(text.toStdString(), type);
        return true;
    }
}

static QString value_text(const ExternalDataValue &value)
{
    return QString::fromStdString(external_data_value_to_string(value));
}

static bool layer_supports_external_binding(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock ||
           layer.type == LayerType::Ticker || layer.type == LayerType::Image;
}

static QString layer_type_label(LayerType type)
{
    switch (type) {
    case LayerType::Text: return QStringLiteral("Text");
    case LayerType::Clock: return QStringLiteral("Clock");
    case LayerType::Ticker: return QStringLiteral("Ticker");
    case LayerType::Image: return QStringLiteral("Image");
    default: return QStringLiteral("Layer");
    }
}

static QString canonical_property_for_layer(const Layer &layer)
{
    return layer.type == LayerType::Image
        ? QString::fromLatin1(ExternalPropertyPaths::ImagePath)
        : QString::fromLatin1(ExternalPropertyPaths::TextContent);
}

static QString formatter_summary(const ExternalDataFormatterConfig &formatter,
                                 const std::string &legacy = {})
{
    QStringList parts;
    if (!formatter.prefix.empty()) parts << QStringLiteral("Prefix");
    if (!formatter.suffix.empty()) parts << QStringLiteral("Suffix");
    if (formatter.number_format_enabled) {
        QString number = formatter.decimal_places >= 0
            ? QStringLiteral("Number (%1 dp)").arg(formatter.decimal_places)
            : QStringLiteral("Number");
        if (formatter.thousands_separator) number += QStringLiteral(", grouping");
        parts << number;
    }
    switch (formatter.text_case) {
    case ExternalDataTextCase::Uppercase: parts << QStringLiteral("UPPERCASE"); break;
    case ExternalDataTextCase::Lowercase: parts << QStringLiteral("lowercase"); break;
    case ExternalDataTextCase::TitleCase: parts << QStringLiteral("Title Case"); break;
    case ExternalDataTextCase::None: default: break;
    }
    if (!formatter.date_time_format.empty()) parts << QStringLiteral("Date/time");
    if (!formatter.conditional_replacements.empty())
        parts << QStringLiteral("%1 replacement(s)").arg(formatter.conditional_replacements.size());
    if (formatter.empty_value_mode == ExternalDataEmptyValueMode::UseFallback)
        parts << QStringLiteral("Empty→fallback");
    else if (formatter.empty_value_mode == ExternalDataEmptyValueMode::Replacement)
        parts << QStringLiteral("Empty→replacement");
    if (!legacy.empty()) parts << QStringLiteral("Legacy: %1").arg(QString::fromStdString(legacy));
    return parts.isEmpty() ? QStringLiteral("None") : parts.join(QStringLiteral(" · "));
}

static QSpinBox *make_millisecond_spin(QWidget *parent, int maximum = 86400000)
{
    auto *spin = new QSpinBox(parent);
    spin->setRange(0, maximum);
    spin->setSuffix(QStringLiteral(" ms"));
    spin->setSpecialValueText(QStringLiteral("Off"));
    return spin;
}

} // namespace

ExternalDataSettingsDialog::ExternalDataSettingsDialog(std::shared_ptr<Title> title,
                                                       QWidget *parent)
    : QDialog(parent), title_(std::move(title))
{
    if (title_) {
        sources_ = title_->external_data_sources;
        for (const auto &source : sources_)
            original_source_ids_.push_back(source.id);
        for (const auto &layer : title_->layers) {
            if (!layer)
                continue;
            for (const auto &binding : layer->external_bindings)
                pending_bindings_.push_back({layer->id, binding});
        }
    }
    fxm_apply_brand_icon(this);
    setWindowTitle(QStringLiteral("External Data Providers"));
    resize(980, 700);
    build_ui();
    populate_source_list();
    if (!sources_.empty())
        source_list_->setCurrentRow(0);
    else
        clear_form();

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "SettingsUI", [this]() {
            std::ostringstream stream;
            stream << "opened title="
                   << ExternalDataLog::sanitize(title_ ? title_->id : std::string{}, 160)
                   << " sources=" << sources_.size()
                   << " pendingBindings=" << pending_bindings_.size();
            return stream.str();
        });
    status_timer_ = new QTimer(this);
    status_timer_->setInterval(400);
    connect(status_timer_, &QTimer::timeout, this,
            [this]() { update_runtime_status(); });
    status_timer_->start();
    update_runtime_status();
}

ExternalDataSettingsDialog::~ExternalDataSettingsDialog()
{
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "SettingsUI", [this]() {
            return std::string("closed title=") +
                   ExternalDataLog::sanitize(title_ ? title_->id : std::string{}, 160);
        });
    /* Runtime preview actions in this dialog may temporarily configure a
     * provider before Save. Restore the authoritative title-store set on both
     * Save and Cancel so no orphan provider survives the dialog. */
    std::vector<ExternalDataSourceDefinition> definitions;
    for (const auto &candidate : TitleDataStore::instance().title_snapshots()) {
        if (!candidate)
            continue;
        definitions.insert(definitions.end(),
                           candidate->external_data_sources.begin(),
                           candidate->external_data_sources.end());
    }
    ExternalDataProviderService::instance().synchronize(definitions);
}

void ExternalDataSettingsDialog::build_ui()
{
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(
        QStringLiteral("Configure provider-backed fields. File and network work runs on the "
                       "external-data worker thread and never on the OBS render thread."), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *splitter = new QSplitter(this);
    auto *left = new QWidget(splitter);
    auto *left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 0, 0);
    source_list_ = new QListWidget(left);
    left_layout->addWidget(source_list_, 1);
    auto *source_buttons = new QHBoxLayout();
    add_source_button_ = new QPushButton(QStringLiteral("Add"), left);
    duplicate_source_button_ = new QPushButton(QStringLiteral("Duplicate"), left);
    remove_source_button_ = new QPushButton(QStringLiteral("Remove"), left);
    source_buttons->addWidget(add_source_button_);
    source_buttons->addWidget(duplicate_source_button_);
    source_buttons->addWidget(remove_source_button_);
    left_layout->addLayout(source_buttons);
    splitter->addWidget(left);

    auto *tabs = new QTabWidget(splitter);

    auto *provider_tab = new QWidget(tabs);
    auto *provider_layout = new QVBoxLayout(provider_tab);
    auto *provider_scroll = new QScrollArea(provider_tab);
    provider_scroll->setWidgetResizable(true);
    auto *provider_form_widget = new QWidget(provider_scroll);
    auto *form = new QFormLayout(provider_form_widget);

    name_edit_ = new QLineEdit(provider_form_widget);
    id_edit_ = new QLineEdit(provider_form_widget);
    id_edit_->setReadOnly(true);
    provider_combo_ = new QComboBox(provider_form_widget);
    for (int value = static_cast<int>(ExternalDataProviderType::None);
         value <= static_cast<int>(ExternalDataProviderType::ManualTable); ++value) {
        const auto type = static_cast<ExternalDataProviderType>(value);
        provider_combo_->addItem(provider_label(type), value);
    }
    enabled_check_ = new FxmSwitch(QStringLiteral("Connect automatically"), provider_form_widget);

    auto *location_row = new QWidget(provider_form_widget);
    auto *location_layout = new QHBoxLayout(location_row);
    location_layout->setContentsMargins(0, 0, 0, 0);
    location_edit_ = new QLineEdit(location_row);
    browse_button_ = new QPushButton(QStringLiteral("Browse…"), location_row);
    location_layout->addWidget(location_edit_, 1);
    location_layout->addWidget(browse_button_);

    refresh_mode_combo_ = new QComboBox(provider_form_widget);
    refresh_mode_combo_->addItem(QStringLiteral("Refresh on cue"),
                                 static_cast<int>(ExternalDataRefreshMode::RefreshOnCue));
    refresh_mode_combo_->addItem(QStringLiteral("Refresh continuously"),
                                 static_cast<int>(ExternalDataRefreshMode::RefreshContinuously));
    refresh_mode_combo_->addItem(QStringLiteral("Refresh manually"),
                                 static_cast<int>(ExternalDataRefreshMode::RefreshManually));
    polling_spin_ = make_millisecond_spin(provider_form_widget);
    polling_spin_->setMaximum(86400000);
    rate_limit_spin_ = make_millisecond_spin(provider_form_widget, 60000);
    keep_last_check_ = new FxmSwitch(QStringLiteral("Keep the last valid values during errors/disconnects"),
                                     provider_form_widget);
    stale_spin_ = make_millisecond_spin(provider_form_widget, 604800000);
    root_path_edit_ = new QLineEdit(provider_form_widget);
    root_path_edit_->setPlaceholderText(QStringLiteral("Optional JSON root, e.g. data.items[0]"));
    headers_edit_ = new QPlainTextEdit(provider_form_widget);
    headers_edit_->setPlaceholderText(QStringLiteral("Header-Name=value\nAnother-Header=value"));
    headers_edit_->setMaximumHeight(90);
    token_edit_ = new QLineEdit(provider_form_widget);
    token_edit_->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    timeout_spin_ = make_millisecond_spin(provider_form_widget, 300000);
    retry_count_spin_ = new QSpinBox(provider_form_widget);
    retry_count_spin_->setRange(0, 20);
    retry_backoff_spin_ = make_millisecond_spin(provider_form_widget, 300000);
    reconnect_initial_spin_ = make_millisecond_spin(provider_form_widget, 300000);
    reconnect_max_spin_ = make_millisecond_spin(provider_form_widget, 3600000);
    csv_headers_check_ = new FxmSwitch(QStringLiteral("First row contains column headers"),
                                       provider_form_widget);
    csv_row_spin_ = new QSpinBox(provider_form_widget);
    csv_row_spin_->setRange(0, 1000000);
    csv_mapping_edit_ = new QPlainTextEdit(provider_form_widget);
    csv_mapping_edit_->setPlaceholderText(QStringLiteral("field.path=Column Name\nother.field=2"));
    csv_mapping_edit_->setMaximumHeight(90);
    text_field_edit_ = new QLineEdit(provider_form_widget);

    form->addRow(QStringLiteral("Name"), name_edit_);
    form->addRow(QStringLiteral("Source ID"), id_edit_);
    form->addRow(QStringLiteral("Provider"), provider_combo_);
    form->addRow(QString(), enabled_check_);
    form->addRow(QStringLiteral("Path / URL"), location_row);
    form->addRow(QStringLiteral("Refresh behavior"), refresh_mode_combo_);
    form->addRow(QStringLiteral("Polling interval"), polling_spin_);
    form->addRow(QStringLiteral("Update rate limit"), rate_limit_spin_);
    form->addRow(QString(), keep_last_check_);
    form->addRow(QStringLiteral("Mark stale after"), stale_spin_);
    form->addRow(QStringLiteral("JSON root path"), root_path_edit_);
    form->addRow(QStringLiteral("Request headers"), headers_edit_);
    form->addRow(QStringLiteral("Authentication token"), token_edit_);
    form->addRow(QStringLiteral("Network timeout"), timeout_spin_);
    form->addRow(QStringLiteral("Retry count"), retry_count_spin_);
    form->addRow(QStringLiteral("Retry backoff"), retry_backoff_spin_);
    form->addRow(QStringLiteral("Reconnect initial delay"), reconnect_initial_spin_);
    form->addRow(QStringLiteral("Reconnect maximum delay"), reconnect_max_spin_);
    form->addRow(QString(), csv_headers_check_);
    form->addRow(QStringLiteral("CSV row"), csv_row_spin_);
    form->addRow(QStringLiteral("CSV column mapping"), csv_mapping_edit_);
    form->addRow(QStringLiteral("Text field path"), text_field_edit_);
    provider_scroll->setWidget(provider_form_widget);
    provider_layout->addWidget(provider_scroll);
    tabs->addTab(provider_tab, QStringLiteral("Provider"));

    auto *fields_tab = new QWidget(tabs);
    auto *fields_layout = new QVBoxLayout(fields_tab);
    auto *fields_help = new QLabel(
        QStringLiteral("Fields are discovered automatically and can be bound immediately. "
                       "Use this optional table only to pin a field for offline use, rename it, "
                       "override its type/default mapping, or create Manual/internal values. "
                       "A field used by a binding is pinned automatically."), fields_tab);
    fields_help->setWordWrap(true);
    fields_layout->addWidget(fields_help);
    fields_table_ = new QTableWidget(0, 6, fields_tab);
    fields_table_->setHorizontalHeaderLabels({QStringLiteral("Pin / override"),
                                               QStringLiteral("Field path"),
                                               QStringLiteral("Display name"),
                                               QStringLiteral("Type"),
                                               QStringLiteral("Manual value"),
                                               QStringLiteral("Live value")});
    fields_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fields_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    fields_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    fields_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    fields_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    fields_layout->addWidget(fields_table_, 1);
    auto *field_buttons = new QHBoxLayout();
    add_field_button_ = new QPushButton(QStringLiteral("Add custom field"), fields_tab);
    remove_field_button_ = new QPushButton(QStringLiteral("Remove override"), fields_tab);
    field_buttons->addWidget(add_field_button_);
    field_buttons->addWidget(remove_field_button_);
    field_buttons->addStretch();
    fields_layout->addLayout(field_buttons);
    tabs->addTab(fields_tab, QStringLiteral("Fields (optional)"));

    auto *bindings_tab = new QWidget(tabs);
    auto *bindings_layout = new QVBoxLayout(bindings_tab);
    auto *bindings_help = new QLabel(
        QStringLiteral("Bind provider fields to live layer properties. The authored value is "
                       "never overwritten and remains the final fallback."), bindings_tab);
    bindings_help->setWordWrap(true);
    bindings_layout->addWidget(bindings_help);
    bindings_table_ = new QTableWidget(0, 7, bindings_tab);
    bindings_table_->setHorizontalHeaderLabels({QStringLiteral("Layer"),
                                                 QStringLiteral("Property"),
                                                 QStringLiteral("Field path"),
                                                 QStringLiteral("Formatter"),
                                                 QStringLiteral("Fallback"),
                                                 QStringLiteral("Live preview"),
                                                 QStringLiteral("Edit")});
    bindings_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    bindings_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    bindings_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    bindings_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    bindings_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    bindings_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    bindings_table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    bindings_layout->addWidget(bindings_table_, 1);
    auto *binding_buttons = new QHBoxLayout();
    add_binding_button_ = new QPushButton(QStringLiteral("Add binding"), bindings_tab);
    remove_binding_button_ = new QPushButton(QStringLiteral("Remove selected"), bindings_tab);
    binding_buttons->addWidget(add_binding_button_);
    binding_buttons->addWidget(remove_binding_button_);
    binding_buttons->addStretch();
    bindings_layout->addLayout(binding_buttons);
    tabs->addTab(bindings_tab, QStringLiteral("Bindings"));

    auto *status_tab = new QWidget(tabs);
    auto *status_layout = new QVBoxLayout(status_tab);
    auto *status_group = new QGroupBox(QStringLiteral("Runtime state"), status_tab);
    auto *status_form = new QFormLayout(status_group);
    state_value_ = new QLabel(QStringLiteral("Disconnected"), status_group);
    last_update_value_ = new QLabel(QStringLiteral("—"), status_group);
    error_value_ = new QLabel(QStringLiteral("—"), status_group);
    error_value_->setWordWrap(true);
    error_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_form->addRow(QStringLiteral("State"), state_value_);
    status_form->addRow(QStringLiteral("Last update"), last_update_value_);
    status_form->addRow(QStringLiteral("Error"), error_value_);
    status_layout->addWidget(status_group);

    auto *preview_group = new QGroupBox(QStringLiteral("Live field preview"), status_tab);
    auto *preview_layout = new QVBoxLayout(preview_group);
    live_preview_table_ = new QTableWidget(0, 6, preview_group);
    live_preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    live_preview_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    live_preview_table_->setHorizontalHeaderLabels({QStringLiteral("Field"),
                                                     QStringLiteral("Type"),
                                                     QStringLiteral("Current value"),
                                                     QStringLiteral("Last update"),
                                                     QStringLiteral("State"),
                                                     QStringLiteral("Schema")});
    live_preview_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    live_preview_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    live_preview_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    live_preview_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    live_preview_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    live_preview_table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    preview_layout->addWidget(live_preview_table_);
    status_layout->addWidget(preview_group, 1);
    auto *runtime_buttons = new QHBoxLayout();
    test_connection_button_ = new QPushButton(QStringLiteral("Test connection"), status_tab);
    connect_button_ = new QPushButton(QStringLiteral("Connect"), status_tab);
    disconnect_button_ = new QPushButton(QStringLiteral("Disconnect"), status_tab);
    refresh_button_ = new QPushButton(QStringLiteral("Refresh now"), status_tab);
    runtime_buttons->addWidget(test_connection_button_);
    runtime_buttons->addWidget(connect_button_);
    runtime_buttons->addWidget(disconnect_button_);
    runtime_buttons->addWidget(refresh_button_);
    runtime_buttons->addStretch();
    status_layout->addLayout(runtime_buttons);
    status_layout->addStretch();
    tabs->addTab(status_tab, QStringLiteral("Status"));

    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({230, 750});
    outer->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);

    connect(source_list_, &QListWidget::currentRowChanged, this,
            [this](int row) { select_source(row); });
    connect(add_source_button_, &QPushButton::clicked, this, [this]() {
        if (current_row_ >= 0 && !save_form_to_source(current_row_, true))
            return;
        ExternalDataSourceDefinition source;
        source.id = TitleDataStore::make_uuid();
        source.name = QStringLiteral("Data Source %1").arg(sources_.size() + 1).toStdString();
        source.provider.type = ExternalDataProviderType::ManualTable;
        const std::string created_id = source.id;
        sources_.push_back(std::move(source));
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("added source id=") +
                       ExternalDataLog::sanitize(created_id, 160) +
                       " provider=ManualTable";
            });
        populate_source_list();
        source_list_->setCurrentRow(static_cast<int>(sources_.size()) - 1);
    });
    connect(duplicate_source_button_, &QPushButton::clicked, this, [this]() {
        const int row = source_list_->currentRow();
        if (row < 0 || row >= static_cast<int>(sources_.size()))
            return;
        if (!save_form_to_source(row, true))
            return;
        ExternalDataSourceDefinition copy = sources_[static_cast<size_t>(row)];
        const std::string old_id = copy.id;
        copy.id = TitleDataStore::make_uuid();
        copy.name = (copy.name.empty() ? std::string("Data Source") : copy.name) + " Copy";
        sources_.insert(sources_.begin() + row + 1, copy);
        std::vector<PendingBinding> duplicated;
        for (const auto &pending : pending_bindings_) {
            if (pending.binding.source_id != old_id)
                continue;
            PendingBinding clone = pending;
            clone.binding.source_id = copy.id;
            duplicated.push_back(std::move(clone));
        }
        pending_bindings_.insert(pending_bindings_.end(), duplicated.begin(), duplicated.end());
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                std::ostringstream stream;
                stream << "duplicated source old=" << ExternalDataLog::sanitize(old_id, 160)
                       << " new=" << ExternalDataLog::sanitize(copy.id, 160)
                       << " copiedBindings=" << duplicated.size();
                return stream.str();
            });
        current_row_ = -1;
        populate_source_list();
        source_list_->setCurrentRow(row + 1);
    });
    connect(remove_source_button_, &QPushButton::clicked, this, [this]() {
        const int row = source_list_->currentRow();
        if (row < 0 || row >= static_cast<int>(sources_.size()))
            return;
        const std::string removed_id = sources_[static_cast<size_t>(row)].id;
        ExternalDataProviderService::instance().disconnect_source(removed_id);
        pending_bindings_.erase(
            std::remove_if(pending_bindings_.begin(), pending_bindings_.end(),
                           [&removed_id](const PendingBinding &pending) {
                               return pending.binding.source_id == removed_id;
                           }),
            pending_bindings_.end());
        sources_.erase(sources_.begin() + row);
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("removed source id=") +
                       ExternalDataLog::sanitize(removed_id, 160);
            });
        current_row_ = -1;
        populate_source_list();
        if (!sources_.empty())
            source_list_->setCurrentRow(std::min(row, static_cast<int>(sources_.size()) - 1));
        else
            clear_form();
    });
    connect(provider_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { update_control_visibility(); });
    connect(refresh_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { update_control_visibility(); });
    connect(browse_button_, &QPushButton::clicked, this, [this]() {
        const auto type = static_cast<ExternalDataProviderType>(provider_combo_->currentData().toInt());
        QString filter;
        if (type == ExternalDataProviderType::JsonFile)
            filter = QStringLiteral("JSON files (*.json);;All files (*.*)");
        else if (type == ExternalDataProviderType::CsvFile)
            filter = QStringLiteral("CSV files (*.csv);;All files (*.*)");
        else
            filter = QStringLiteral("Text files (*.txt *.log *.md);;All files (*.*)");
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select data file"),
                                                          location_edit_->text(), filter);
        if (!path.isEmpty())
            location_edit_->setText(path);
    });
    connect(add_field_button_, &QPushButton::clicked, this,
            [this]() { add_field_row(); });
    connect(remove_field_button_, &QPushButton::clicked, this, [this]() {
        std::set<int, std::greater<int>> rows;
        for (const QModelIndex &index : fields_table_->selectionModel()->selectedRows())
            rows.insert(index.row());
        for (int row : rows)
            fields_table_->removeRow(row);
    });
    connect(add_binding_button_, &QPushButton::clicked, this,
            [this]() { add_binding_row(); });
    connect(remove_binding_button_, &QPushButton::clicked, this, [this]() {
        std::set<int, std::greater<int>> rows;
        for (const QModelIndex &index : bindings_table_->selectionModel()->selectedRows())
            rows.insert(index.row());
        for (int row : rows) {
            bindings_table_->removeRow(row);
            if (row >= 0 && row < static_cast<int>(binding_formatter_configs_.size()))
                binding_formatter_configs_.erase(binding_formatter_configs_.begin() + row);
            if (row >= 0 && row < static_cast<int>(binding_legacy_formatters_.size()))
                binding_legacy_formatters_.erase(binding_legacy_formatters_.begin() + row);
        }
    });
    connect(test_connection_button_, &QPushButton::clicked, this, [this]() {
        if (current_row_ < 0 || !save_form_to_source(current_row_, true))
            return;
        const auto &source = sources_[static_cast<size_t>(current_row_)];
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("test connection source=") +
                       ExternalDataLog::sanitize(source.id, 160) +
                       " provider=" + ExternalDataLog::provider_type_name(source.provider.type) +
                       " location=" + ExternalDataLog::safe_location(source.provider.location);
            });
        ExternalDataProviderService::instance().configure_source(source);
        ExternalDataProviderService::instance().connect_source(source.id);
        ExternalDataProviderService::instance().refresh_source(source.id);
        state_value_->setText(QStringLiteral("Testing…"));
        test_connection_button_->setEnabled(false);
        QTimer::singleShot(1200, this, [this]() {
            update_runtime_status();
            if (test_connection_button_)
                test_connection_button_->setEnabled(current_row_ >= 0);
        });
    });
    connect(connect_button_, &QPushButton::clicked, this, [this]() {
        if (current_row_ < 0 || !save_form_to_source(current_row_, true))
            return;
        const auto &source = sources_[static_cast<size_t>(current_row_)];
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("connect clicked source=") +
                       ExternalDataLog::sanitize(source.id, 160);
            });
        ExternalDataProviderService::instance().configure_source(source);
        ExternalDataProviderService::instance().connect_source(source.id);
    });
    connect(disconnect_button_, &QPushButton::clicked, this, [this]() {
        if (current_row_ < 0)
            return;
        const std::string source_id = sources_[static_cast<size_t>(current_row_)].id;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("disconnect clicked source=") +
                       ExternalDataLog::sanitize(source_id, 160);
            });
        ExternalDataProviderService::instance().disconnect_source(source_id);
    });
    connect(refresh_button_, &QPushButton::clicked, this, [this]() {
        if (current_row_ < 0 || !save_form_to_source(current_row_, true))
            return;
        const auto &source = sources_[static_cast<size_t>(current_row_)];
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "SettingsUI", [&]() {
                return std::string("refresh clicked source=") +
                       ExternalDataLog::sanitize(source.id, 160);
            });
        ExternalDataProviderService::instance().configure_source(source);
        ExternalDataProviderService::instance().connect_source(source.id);
        ExternalDataProviderService::instance().refresh_source(source.id);
    });
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this]() { commit_and_accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExternalDataSettingsDialog::populate_source_list()
{
    const int previous = source_list_->currentRow();
    source_list_->clear();
    for (const auto &source : sources_) {
        const QString name = source.name.empty() ? QString::fromStdString(source.id)
                                                 : QString::fromStdString(source.name);
        auto *item = new QListWidgetItem(
            name + QStringLiteral("\n") + provider_label(source.provider.type), source_list_);
        item->setData(Qt::UserRole, QString::fromStdString(source.id));
    }
    if (previous >= 0 && previous < source_list_->count())
        source_list_->setCurrentRow(previous);
}

void ExternalDataSettingsDialog::select_source(int row)
{
    if (loading_)
        return;
    if (current_row_ >= 0 && current_row_ < static_cast<int>(sources_.size()) &&
        current_row_ != row) {
        if (!save_form_to_source(current_row_, true)) {
            loading_ = true;
            source_list_->setCurrentRow(current_row_);
            loading_ = false;
            return;
        }
    }
    current_row_ = row;
    load_source_to_form(row);
}

void ExternalDataSettingsDialog::load_source_to_form(int row)
{
    loading_ = true;
    if (row < 0 || row >= static_cast<int>(sources_.size())) {
        clear_form();
        loading_ = false;
        return;
    }
    const auto &source = sources_[static_cast<size_t>(row)];
    name_edit_->setText(QString::fromStdString(source.name));
    id_edit_->setText(QString::fromStdString(source.id));
    provider_combo_->setCurrentIndex(provider_combo_->findData(static_cast<int>(source.provider.type)));
    enabled_check_->setChecked(source.provider.enabled);
    location_edit_->setText(QString::fromStdString(source.provider.location));
    refresh_mode_combo_->setCurrentIndex(refresh_mode_combo_->findData(
        static_cast<int>(source.provider.refresh_mode)));
    polling_spin_->setValue(source.provider.polling_interval_ms);
    rate_limit_spin_->setValue(source.provider.rate_limit_ms);
    keep_last_check_->setChecked(source.provider.keep_last_value);
    stale_spin_->setValue(source.provider.stale_after_ms);
    root_path_edit_->setText(QString::fromStdString(source.provider.root_path));
    headers_edit_->setPlainText(key_value_lines(source.provider.headers));
    token_edit_->setText(QString::fromStdString(source.provider.authentication_token));
    timeout_spin_->setValue(source.provider.timeout_ms);
    retry_count_spin_->setValue(source.provider.retry_count);
    retry_backoff_spin_->setValue(source.provider.retry_backoff_ms);
    reconnect_initial_spin_->setValue(source.provider.reconnect_initial_ms);
    reconnect_max_spin_->setValue(source.provider.reconnect_max_ms);
    csv_headers_check_->setChecked(source.provider.csv_first_row_headers);
    csv_row_spin_->setValue(source.provider.csv_row_index);
    csv_mapping_edit_->setPlainText(key_value_lines(source.provider.csv_column_mapping));
    text_field_edit_->setText(QString::fromStdString(source.provider.text_field_path));

    fields_table_->setRowCount(0);
    const ExternalDataSourceState runtime =
        ExternalDataManager::instance().source_state(source.id);
    for (const auto &field : source.fields) {
        const auto manual = source.provider.manual_values.find(field.path);
        const auto live = runtime.fields.find(field.path);
        add_field_row(&field,
                      manual == source.provider.manual_values.end() ? nullptr : &manual->second,
                      true, false,
                      live != runtime.fields.end() && live->second.has_current_value
                          ? &live->second.current_value : nullptr);
    }
    for (const auto &entry : runtime.fields) {
        const bool pinned = std::any_of(
            source.fields.begin(), source.fields.end(),
            [&entry](const ExternalDataFieldDefinition &field) {
                return field.path == entry.first;
            });
        if (!pinned) {
            add_field_row(&entry.second.definition, nullptr, false, true,
                          entry.second.has_current_value
                              ? &entry.second.current_value : nullptr);
        }
    }
    for (const auto &manual : source.provider.manual_values) {
        const bool has_field = std::any_of(source.fields.begin(), source.fields.end(),
                                           [&manual](const ExternalDataFieldDefinition &field) {
                                               return field.path == manual.first;
                                           });
        if (!has_field) {
            ExternalDataFieldDefinition field;
            field.path = manual.first;
            field.name = manual.first;
            field.type = manual.second.type;
            add_field_row(&field, &manual.second, true, false, &manual.second);
        }
    }
    load_bindings_for_source(source.id);
    remove_source_button_->setEnabled(true);
    loading_ = false;
    update_control_visibility();
    update_runtime_status();
}

bool ExternalDataSettingsDialog::save_form_to_source(int row, bool report_errors)
{
    if (loading_ || row < 0 || row >= static_cast<int>(sources_.size()))
        return true;
    auto &source = sources_[static_cast<size_t>(row)];
    source.name = name_edit_->text().trimmed().toStdString();
    source.provider.type = static_cast<ExternalDataProviderType>(provider_combo_->currentData().toInt());
    source.provider.enabled = enabled_check_->isChecked();
    source.provider.location = location_edit_->text().trimmed().toStdString();
    source.provider.refresh_mode = static_cast<ExternalDataRefreshMode>(
        refresh_mode_combo_->currentData().toInt());
    source.provider.polling_interval_ms = polling_spin_->value();
    source.provider.rate_limit_ms = rate_limit_spin_->value();
    source.provider.keep_last_value = keep_last_check_->isChecked();
    source.provider.stale_after_ms = stale_spin_->value();
    source.provider.root_path = root_path_edit_->text().trimmed().toStdString();
    source.provider.headers = parse_key_value_lines(headers_edit_->toPlainText());
    source.provider.authentication_token = token_edit_->text().toStdString();
    source.provider.timeout_ms = std::max(250, timeout_spin_->value());
    source.provider.retry_count = retry_count_spin_->value();
    source.provider.retry_backoff_ms = std::max(50, retry_backoff_spin_->value());
    source.provider.reconnect_initial_ms = std::max(100, reconnect_initial_spin_->value());
    source.provider.reconnect_max_ms = std::max(source.provider.reconnect_initial_ms,
                                                reconnect_max_spin_->value());
    source.provider.csv_first_row_headers = csv_headers_check_->isChecked();
    source.provider.csv_row_index = csv_row_spin_->value();
    source.provider.csv_column_mapping = parse_key_value_lines(csv_mapping_edit_->toPlainText());
    source.provider.text_field_path = text_field_edit_->text().trimmed().toStdString();
    if (source.provider.text_field_path.empty())
        source.provider.text_field_path = "text";

    source.fields.clear();
    source.provider.manual_values.clear();
    std::set<std::string> seen_paths;
    for (int table_row = 0; table_row < fields_table_->rowCount(); ++table_row) {
        const auto *pin_item = fields_table_->item(table_row, 0);
        const auto *path_item = fields_table_->item(table_row, 1);
        const QString path_text = path_item ? path_item->text().trimmed() : QString();
        if (path_text.isEmpty())
            continue;
        const auto *value_item = fields_table_->item(table_row, 4);
        const bool has_manual_value = value_item && !value_item->text().isEmpty();
        const bool should_pin =
            (pin_item && pin_item->checkState() == Qt::Checked) ||
            has_manual_value ||
            source.provider.type == ExternalDataProviderType::ManualTable;
        if (!should_pin)
            continue;

        const std::string path = path_text.toStdString();
        if (!seen_paths.insert(path).second) {
            if (report_errors)
                QMessageBox::warning(this, QStringLiteral("External Data"),
                    QStringLiteral("Duplicate pinned field path: %1").arg(path_text));
            return false;
        }
        ExternalDataFieldDefinition field;
        field.path = path;
        const auto *name_item = fields_table_->item(table_row, 2);
        field.name = name_item && !name_item->text().trimmed().isEmpty()
            ? name_item->text().trimmed().toStdString() : path;
        auto *type_combo = qobject_cast<QComboBox *>(fields_table_->cellWidget(table_row, 3));
        field.type = type_combo
            ? static_cast<ExternalDataType>(type_combo->currentData().toInt())
            : ExternalDataType::String;
        source.fields.push_back(field);

        if (has_manual_value) {
            ExternalDataValue value;
            if (!parse_value(value_item->text(), field.type, value)) {
                if (report_errors)
                    QMessageBox::warning(this, QStringLiteral("External Data"),
                        QStringLiteral("Manual value for '%1' is not valid for type %2.")
                            .arg(path_text, type_label(field.type)));
                return false;
            }
            source.provider.manual_values[path] = std::move(value);
        }
    }

    if (!save_bindings_for_source(source.id, report_errors))
        return false;

    if (source.name.empty())
        source.name = source.id;
    if (auto *item = source_list_->item(row))
        item->setText(QString::fromStdString(source.name) + QStringLiteral("\n") +
                      provider_label(source.provider.type));
    return true;
}

void ExternalDataSettingsDialog::clear_form()
{
    const QList<QWidget *> widgets = {
        name_edit_, id_edit_, provider_combo_, enabled_check_, location_edit_, browse_button_,
        refresh_mode_combo_, polling_spin_, rate_limit_spin_, keep_last_check_, stale_spin_, root_path_edit_,
        headers_edit_, token_edit_, timeout_spin_, retry_count_spin_, retry_backoff_spin_,
        reconnect_initial_spin_, reconnect_max_spin_, csv_headers_check_, csv_row_spin_,
        csv_mapping_edit_, text_field_edit_, fields_table_, add_field_button_,
        remove_field_button_, bindings_table_, add_binding_button_,
        remove_binding_button_, live_preview_table_, test_connection_button_,
        connect_button_, disconnect_button_, refresh_button_};
    for (QWidget *widget : widgets) {
        if (widget)
            widget->setEnabled(false);
    }
    name_edit_->clear();
    id_edit_->clear();
    location_edit_->clear();
    fields_table_->setRowCount(0);
    bindings_table_->setRowCount(0);
    binding_formatter_configs_.clear();
    binding_legacy_formatters_.clear();
    if (live_preview_table_) live_preview_table_->setRowCount(0);
    state_value_->setText(QStringLiteral("Disconnected"));
    last_update_value_->setText(QStringLiteral("—"));
    error_value_->setText(QStringLiteral("—"));
    remove_source_button_->setEnabled(false);
}

void ExternalDataSettingsDialog::update_control_visibility()
{
    const bool has_source = current_row_ >= 0 && current_row_ < static_cast<int>(sources_.size());
    if (!has_source)
        return;
    const QList<QWidget *> widgets = {
        name_edit_, id_edit_, provider_combo_, enabled_check_, location_edit_, browse_button_,
        refresh_mode_combo_, polling_spin_, rate_limit_spin_, keep_last_check_, stale_spin_, root_path_edit_,
        headers_edit_, token_edit_, timeout_spin_, retry_count_spin_, retry_backoff_spin_,
        reconnect_initial_spin_, reconnect_max_spin_, csv_headers_check_, csv_row_spin_,
        csv_mapping_edit_, text_field_edit_, fields_table_, add_field_button_,
        remove_field_button_, bindings_table_, add_binding_button_,
        remove_binding_button_, live_preview_table_, test_connection_button_,
        connect_button_, disconnect_button_, refresh_button_};
    for (QWidget *widget : widgets)
        widget->setEnabled(true);

    const auto type = static_cast<ExternalDataProviderType>(provider_combo_->currentData().toInt());
    const bool is_file = type == ExternalDataProviderType::JsonFile ||
                         type == ExternalDataProviderType::CsvFile ||
                         type == ExternalDataProviderType::LocalTextFile;
    const bool is_network = type == ExternalDataProviderType::HttpJson ||
                            type == ExternalDataProviderType::WebSocket;
    const bool is_json = type == ExternalDataProviderType::JsonFile ||
                         type == ExternalDataProviderType::HttpJson ||
                         type == ExternalDataProviderType::WebSocket;
    const bool is_csv = type == ExternalDataProviderType::CsvFile;
    const bool is_websocket = type == ExternalDataProviderType::WebSocket;
    const bool is_text = type == ExternalDataProviderType::LocalTextFile;
    const bool has_location = is_file || is_network;

    location_edit_->setEnabled(has_location);
    browse_button_->setEnabled(is_file);
    refresh_mode_combo_->setEnabled(type != ExternalDataProviderType::None);
    polling_spin_->setEnabled(
        type != ExternalDataProviderType::None &&
        type != ExternalDataProviderType::ManualTable &&
        static_cast<ExternalDataRefreshMode>(refresh_mode_combo_->currentData().toInt()) ==
            ExternalDataRefreshMode::RefreshContinuously);
    root_path_edit_->setEnabled(is_json);
    headers_edit_->setEnabled(is_network);
    token_edit_->setEnabled(is_network);
    timeout_spin_->setEnabled(type == ExternalDataProviderType::HttpJson);
    retry_count_spin_->setEnabled(type == ExternalDataProviderType::HttpJson);
    retry_backoff_spin_->setEnabled(type == ExternalDataProviderType::HttpJson);
    reconnect_initial_spin_->setEnabled(is_websocket);
    reconnect_max_spin_->setEnabled(is_websocket);
    csv_headers_check_->setEnabled(is_csv);
    csv_row_spin_->setEnabled(is_csv);
    csv_mapping_edit_->setEnabled(is_csv);
    text_field_edit_->setEnabled(is_text);
}

void ExternalDataSettingsDialog::update_runtime_status()
{
    for (int row = 0; row < source_list_->count() && row < static_cast<int>(sources_.size()); ++row) {
        const auto &definition = sources_[static_cast<size_t>(row)];
        const auto runtime = ExternalDataManager::instance().source_state(definition.id);
        const QString name = definition.name.empty() ? QString::fromStdString(definition.id)
                                                      : QString::fromStdString(definition.name);
        const QString state = state_label(runtime.connection_state);
        if (auto *item = source_list_->item(row)) {
            item->setText(name + QStringLiteral("\n") + provider_label(definition.provider.type) +
                          QStringLiteral(" · ") + state);
            item->setToolTip(runtime.error_message.empty()
                ? state : state + QStringLiteral(": ") + QString::fromStdString(runtime.error_message));
        }
    }

    if (current_row_ < 0 || current_row_ >= static_cast<int>(sources_.size()))
        return;
    const auto state = ExternalDataManager::instance().source_state(
        sources_[static_cast<size_t>(current_row_)].id);
    refresh_discovered_fields();
    state_value_->setText(state_label(state.connection_state));
    if (state.last_update_timestamp_ms > 0) {
        last_update_value_->setText(QDateTime::fromMSecsSinceEpoch(
            state.last_update_timestamp_ms).toString(Qt::ISODateWithMs));
    } else {
        last_update_value_->setText(QStringLiteral("—"));
    }
    error_value_->setText(state.error_message.empty()
        ? QStringLiteral("—") : QString::fromStdString(state.error_message));

    if (live_preview_table_) {
        live_preview_table_->setRowCount(0);
        for (const auto &entry : state.fields) {
            const auto &field = entry.second;
            const int row = live_preview_table_->rowCount();
            live_preview_table_->insertRow(row);
            const QString display = field.definition.name.empty()
                ? QString::fromStdString(entry.first)
                : QStringLiteral("%1 — %2").arg(QString::fromStdString(field.definition.name),
                                                 QString::fromStdString(entry.first));
            live_preview_table_->setItem(row, 0, new QTableWidgetItem(display));
            live_preview_table_->setItem(row, 1, new QTableWidgetItem(type_label(field.definition.type)));
            live_preview_table_->setItem(row, 2, new QTableWidgetItem(field.has_current_value
                ? value_text(field.current_value) : QStringLiteral("—")));
            live_preview_table_->setItem(row, 3, new QTableWidgetItem(field.last_update_timestamp_ms > 0
                ? QDateTime::fromMSecsSinceEpoch(field.last_update_timestamp_ms).toString(Qt::ISODateWithMs)
                : QStringLiteral("—")));
            live_preview_table_->setItem(row, 4, new QTableWidgetItem(
                field.has_current_value ? state_label(state.connection_state) : QStringLiteral("No value")));
            const auto &source = sources_[static_cast<size_t>(current_row_)];
            const bool pinned = std::any_of(
                source.fields.begin(), source.fields.end(),
                [&entry](const ExternalDataFieldDefinition &candidate) {
                    return candidate.path == entry.first;
                });
            live_preview_table_->setItem(row, 5, new QTableWidgetItem(
                pinned ? QStringLiteral("Pinned / override")
                       : QStringLiteral("Discovered")));
        }
    }

    for (int row = 0; row < bindings_table_->rowCount(); ++row) {
        auto *field_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 2));
        if (!field_combo) continue;
        QString path = field_combo->currentData().toString();
        if (path.isEmpty()) path = field_combo->currentText().trimmed();
        const auto field = state.fields.find(path.toStdString());
        QString preview = QStringLiteral("—");
        if (field != state.fields.end() && field->second.has_current_value) {
            const ExternalDataFormatterConfig formatter =
                row < static_cast<int>(binding_formatter_configs_.size())
                    ? binding_formatter_configs_[static_cast<size_t>(row)]
                    : ExternalDataFormatterConfig{};
            preview = QString::fromStdString(format_external_data_value(
                field->second.current_value, formatter));
        }
        auto *item = bindings_table_->item(row, 5);
        if (!item) {
            item = new QTableWidgetItem();
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            bindings_table_->setItem(row, 5, item);
        }
        item->setText(preview);
    }
}

void ExternalDataSettingsDialog::add_field_row(
    const ExternalDataFieldDefinition *field,
    const ExternalDataValue *manual_value,
    bool pinned, bool discovered,
    const ExternalDataValue *live_value)
{
    const int row = fields_table_->rowCount();
    fields_table_->insertRow(row);

    auto *pin_item = new QTableWidgetItem(
        discovered ? QStringLiteral("Discovered") : QStringLiteral("Pinned"));
    pin_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    pin_item->setCheckState(pinned ? Qt::Checked : Qt::Unchecked);
    pin_item->setToolTip(discovered
        ? QStringLiteral("Discovered automatically. Check to preserve this field as a schema override.")
        : QStringLiteral("Pinned fields stay available when the provider is offline."));
    fields_table_->setItem(row, 0, pin_item);

    fields_table_->setItem(row, 1, new QTableWidgetItem(
        field ? QString::fromStdString(field->path) : QString()));
    fields_table_->setItem(row, 2, new QTableWidgetItem(
        field ? QString::fromStdString(field->name) : QString()));
    const ExternalDataType type = field ? field->type : ExternalDataType::String;
    fields_table_->setCellWidget(row, 3, make_type_combo(fields_table_, type));
    fields_table_->setItem(row, 4, new QTableWidgetItem(
        manual_value ? value_text(*manual_value) : QString()));
    auto *live_item = new QTableWidgetItem(
        live_value ? value_text(*live_value) : QStringLiteral("—"));
    live_item->setFlags(live_item->flags() & ~Qt::ItemIsEditable);
    fields_table_->setItem(row, 5, live_item);
}

void ExternalDataSettingsDialog::refresh_discovered_fields()
{
    if (loading_ || current_row_ < 0 ||
        current_row_ >= static_cast<int>(sources_.size()) || !fields_table_)
        return;

    const auto &source = sources_[static_cast<size_t>(current_row_)];
    const ExternalDataSourceState runtime =
        ExternalDataManager::instance().source_state(source.id);
    for (const auto &entry : runtime.fields) {
        int existing_row = -1;
        for (int row = 0; row < fields_table_->rowCount(); ++row) {
            const auto *path_item = fields_table_->item(row, 1);
            if (path_item && path_item->text().trimmed().toStdString() == entry.first) {
                existing_row = row;
                break;
            }
        }
        if (existing_row < 0) {
            add_field_row(&entry.second.definition, nullptr, false, true,
                          entry.second.has_current_value
                              ? &entry.second.current_value : nullptr);
            continue;
        }

        if (auto *live_item = fields_table_->item(existing_row, 5))
            live_item->setText(entry.second.has_current_value
                ? value_text(entry.second.current_value) : QStringLiteral("—"));
        const auto *pin_item = fields_table_->item(existing_row, 0);
        const bool pinned = pin_item && pin_item->checkState() == Qt::Checked;
        if (!pinned) {
            auto *type_combo = qobject_cast<QComboBox *>(
                fields_table_->cellWidget(existing_row, 3));
            if (type_combo) {
                const int type_index = type_combo->findData(
                    static_cast<int>(entry.second.definition.type));
                if (type_index >= 0)
                    type_combo->setCurrentIndex(type_index);
            }
            auto *name_item = fields_table_->item(existing_row, 2);
            if (name_item && name_item->text().trimmed().isEmpty())
                name_item->setText(QString::fromStdString(entry.second.definition.name));
        }
    }

    /* Existing binding rows are updated in place as discovery finds new paths,
     * so the user never has to close/reopen the settings dialog. */
    for (int binding_row = 0; binding_row < bindings_table_->rowCount(); ++binding_row) {
        auto *combo = qobject_cast<QComboBox *>(
            bindings_table_->cellWidget(binding_row, 2));
        if (!combo)
            continue;
        for (const auto &entry : runtime.fields) {
            const QString path = QString::fromStdString(entry.first);
            if (combo->findData(path) >= 0)
                continue;
            const QString name = QString::fromStdString(entry.second.definition.name);
            combo->addItem(name.isEmpty() || name == path
                ? path + QStringLiteral("  [discovered]")
                : QStringLiteral("%1 — %2  [discovered]").arg(name, path), path);
        }
    }
}

void ExternalDataSettingsDialog::load_bindings_for_source(const std::string &source_id)
{
    bindings_table_->setRowCount(0);
    binding_formatter_configs_.clear();
    binding_legacy_formatters_.clear();
    for (const auto &pending : pending_bindings_) {
        if (pending.binding.source_id == source_id)
            add_binding_row(pending.layer_id, &pending.binding);
    }
}

void ExternalDataSettingsDialog::add_binding_row(
    const std::string &layer_id,
    const ExternalPropertyBinding *binding)
{
    if (!title_)
        return;

    const int row = bindings_table_->rowCount();
    bindings_table_->insertRow(row);
    binding_formatter_configs_.push_back(binding ? binding->formatter_config
                                                  : ExternalDataFormatterConfig{});
    binding_legacy_formatters_.push_back(binding ? binding->formatter : std::string{});

    auto *layer_combo = new QComboBox(bindings_table_);
    for (const auto &layer : title_->layers) {
        if (!layer || !layer_supports_external_binding(*layer))
            continue;
        const QString name = layer->name.empty()
            ? QString::fromStdString(layer->id)
            : QString::fromStdString(layer->name);
        layer_combo->addItem(
            QStringLiteral("%1 (%2)").arg(name, layer_type_label(layer->type)),
            QString::fromStdString(layer->id));
    }
    const std::string selected_layer_id = !layer_id.empty()
        ? layer_id
        : (binding ? std::string{} : (layer_combo->count() > 0
            ? layer_combo->itemData(0).toString().toStdString() : std::string{}));
    if (!selected_layer_id.empty()) {
        const int index = layer_combo->findData(QString::fromStdString(selected_layer_id));
        if (index >= 0)
            layer_combo->setCurrentIndex(index);
    }
    bindings_table_->setCellWidget(row, 0, layer_combo);

    auto *property_combo = new QComboBox(bindings_table_);
    bindings_table_->setCellWidget(row, 1, property_combo);

    auto *field_combo = new QComboBox(bindings_table_);
    field_combo->setEditable(true);
    field_combo->setInsertPolicy(QComboBox::NoInsert);
    /* Read the visible field table rather than only the last committed source
     * snapshot, so a field can be authored and bound in the same dialog pass. */
    for (int field_row = 0; field_row < fields_table_->rowCount(); ++field_row) {
        const auto *path_item = fields_table_->item(field_row, 1);
        const QString path = path_item ? path_item->text().trimmed() : QString();
        if (path.isEmpty())
            continue;
        const auto *name_item = fields_table_->item(field_row, 2);
        const QString name = name_item ? name_item->text().trimmed() : QString();
        field_combo->addItem(name.isEmpty()
            ? path
            : QStringLiteral("%1 — %2").arg(name, path), path);
    }
    if (binding) {
        const QString field_path = QString::fromStdString(binding->field_path);
        const int field_index = field_combo->findData(field_path);
        if (field_index >= 0)
            field_combo->setCurrentIndex(field_index);
        else
            field_combo->setEditText(field_path);
    }
    bindings_table_->setCellWidget(row, 2, field_combo);

    auto *formatter_item = new QTableWidgetItem(formatter_summary(
        binding_formatter_configs_[static_cast<size_t>(row)],
        binding_legacy_formatters_[static_cast<size_t>(row)]));
    formatter_item->setFlags(formatter_item->flags() & ~Qt::ItemIsEditable);
    bindings_table_->setItem(row, 3, formatter_item);
    bindings_table_->setItem(row, 4, new QTableWidgetItem(
        binding && binding->has_fallback_value
            ? value_text(binding->fallback_value) : QString()));
    auto *preview_item = new QTableWidgetItem(QStringLiteral("—"));
    preview_item->setFlags(preview_item->flags() & ~Qt::ItemIsEditable);
    bindings_table_->setItem(row, 5, preview_item);
    auto *edit_button = new QPushButton(QStringLiteral("Edit…"), bindings_table_);
    edit_button->setToolTip(QStringLiteral("Configure source, field, formatter, fallback and preview"));
    bindings_table_->setCellWidget(row, 6, edit_button);

    update_binding_property_combo(row);
    if (binding) {
        const int property_index = property_combo->findData(
            QString::fromStdString(binding->property_path));
        if (property_index >= 0)
            property_combo->setCurrentIndex(property_index);
    }

    connect(layer_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, layer_combo](int) {
                for (int candidate = 0; candidate < bindings_table_->rowCount(); ++candidate) {
                    if (bindings_table_->cellWidget(candidate, 0) == layer_combo) {
                        update_binding_property_combo(candidate);
                        break;
                    }
                }
            });
    connect(edit_button, &QPushButton::clicked, this, [this, edit_button]() {
        int edit_row = -1;
        for (int candidate = 0; candidate < bindings_table_->rowCount(); ++candidate) {
            if (bindings_table_->cellWidget(candidate, 6) == edit_button) {
                edit_row = candidate;
                break;
            }
        }
        if (edit_row < 0 || !title_) return;
        auto *property_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(edit_row, 1));
        auto *field_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(edit_row, 2));
        const std::string property = property_combo
            ? property_combo->currentData().toString().toStdString() : std::string{};
        ExternalPropertyBinding initial;
        initial.enabled = true;
        initial.property_path = property;
        initial.source_id = sources_[static_cast<size_t>(current_row_)].id;
        if (field_combo) {
            QString field = field_combo->currentData().toString();
            if (field.isEmpty()) field = field_combo->currentText().trimmed();
            initial.field_path = field.toStdString();
        }
        if (edit_row < static_cast<int>(binding_formatter_configs_.size()))
            initial.formatter_config = binding_formatter_configs_[static_cast<size_t>(edit_row)];
        if (edit_row < static_cast<int>(binding_legacy_formatters_.size()))
            initial.formatter = binding_legacy_formatters_[static_cast<size_t>(edit_row)];
        if (const auto *fallback = bindings_table_->item(edit_row, 4); fallback && !fallback->text().isEmpty()) {
            const auto source_it = std::find_if(sources_.begin(), sources_.end(),
                [&initial](const ExternalDataSourceDefinition &source) { return source.id == initial.source_id; });
            ExternalDataType type = ExternalDataType::String;
            if (source_it != sources_.end())
                type = external_data_field_type(*source_it, initial.field_path);
            initial.has_fallback_value = parse_value(fallback->text(), type, initial.fallback_value);
        }
        auto preview_title = std::make_shared<Title>(*title_);
        preview_title->external_data_sources = {
            sources_[static_cast<size_t>(current_row_)]
        };
        ExternalDataBindingDialog dialog(preview_title, property,
                                         ExternalDataValue::string(std::string{}),
                                         &initial, this);
        if (dialog.exec() != QDialog::Accepted) return;
        if (dialog.remove_requested()) {
            bindings_table_->removeRow(edit_row);
            if (edit_row < static_cast<int>(binding_formatter_configs_.size()))
                binding_formatter_configs_.erase(binding_formatter_configs_.begin() + edit_row);
            if (edit_row < static_cast<int>(binding_legacy_formatters_.size()))
                binding_legacy_formatters_.erase(binding_legacy_formatters_.begin() + edit_row);
            return;
        }
        const auto result = dialog.binding();
        if (edit_row < static_cast<int>(binding_formatter_configs_.size()))
            binding_formatter_configs_[static_cast<size_t>(edit_row)] = result.formatter_config;
        if (edit_row < static_cast<int>(binding_legacy_formatters_.size()))
            binding_legacy_formatters_[static_cast<size_t>(edit_row)] = result.formatter;
        if (field_combo) {
            const int field_index = field_combo->findData(QString::fromStdString(result.field_path));
            if (field_index >= 0) field_combo->setCurrentIndex(field_index);
            else field_combo->setEditText(QString::fromStdString(result.field_path));
        }
        if (auto *item = bindings_table_->item(edit_row, 3))
            item->setText(formatter_summary(result.formatter_config, result.formatter));
        if (auto *item = bindings_table_->item(edit_row, 4))
            item->setText(result.has_fallback_value ? value_text(result.fallback_value) : QString());
        update_runtime_status();
    });
}

void ExternalDataSettingsDialog::update_binding_property_combo(int row)
{
    if (!title_ || row < 0 || row >= bindings_table_->rowCount())
        return;
    auto *layer_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 0));
    auto *property_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 1));
    if (!layer_combo || !property_combo)
        return;

    const std::string layer_id = layer_combo->currentData().toString().toStdString();
    const auto layer_it = std::find_if(
        title_->layers.begin(), title_->layers.end(),
        [&layer_id](const std::shared_ptr<Layer> &layer) {
            return layer && layer->id == layer_id;
        });

    const QString previous = property_combo->currentData().toString();
    property_combo->clear();
    if (layer_it == title_->layers.end())
        return;
    const QString property = canonical_property_for_layer(**layer_it);
    property_combo->addItem(property, property);
    const int previous_index = property_combo->findData(previous);
    if (previous_index >= 0)
        property_combo->setCurrentIndex(previous_index);
}

bool ExternalDataSettingsDialog::save_bindings_for_source(
    const std::string &source_id,
    bool report_errors)
{
    pending_bindings_.erase(
        std::remove_if(pending_bindings_.begin(), pending_bindings_.end(),
                       [&source_id](const PendingBinding &pending) {
                           return pending.binding.source_id == source_id;
                       }),
        pending_bindings_.end());

    const auto source_it = std::find_if(
        sources_.begin(), sources_.end(),
        [&source_id](const ExternalDataSourceDefinition &source) {
            return source.id == source_id;
        });
    if (source_it == sources_.end())
        return true;

    std::set<std::pair<std::string, std::string>> occupied_properties;
    for (const auto &pending : pending_bindings_)
        occupied_properties.emplace(pending.layer_id, pending.binding.property_path);

    std::vector<PendingBinding> parsed;
    for (int row = 0; row < bindings_table_->rowCount(); ++row) {
        auto *layer_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 0));
        auto *property_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 1));
        auto *field_combo = qobject_cast<QComboBox *>(bindings_table_->cellWidget(row, 2));
        if (!layer_combo || !property_combo || !field_combo)
            continue;

        const std::string layer_id = layer_combo->currentData().toString().toStdString();
        const std::string property_path = property_combo->currentData().toString().toStdString();
        QString field_text = field_combo->currentData().toString();
        if (field_combo->currentIndex() < 0 || field_text.isEmpty())
            field_text = field_combo->currentText().trimmed();
        const std::string field_path = field_text.trimmed().toStdString();
        if (layer_id.empty() || property_path.empty() || field_path.empty())
            continue;

        const auto property_key = std::make_pair(layer_id, property_path);
        if (!occupied_properties.insert(property_key).second) {
            if (report_errors) {
                QMessageBox::warning(
                    this, QStringLiteral("External Data"),
                    QStringLiteral("Layer property '%1' can only have one external-data binding.")
                        .arg(QString::fromStdString(property_path)));
            }
            return false;
        }

        ExternalPropertyBinding binding;
        binding.enabled = true;
        binding.source_id = source_id;
        binding.field_path = field_path;
        binding.property_path = property_path;
        if (row < static_cast<int>(binding_formatter_configs_.size()))
            binding.formatter_config = binding_formatter_configs_[static_cast<size_t>(row)];
        if (row < static_cast<int>(binding_legacy_formatters_.size()))
            binding.formatter = binding_legacy_formatters_[static_cast<size_t>(row)];

        /* A binding is itself an instruction to preserve this path. Runtime-
         * discovered fields are therefore pinned automatically, retaining their
         * inferred type for offline editing and future reconnects. */
        const bool newly_pinned = pin_external_data_field(*source_it, field_path);
        if (newly_pinned) {
            for (int field_row = 0; field_row < fields_table_->rowCount(); ++field_row) {
                const auto *path_item = fields_table_->item(field_row, 1);
                if (!path_item || path_item->text().trimmed().toStdString() != field_path)
                    continue;
                if (auto *pin_item = fields_table_->item(field_row, 0)) {
                    pin_item->setCheckState(Qt::Checked);
                    pin_item->setText(QStringLiteral("Pinned"));
                }
                break;
            }
        }
        const ExternalDataType field_type =
            external_data_field_type(*source_it, field_path);
        if (const auto *fallback_item = bindings_table_->item(row, 4)) {
            const QString fallback_text = fallback_item->text();
            if (!fallback_text.isEmpty()) {
                if (!parse_value(fallback_text, field_type, binding.fallback_value)) {
                    if (report_errors) {
                        QMessageBox::warning(
                            this, QStringLiteral("External Data"),
                            QStringLiteral("Fallback for field '%1' is not valid for type %2.")
                                .arg(QString::fromStdString(field_path), type_label(field_type)));
                    }
                    return false;
                }
                binding.has_fallback_value = true;
            }
        }
        parsed.push_back({layer_id, std::move(binding)});
    }

    pending_bindings_.insert(pending_bindings_.end(), parsed.begin(), parsed.end());
    return true;
}

void ExternalDataSettingsDialog::commit_and_accept()
{
    if (current_row_ >= 0 && !save_form_to_source(current_row_, true))
        return;
    if (!title_) {
        reject();
        return;
    }

    std::set<std::string> managed_source_ids(original_source_ids_.begin(),
                                             original_source_ids_.end());
    for (const auto &source : sources_)
        managed_source_ids.insert(source.id);

    for (auto &layer : title_->layers) {
        if (!layer)
            continue;
        layer->external_bindings.erase(
            std::remove_if(layer->external_bindings.begin(), layer->external_bindings.end(),
                           [&managed_source_ids](const ExternalPropertyBinding &binding) {
                               return managed_source_ids.find(binding.source_id) !=
                                      managed_source_ids.end();
                           }),
            layer->external_bindings.end());
    }
    for (const auto &pending : pending_bindings_) {
        const auto layer_it = std::find_if(
            title_->layers.begin(), title_->layers.end(),
            [&pending](const std::shared_ptr<Layer> &layer) {
                return layer && layer->id == pending.layer_id;
            });
        if (layer_it != title_->layers.end())
            (*layer_it)->external_bindings.push_back(pending.binding);
    }

    title_->external_data_sources = sources_;
    title_->external_data_enabled = !sources_.empty();
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "SettingsUI", [this]() {
            std::ostringstream stream;
            stream << "saved title=" << ExternalDataLog::sanitize(title_->id, 160)
                   << " sources=" << sources_.size()
                   << " bindings=" << pending_bindings_.size()
                   << " enabled=" << (title_->external_data_enabled ? 1 : 0);
            return stream.str();
        });
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    accept();
}
