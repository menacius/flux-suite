#include "external-data-table-mapping-dialog.h"
#include "fxm-modern-controls.h"

#include "external-data-binding-dialog.h"
#include "external-data-provider.h"
#include "external-data.h"
#include "external-data-log.h"
#include "live-text-cue-utils.h"
#include "title-assets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>

namespace {

static QString display_value(const ExternalDataValue &value,
                             const ExternalPropertyBinding &binding)
{
    return QString::fromStdString(format_external_data_value(
        value, binding.formatter_config, binding.formatter));
}

static QString mode_description(LiveTextTableUpdateMode mode)
{
    switch (mode) {
    case LiveTextTableUpdateMode::ReplaceRows:
        return QStringLiteral("Replace all cue rows with the provider table.");
    case LiveTextTableUpdateMode::AppendRows:
        return QStringLiteral("Add rows that are not already managed by this mapping.");
    case LiveTextTableUpdateMode::SynchronizeRows:
    default:
        return QStringLiteral("Update, add and remove source-managed rows while preserving stable row IDs.");
    }
}

} // namespace

ExternalDataTableMappingDialog::ExternalDataTableMappingDialog(
    std::shared_ptr<Title> title, const LiveTextTableBinding *existing,
    QWidget *parent)
    : QDialog(parent), title_(std::move(title))
{
    if (existing) {
        initial_ = *existing;
        has_initial_ = true;
    }
    fxm_apply_brand_icon(this);
    setWindowTitle(QStringLiteral("Populate Live Text Cues from Table"));
    resize(960, 760);
    build_ui();
    populate_sources();
    load_existing();
    refresh_preview();

    last_revision_ = ExternalDataManager::instance().revision();
    preview_timer_ = new QTimer(this);
    preview_timer_->setInterval(350);
    connect(preview_timer_, &QTimer::timeout, this, [this]() {
        const uint64_t revision = ExternalDataManager::instance().revision();
        if (revision != last_revision_) {
            last_revision_ = revision;
            const QString table_path = table_combo_->currentData().toString();
            populate_tables();
            const int index = table_combo_->findData(table_path);
            if (index >= 0)
                table_combo_->setCurrentIndex(index);
            rebuild_column_mapping();
        }
        refresh_preview();
    });
    preview_timer_->start();
}

void ExternalDataTableMappingDialog::build_ui()
{
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(
        QStringLiteral("Map each exposed Live Text Cue column to a column in an external table. "
                       "Rows remain live bindings, so provider updates use the same formatter and fallback pipeline as ordinary cue cells."),
        this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *source_group = new QGroupBox(QStringLiteral("Table source"), this);
    auto *source_form = new QFormLayout(source_group);
    source_combo_ = new QComboBox(source_group);
    table_combo_ = new QComboBox(source_group);
    refresh_button_ = new QPushButton(QStringLiteral("Refresh provider"), source_group);
    auto *table_row = new QWidget(source_group);
    auto *table_row_layout = new QHBoxLayout(table_row);
    table_row_layout->setContentsMargins(0, 0, 0, 0);
    table_row_layout->addWidget(table_combo_, 1);
    table_row_layout->addWidget(refresh_button_);
    source_form->addRow(QStringLiteral("Source"), source_combo_);
    source_form->addRow(QStringLiteral("Table / array"), table_row);
    outer->addWidget(source_group);

    auto *behavior_group = new QGroupBox(QStringLiteral("Row behavior"), this);
    auto *behavior_form = new QFormLayout(behavior_group);
    mode_combo_ = new QComboBox(behavior_group);
    mode_combo_->addItem(QStringLiteral("Replace rows"),
                         static_cast<int>(LiveTextTableUpdateMode::ReplaceRows));
    mode_combo_->addItem(QStringLiteral("Append rows"),
                         static_cast<int>(LiveTextTableUpdateMode::AppendRows));
    mode_combo_->addItem(QStringLiteral("Synchronize rows"),
                         static_cast<int>(LiveTextTableUpdateMode::SynchronizeRows));
    row_id_combo_ = new QComboBox(behavior_group);
    row_id_combo_->setEditable(true);
    row_id_combo_->addItem(QStringLiteral("Use source row index"), QString());
    start_row_spin_ = new QSpinBox(behavior_group);
    start_row_spin_->setRange(0, 1000000);
    maximum_rows_spin_ = new QSpinBox(behavior_group);
    maximum_rows_spin_->setRange(0, 1000000);
    maximum_rows_spin_->setSpecialValueText(QStringLiteral("All rows"));
    ignore_empty_check_ = new FxmSwitch(QStringLiteral("Ignore rows where all mapped values are empty"), behavior_group);
    ignore_empty_check_->setChecked(true);
    preserve_manual_check_ = new FxmSwitch(QStringLiteral("Preserve manually created cue rows"), behavior_group);
    preserve_manual_check_->setChecked(true);
    behavior_form->addRow(QStringLiteral("Update mode"), mode_combo_);
    behavior_form->addRow(QStringLiteral("Stable row ID field"), row_id_combo_);
    behavior_form->addRow(QStringLiteral("Start at row"), start_row_spin_);
    behavior_form->addRow(QStringLiteral("Maximum rows"), maximum_rows_spin_);
    behavior_form->addRow(QString(), ignore_empty_check_);
    behavior_form->addRow(QString(), preserve_manual_check_);
    outer->addWidget(behavior_group);

    auto *mapping_group = new QGroupBox(QStringLiteral("Cue column mapping"), this);
    auto *mapping_layout = new QVBoxLayout(mapping_group);
    mapping_table_ = new QTableWidget(mapping_group);
    mapping_table_->setColumnCount(4);
    mapping_table_->setHorizontalHeaderLabels({QStringLiteral("Live cue column"),
                                                QStringLiteral("External field"),
                                                QStringLiteral("Formatter / fallback"),
                                                QStringLiteral("Live preview")});
    mapping_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    mapping_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    mapping_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    mapping_table_->verticalHeader()->setVisible(false);
    mapping_table_->setSelectionMode(QAbstractItemView::NoSelection);
    mapping_layout->addWidget(mapping_table_);
    outer->addWidget(mapping_group, 1);

    auto *preview_group = new QGroupBox(QStringLiteral("Result preview"), this);
    auto *preview_layout = new QVBoxLayout(preview_group);
    status_label_ = new QLabel(preview_group);
    status_label_->setWordWrap(true);
    preview_table_ = new QTableWidget(preview_group);
    preview_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    preview_table_->setSelectionMode(QAbstractItemView::NoSelection);
    preview_table_->verticalHeader()->setVisible(false);
    preview_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    preview_layout->addWidget(status_label_);
    preview_layout->addWidget(preview_table_);
    outer->addWidget(preview_group, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    remove_mapping_button_ = buttons->addButton(QStringLiteral("Remove table mapping"),
                                                QDialogButtonBox::DestructiveRole);
    remove_mapping_button_->setVisible(has_initial_);
    outer->addWidget(buttons);

    connect(source_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { populate_tables(); rebuild_column_mapping(); refresh_preview(); });
    connect(table_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { rebuild_column_mapping(); refresh_preview(); });
    connect(mode_combo_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() {
                preserve_manual_check_->setEnabled(
                    static_cast<LiveTextTableUpdateMode>(mode_combo_->currentData().toInt()) ==
                    LiveTextTableUpdateMode::SynchronizeRows);
                mode_combo_->setToolTip(mode_description(
                    static_cast<LiveTextTableUpdateMode>(mode_combo_->currentData().toInt())));
                refresh_preview();
            });
    connect(row_id_combo_, &QComboBox::currentTextChanged, this,
            [this]() { refresh_preview(); });
    connect(start_row_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this]() { refresh_preview(); });
    connect(maximum_rows_spin_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this]() { refresh_preview(); });
    connect(ignore_empty_check_, &QCheckBox::toggled, this,
            [this]() { refresh_preview(); });
    connect(refresh_button_, &QPushButton::clicked, this, [this]() {
        const std::string source_id = source_combo_->currentData().toString().toStdString();
        if (source_id.empty())
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "TableMappingUI", [&]() {
                return std::string("refresh requested source=") +
                       ExternalDataLog::sanitize(source_id, 160);
            });
        ExternalDataProviderService::instance().connect_source(source_id);
        ExternalDataProviderService::instance().refresh_source(source_id);
        status_label_->setText(QStringLiteral("Refresh requested asynchronously…"));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!collect_mapping(true))
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "TableMappingUI", [this]() {
                std::ostringstream stream;
                stream << "mapping accepted id=" << ExternalDataLog::sanitize(result_.id, 160)
                       << " source=" << ExternalDataLog::sanitize(result_.source_id, 160)
                       << " table=" << ExternalDataLog::sanitize(result_.table_path, 320)
                       << " mode=" << ExternalDataLog::table_update_mode_name(result_.update_mode)
                       << " columns=" << result_.columns.size()
                       << " startRow=" << result_.start_row
                       << " maximumRows=" << result_.maximum_rows;
                return stream.str();
            });
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(remove_mapping_button_, &QPushButton::clicked, this, [this]() {
        remove_requested_ = true;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "TableMappingUI", [this]() {
                return std::string("mapping removal requested id=") +
                       ExternalDataLog::sanitize(initial_.id, 160);
            });
        accept();
    });
}

void ExternalDataTableMappingDialog::populate_sources()
{
    source_combo_->clear();
    if (!title_)
        return;
    for (const auto &source : title_->external_data_sources) {
        const QString name = source.name.empty()
            ? QString::fromStdString(source.id)
            : QString::fromStdString(source.name);
        source_combo_->addItem(name, QString::fromStdString(source.id));
    }
    populate_tables();
}

void ExternalDataTableMappingDialog::populate_tables()
{
    const QString previous = table_combo_->currentData().toString();
    table_combo_->clear();
    const std::string source_id = source_combo_->currentData().toString().toStdString();
    const auto tables = ExternalDataManager::instance().table_snapshots(source_id);
    for (const auto &table : tables) {
        table_combo_->addItem(
            QStringLiteral("%1  (%2 rows × %3 columns)")
                .arg(QString::fromStdString(table.path))
                .arg(static_cast<qulonglong>(table.rows.size()))
                .arg(static_cast<qulonglong>(table.columns.size())),
            QString::fromStdString(table.path));
    }
    int index = table_combo_->findData(previous);
    if (index >= 0)
        table_combo_->setCurrentIndex(index);
    else if (table_combo_->count() > 0)
        table_combo_->setCurrentIndex(0);
}

ExternalDataTableSnapshot ExternalDataTableMappingDialog::current_table() const
{
    return ExternalDataManager::instance().table_snapshot(
        source_combo_->currentData().toString().toStdString(),
        table_combo_->currentData().toString().toStdString());
}

std::vector<ExternalDataFieldDefinition>
ExternalDataTableMappingDialog::current_table_fields() const
{
    const auto table = current_table();
    std::vector<ExternalDataFieldDefinition> fields;
    fields.reserve(table.columns.size());
    for (const auto &column : table.columns) {
        ExternalDataFieldDefinition field;
        field.path = column;
        field.name = column;
        for (const auto &row : table.rows) {
            const auto value = row.values.find(column);
            if (value != row.values.end()) {
                field.type = value->second.type;
                break;
            }
        }
        fields.push_back(std::move(field));
    }
    return fields;
}

void ExternalDataTableMappingDialog::rebuild_column_mapping()
{
    if (!mapping_table_ || !title_)
        return;
    const auto exposed = fxm::live_text::exposed_text_layers(title_);
    const auto fields = current_table_fields();
    const auto previous = column_bindings_;
    const std::string current_source_id =
        source_combo_->currentData().toString().toStdString();
    const std::string current_table_path =
        table_combo_->currentData().toString().toStdString();
    const bool same_previous_context =
        current_source_id == column_context_source_id_ &&
        current_table_path == column_context_table_path_;
    const bool initial_context = has_initial_ && !initial_applied_ &&
        current_source_id == initial_.source_id &&
        current_table_path == initial_.table_path;
    column_bindings_.assign(exposed.size(), ExternalPropertyBinding{});
    mapping_table_->setRowCount(static_cast<int>(exposed.size()));

    row_id_combo_->blockSignals(true);
    QString previous_row_id;
    if (same_previous_context) {
        previous_row_id = row_id_combo_->currentData().toString().isEmpty()
            ? row_id_combo_->currentText() : row_id_combo_->currentData().toString();
    } else if (initial_context) {
        previous_row_id = QString::fromStdString(initial_.row_id_field);
    }
    row_id_combo_->clear();
    row_id_combo_->addItem(QStringLiteral("Use source row index"), QString());
    for (const auto &field : fields)
        row_id_combo_->addItem(QString::fromStdString(field.path),
                               QString::fromStdString(field.path));
    int row_id_index = row_id_combo_->findData(previous_row_id);
    if (row_id_index >= 0)
        row_id_combo_->setCurrentIndex(row_id_index);
    else if (!previous_row_id.isEmpty())
        row_id_combo_->setEditText(previous_row_id);
    row_id_combo_->blockSignals(false);

    for (int row = 0; row < static_cast<int>(exposed.size()); ++row) {
        const auto &layer = exposed[static_cast<size_t>(row)];
        auto *name_item = new QTableWidgetItem(
            layer ? QString::fromStdString(layer->name) : QStringLiteral("Unknown layer"));
        name_item->setFlags(Qt::ItemIsEnabled);
        mapping_table_->setItem(row, 0, name_item);

        auto *field_combo = new QComboBox(mapping_table_);
        field_combo->addItem(QStringLiteral("Not mapped"), QString());
        for (const auto &field : fields)
            field_combo->addItem(QString::fromStdString(field.path),
                                 QString::fromStdString(field.path));
        mapping_table_->setCellWidget(row, 1, field_combo);

        auto *format_button = new QPushButton(QStringLiteral("Configure…"), mapping_table_);
        mapping_table_->setCellWidget(row, 2, format_button);
        auto *preview = new QLabel(mapping_table_);
        preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        mapping_table_->setCellWidget(row, 3, preview);

        ExternalPropertyBinding binding;
        binding.property_path = layer && layer->type == LayerType::Image
            ? ExternalPropertyPaths::ImagePath
            : ExternalPropertyPaths::TextContent;
        binding.source_id = current_source_id;
        const auto initial_column = std::find_if(
            initial_.columns.begin(), initial_.columns.end(),
            [&layer](const LiveTextTableColumnBinding &candidate) {
                return layer && candidate.layer_id == layer->id;
            });
        if (initial_context && initial_column != initial_.columns.end())
            binding = initial_column->binding;
        else if (same_previous_context && row < static_cast<int>(previous.size()))
            binding = previous[static_cast<size_t>(row)];
        binding.source_id = current_source_id;
        int field_index = field_combo->findData(QString::fromStdString(binding.field_path));
        if (field_index < 0 && !binding.field_path.empty() && initial_context) {
            field_combo->addItem(
                QStringLiteral("%1  [configured]")
                    .arg(QString::fromStdString(binding.field_path)),
                QString::fromStdString(binding.field_path));
            field_index = field_combo->count() - 1;
        }
        if (field_index >= 0)
            field_combo->setCurrentIndex(field_index);
        else
            binding.field_path.clear();
        column_bindings_[static_cast<size_t>(row)] = binding;

        connect(field_combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, row, field_combo](int) {
                    if (row < 0 || row >= static_cast<int>(column_bindings_.size()))
                        return;
                    column_bindings_[static_cast<size_t>(row)].field_path =
                        field_combo->currentData().toString().toStdString();
                    refresh_preview();
                });
        connect(format_button, &QPushButton::clicked, this,
                [this, row]() { configure_column(row); });
    }
    column_context_source_id_ = current_source_id;
    column_context_table_path_ = current_table_path;
    refresh_preview();
}

void ExternalDataTableMappingDialog::load_existing()
{
    if (!has_initial_)
        return;
    int source = source_combo_->findData(QString::fromStdString(initial_.source_id));
    if (source >= 0)
        source_combo_->setCurrentIndex(source);
    populate_tables();
    int table = table_combo_->findData(QString::fromStdString(initial_.table_path));
    if (table >= 0)
        table_combo_->setCurrentIndex(table);
    mode_combo_->setCurrentIndex(mode_combo_->findData(static_cast<int>(initial_.update_mode)));
    start_row_spin_->setValue(initial_.start_row);
    maximum_rows_spin_->setValue(initial_.maximum_rows);
    ignore_empty_check_->setChecked(initial_.ignore_empty_rows);
    preserve_manual_check_->setChecked(initial_.preserve_manual_rows);
    rebuild_column_mapping();
    int row_id = row_id_combo_->findData(QString::fromStdString(initial_.row_id_field));
    if (row_id >= 0)
        row_id_combo_->setCurrentIndex(row_id);
    else if (!initial_.row_id_field.empty())
        row_id_combo_->setEditText(QString::fromStdString(initial_.row_id_field));
    initial_applied_ = true;
}

void ExternalDataTableMappingDialog::configure_column(int row)
{
    if (!title_ || row < 0 || row >= static_cast<int>(column_bindings_.size()))
        return;
    const auto fields = current_table_fields();
    if (fields.empty()) {
        QMessageBox::information(this, QStringLiteral("Table mapping"),
                                 QStringLiteral("Refresh the provider before configuring a table column."));
        return;
    }
    ExternalPropertyBinding current = column_bindings_[static_cast<size_t>(row)];
    std::map<std::string, ExternalDataValue> preview_values;
    const auto table = current_table();
    if (!table.rows.empty())
        preview_values = table.rows.front().values;
    ExternalDataBindingDialog dialog(
        title_, current.property_path, ExternalDataValue::string(std::string{}),
        current.field_path.empty() ? nullptr : &current, this,
        source_combo_->currentData().toString().toStdString(), fields,
        std::move(preview_values));
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (dialog.remove_requested()) {
        current.field_path.clear();
        current.formatter.clear();
        current.formatter_config = {};
        current.has_fallback_value = false;
        current.fallback_value = {};
    } else {
        current = dialog.binding();
        current.source_id = source_combo_->currentData().toString().toStdString();
    }
    column_bindings_[static_cast<size_t>(row)] = current;
    if (auto *combo = qobject_cast<QComboBox *>(mapping_table_->cellWidget(row, 1))) {
        const int index = combo->findData(QString::fromStdString(current.field_path));
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }
    refresh_preview();
}

bool ExternalDataTableMappingDialog::collect_mapping(bool report_error)
{
    LiveTextTableBinding mapping;
    mapping.enabled = true;
    mapping.id = !initial_.id.empty()
        ? initial_.id : TitleDataStore::make_uuid();
    mapping.source_id = source_combo_->currentData().toString().toStdString();
    mapping.table_path = table_combo_->currentData().toString().toStdString();
    mapping.update_mode = static_cast<LiveTextTableUpdateMode>(mode_combo_->currentData().toInt());
    QString row_id = row_id_combo_->currentData().toString();
    if (row_id.isEmpty())
        row_id = row_id_combo_->currentText().trimmed();
    if (row_id == QStringLiteral("Use source row index"))
        row_id.clear();
    mapping.row_id_field = row_id.toStdString();
    mapping.start_row = start_row_spin_->value();
    mapping.maximum_rows = maximum_rows_spin_->value();
    mapping.ignore_empty_rows = ignore_empty_check_->isChecked();
    mapping.preserve_manual_rows = preserve_manual_check_->isChecked();

    const auto exposed = fxm::live_text::exposed_text_layers(title_);
    for (size_t i = 0; i < exposed.size() && i < column_bindings_.size(); ++i) {
        if (!exposed[i] || column_bindings_[i].field_path.empty())
            continue;
        LiveTextTableColumnBinding column;
        column.layer_id = exposed[i]->id;
        column.binding = column_bindings_[i];
        column.binding.source_id = mapping.source_id;
        mapping.columns.push_back(std::move(column));
    }
    if (mapping.source_id.empty() || mapping.table_path.empty() || mapping.columns.empty()) {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Warning, "TableMappingUI", [&]() {
                std::ostringstream stream;
                stream << "mapping validation failed source="
                       << ExternalDataLog::sanitize(mapping.source_id, 160)
                       << " table=" << ExternalDataLog::sanitize(mapping.table_path, 320)
                       << " columns=" << mapping.columns.size();
                return stream.str();
            });
        if (report_error)
            QMessageBox::warning(this, QStringLiteral("Table mapping"),
                                 QStringLiteral("Select a source, a table, and map at least one cue column."));
        return false;
    }
    result_ = std::move(mapping);
    return true;
}

void ExternalDataTableMappingDialog::refresh_preview()
{
    const ExternalDataTableSnapshot table = current_table();
    if (table.path.empty()) {
        preview_table_->clear();
        preview_table_->setRowCount(0);
        preview_table_->setColumnCount(0);
        status_label_->setText(QStringLiteral("No table snapshot is available. Click Refresh provider or Test connection first."));
        return;
    }

    const auto exposed = title_ ? fxm::live_text::exposed_text_layers(title_)
                                : std::vector<std::shared_ptr<Layer>>{};
    std::vector<size_t> mapped_columns;
    for (size_t i = 0; i < column_bindings_.size(); ++i) {
        if (!column_bindings_[i].field_path.empty())
            mapped_columns.push_back(i);
    }
    preview_table_->clear();
    preview_table_->setColumnCount(static_cast<int>(mapped_columns.size()));
    QStringList headers;
    for (size_t index : mapped_columns) {
        headers.push_back(index < exposed.size() && exposed[index]
            ? QString::fromStdString(exposed[index]->name)
            : QStringLiteral("Cue column"));
    }
    preview_table_->setHorizontalHeaderLabels(headers);

    const size_t start = std::min<size_t>(static_cast<size_t>(start_row_spin_->value()),
                                          table.rows.size());
    size_t end = table.rows.size();
    if (maximum_rows_spin_->value() > 0)
        end = std::min(end, start + static_cast<size_t>(maximum_rows_spin_->value()));
    end = std::min(end, start + static_cast<size_t>(20));

    int output_row = 0;
    preview_table_->setRowCount(static_cast<int>(end - start));
    for (size_t row_index = start; row_index < end; ++row_index) {
        const auto &row = table.rows[row_index];
        bool any = false;
        for (size_t visual_column = 0; visual_column < mapped_columns.size(); ++visual_column) {
            const size_t binding_index = mapped_columns[visual_column];
            const auto &binding = column_bindings_[binding_index];
            QString text;
            const auto value = row.values.find(binding.field_path);
            if (value != row.values.end()) {
                text = display_value(value->second, binding);
                any = any || !external_data_value_is_empty(value->second);
            } else if (binding.has_fallback_value) {
                text = display_value(binding.fallback_value, binding);
            }
            preview_table_->setItem(output_row, static_cast<int>(visual_column),
                                    new QTableWidgetItem(text));
            if (auto *label = qobject_cast<QLabel *>(mapping_table_->cellWidget(
                    static_cast<int>(binding_index), 3))) {
                label->setText(text);
            }
        }
        if (ignore_empty_check_->isChecked() && !any) {
            preview_table_->removeRow(output_row);
            continue;
        }
        ++output_row;
    }
    preview_table_->setRowCount(output_row);
    const auto state = ExternalDataManager::instance().source_state(
        source_combo_->currentData().toString().toStdString());
    status_label_->setText(
        QStringLiteral("%1 rows available; previewing %2. Provider state: %3")
            .arg(static_cast<qulonglong>(table.rows.size()))
            .arg(output_row)
            .arg(QString::fromLatin1(external_data_connection_state_name(state.connection_state))));
}
