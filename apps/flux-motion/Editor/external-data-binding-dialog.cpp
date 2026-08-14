#include "external-data-binding-dialog.h"
#include "fxm-modern-controls.h"

#include "external-data.h"
#include "external-data-log.h"
#include "title-assets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <functional>

namespace {

static QString value_text(const ExternalDataValue &value)
{
    return QString::fromStdString(external_data_value_to_string(value));
}

static bool parse_color(QString text, uint32_t &argb)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('#')))
        text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8)
        return false;
    bool ok = false;
    const qulonglong parsed = text.toULongLong(&ok, 16);
    if (!ok || parsed > UINT32_MAX)
        return false;
    argb = text.size() == 6 ? 0xFF000000u | static_cast<uint32_t>(parsed)
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
        if (!ok) return false;
        value = ExternalDataValue::integer(static_cast<int64_t>(parsed));
        return true;
    }
    case ExternalDataType::Float: {
        bool ok = false;
        const double parsed = trimmed.toDouble(&ok);
        if (!ok || !std::isfinite(parsed)) return false;
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
        if (!parse_color(trimmed, color)) return false;
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

static QString origin_label(ExternalDataValueOrigin origin)
{
    switch (origin) {
    case ExternalDataValueOrigin::LiveExternal: return QStringLiteral("Live external value");
    case ExternalDataValueOrigin::BindingFallback: return QStringLiteral("Binding fallback");
    case ExternalDataValueOrigin::FieldDefault: return QStringLiteral("Field default");
    case ExternalDataValueOrigin::Authored:
    default: return QStringLiteral("Authored value");
    }
}

static QString state_label(ExternalDataConnectionState state)
{
    switch (state) {
    case ExternalDataConnectionState::Connecting: return QStringLiteral("Connecting");
    case ExternalDataConnectionState::Connected: return QStringLiteral("Connected");
    case ExternalDataConnectionState::Error: return QStringLiteral("Error");
    case ExternalDataConnectionState::Updating: return QStringLiteral("Updating");
    case ExternalDataConnectionState::Stale: return QStringLiteral("Stale");
    case ExternalDataConnectionState::Disconnected:
    default: return QStringLiteral("Disconnected");
    }
}

} // namespace

ExternalDataBindingDialog::ExternalDataBindingDialog(
    std::shared_ptr<Title> title, std::string property_path,
    ExternalDataValue authored_value, const ExternalPropertyBinding *existing,
    QWidget *parent, std::string locked_source_id,
    std::vector<ExternalDataFieldDefinition> field_options,
    std::map<std::string, ExternalDataValue> preview_values)
    : QDialog(parent), title_(std::move(title)),
      property_path_(std::move(property_path)),
      authored_value_(std::move(authored_value)),
      locked_source_id_(std::move(locked_source_id)),
      field_options_(std::move(field_options)),
      preview_values_(std::move(preview_values))
{
    if (existing) {
        initial_ = *existing;
        has_initial_ = true;
    }
    fxm_apply_brand_icon(this);
    setWindowTitle(QStringLiteral("External Data Binding"));
    resize(700, 720);
    build_ui();
    populate_sources();
    load_existing();
    update_preview();

    last_field_revision_ = ExternalDataManager::instance().revision();
    preview_timer_ = new QTimer(this);
    preview_timer_->setInterval(300);
    connect(preview_timer_, &QTimer::timeout, this, [this]() {
        const uint64_t revision = ExternalDataManager::instance().revision();
        if (revision != last_field_revision_) {
            last_field_revision_ = revision;
            populate_fields();
        }
        update_preview();
    });
    preview_timer_->start();
}

void ExternalDataBindingDialog::build_ui()
{
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(
        QStringLiteral("Bind this property to a provider field. The authored value is preserved "
                       "and remains available as the final fallback."), this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *binding_group = new QGroupBox(QStringLiteral("Binding"), this);
    auto *binding_form = new QFormLayout(binding_group);
    source_combo_ = new QComboBox(binding_group);
    field_combo_ = new QComboBox(binding_group);
    field_combo_->setEditable(true);
    field_combo_->setInsertPolicy(QComboBox::NoInsert);
    fallback_edit_ = new QLineEdit(binding_group);
    fallback_edit_->setPlaceholderText(QStringLiteral("Optional typed fallback"));
    binding_form->addRow(QStringLiteral("Source"), source_combo_);
    binding_form->addRow(QStringLiteral("Field"), field_combo_);
    binding_form->addRow(QStringLiteral("Fallback"), fallback_edit_);
    outer->addWidget(binding_group);

    auto *formatter_group = new QGroupBox(QStringLiteral("Formatter pipeline"), this);
    auto *formatter_layout = new QVBoxLayout(formatter_group);
    auto *formatter_form = new QFormLayout();
    prefix_edit_ = new QLineEdit(formatter_group);
    suffix_edit_ = new QLineEdit(formatter_group);
    number_format_check_ = new FxmSwitch(QStringLiteral("Enable number formatting"), formatter_group);
    decimal_places_spin_ = new QSpinBox(formatter_group);
    decimal_places_spin_->setRange(-1, 12);
    decimal_places_spin_->setSpecialValueText(QStringLiteral("Automatic"));
    thousands_check_ = new FxmSwitch(QStringLiteral("Thousands separator"), formatter_group);
    case_combo_ = new QComboBox(formatter_group);
    case_combo_->addItem(QStringLiteral("Unchanged"), static_cast<int>(ExternalDataTextCase::None));
    case_combo_->addItem(QStringLiteral("UPPERCASE"), static_cast<int>(ExternalDataTextCase::Uppercase));
    case_combo_->addItem(QStringLiteral("lowercase"), static_cast<int>(ExternalDataTextCase::Lowercase));
    case_combo_->addItem(QStringLiteral("Title Case"), static_cast<int>(ExternalDataTextCase::TitleCase));
    date_format_edit_ = new QLineEdit(formatter_group);
    date_format_edit_->setPlaceholderText(QStringLiteral("strftime format, e.g. %d/%m/%Y %H:%M"));
    empty_mode_combo_ = new QComboBox(formatter_group);
    empty_mode_combo_->addItem(QStringLiteral("Keep empty"), static_cast<int>(ExternalDataEmptyValueMode::KeepEmpty));
    empty_mode_combo_->addItem(QStringLiteral("Use fallback/default/authored"), static_cast<int>(ExternalDataEmptyValueMode::UseFallback));
    empty_mode_combo_->addItem(QStringLiteral("Replace empty value"), static_cast<int>(ExternalDataEmptyValueMode::Replacement));
    empty_replacement_edit_ = new QLineEdit(formatter_group);
    formatter_form->addRow(QStringLiteral("Prefix"), prefix_edit_);
    formatter_form->addRow(QStringLiteral("Suffix"), suffix_edit_);
    formatter_form->addRow(QString(), number_format_check_);
    formatter_form->addRow(QStringLiteral("Decimal places"), decimal_places_spin_);
    formatter_form->addRow(QString(), thousands_check_);
    formatter_form->addRow(QStringLiteral("Text case"), case_combo_);
    formatter_form->addRow(QStringLiteral("Date/time format"), date_format_edit_);
    formatter_form->addRow(QStringLiteral("Empty value"), empty_mode_combo_);
    formatter_form->addRow(QStringLiteral("Empty replacement"), empty_replacement_edit_);
    formatter_layout->addLayout(formatter_form);

    auto *replacement_label = new QLabel(QStringLiteral("Conditional replacement (first matching rule wins)"), formatter_group);
    formatter_layout->addWidget(replacement_label);
    replacement_table_ = new QTableWidget(0, 3, formatter_group);
    replacement_table_->setHorizontalHeaderLabels({QStringLiteral("When value equals"),
                                                    QStringLiteral("Replace with"),
                                                    QStringLiteral("Case sensitive")});
    replacement_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    replacement_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    replacement_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    replacement_table_->setMaximumHeight(150);
    formatter_layout->addWidget(replacement_table_);
    auto *replacement_buttons = new QHBoxLayout();
    add_replacement_button_ = new QPushButton(QStringLiteral("Add rule"), formatter_group);
    remove_replacement_button_ = new QPushButton(QStringLiteral("Remove selected"), formatter_group);
    replacement_buttons->addWidget(add_replacement_button_);
    replacement_buttons->addWidget(remove_replacement_button_);
    replacement_buttons->addStretch();
    formatter_layout->addLayout(replacement_buttons);
    outer->addWidget(formatter_group, 1);

    auto *preview_group = new QGroupBox(QStringLiteral("Live preview"), this);
    auto *preview_form = new QFormLayout(preview_group);
    raw_preview_ = new QLabel(QStringLiteral("—"), preview_group);
    formatted_preview_ = new QLabel(QStringLiteral("—"), preview_group);
    state_preview_ = new QLabel(QStringLiteral("—"), preview_group);
    raw_preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formatted_preview_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formatted_preview_->setWordWrap(true);
    state_preview_->setWordWrap(true);
    preview_form->addRow(QStringLiteral("Raw value"), raw_preview_);
    preview_form->addRow(QStringLiteral("Formatted value"), formatted_preview_);
    preview_form->addRow(QStringLiteral("Origin / state"), state_preview_);
    outer->addWidget(preview_group);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    remove_binding_button_ = new QPushButton(QStringLiteral("Remove binding"), buttons);
    buttons->addButton(remove_binding_button_, QDialogButtonBox::DestructiveRole);
    remove_binding_button_->setVisible(has_initial_);
    outer->addWidget(buttons);

    connect(source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { populate_fields(); update_preview(); });
    connect(field_combo_, &QComboBox::currentTextChanged, this,
            [this](const QString &) { update_preview(); });
    for (QLineEdit *edit : {fallback_edit_, prefix_edit_, suffix_edit_,
                            date_format_edit_, empty_replacement_edit_}) {
        connect(edit, &QLineEdit::textChanged, this,
                [this](const QString &) { update_preview(); });
    }
    connect(number_format_check_, &QCheckBox::toggled, this,
            [this](bool) { update_formatter_visibility(); update_preview(); });
    connect(decimal_places_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int) { update_preview(); });
    connect(thousands_check_, &QCheckBox::toggled, this,
            [this](bool) { update_preview(); });
    connect(case_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { update_preview(); });
    connect(empty_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { update_formatter_visibility(); update_preview(); });
    connect(replacement_table_, &QTableWidget::cellChanged, this,
            [this](int, int) { update_preview(); });
    connect(add_replacement_button_, &QPushButton::clicked, this, [this]() {
        const int row = replacement_table_->rowCount();
        replacement_table_->insertRow(row);
        replacement_table_->setItem(row, 0, new QTableWidgetItem());
        replacement_table_->setItem(row, 1, new QTableWidgetItem());
        auto *case_item = new QTableWidgetItem();
        case_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        case_item->setCheckState(Qt::Checked);
        replacement_table_->setItem(row, 2, case_item);
    });
    connect(remove_replacement_button_, &QPushButton::clicked, this, [this]() {
        QList<int> rows;
        for (const QModelIndex &index : replacement_table_->selectionModel()->selectedRows())
            rows.push_back(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        for (int row : rows)
            replacement_table_->removeRow(row);
        update_preview();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        if (!collect_binding(true))
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "BindingUI", [this]() {
                std::ostringstream stream;
                stream << "binding accepted property="
                       << ExternalDataLog::sanitize(property_path_, 240)
                       << " source=" << ExternalDataLog::sanitize(result_.source_id, 160)
                       << " field=" << ExternalDataLog::sanitize(result_.field_path, 320)
                       << " fallback=" << (result_.has_fallback_value ? 1 : 0)
                       << " replacements="
                       << result_.formatter_config.conditional_replacements.size();
                return stream.str();
            });
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(remove_binding_button_, &QPushButton::clicked, this, [this]() {
        remove_requested_ = true;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "BindingUI", [this]() {
                return std::string("binding removal requested property=") +
                       ExternalDataLog::sanitize(property_path_, 240) +
                       " source=" + ExternalDataLog::sanitize(result_.source_id, 160) +
                       " field=" + ExternalDataLog::sanitize(result_.field_path, 320);
            });
        accept();
    });
}

void ExternalDataBindingDialog::populate_sources()
{
    source_combo_->clear();
    if (!title_)
        return;
    for (const auto &source : title_->external_data_sources) {
        if (!locked_source_id_.empty() && source.id != locked_source_id_)
            continue;
        const QString name = source.name.empty() ? QString::fromStdString(source.id)
                                                 : QString::fromStdString(source.name);
        source_combo_->addItem(name, QString::fromStdString(source.id));
    }
    source_combo_->setEnabled(locked_source_id_.empty());
    populate_fields();
}

void ExternalDataBindingDialog::populate_fields()
{
    const QString previous = field_combo_->currentData().toString().isEmpty()
        ? field_combo_->currentText() : field_combo_->currentData().toString();
    field_combo_->clear();
    if (!title_)
        return;
    const std::string source_id = source_combo_->currentData().toString().toStdString();
    const auto source = std::find_if(title_->external_data_sources.begin(),
                                    title_->external_data_sources.end(),
        [&source_id](const ExternalDataSourceDefinition &candidate) {
            return candidate.id == source_id;
        });
    if (source != title_->external_data_sources.end()) {
        const auto available = field_options_.empty()
            ? available_external_data_fields(*source) : field_options_;
        for (const auto &field : available) {
            const QString path = QString::fromStdString(field.path);
            const bool pinned = std::any_of(
                source->fields.begin(), source->fields.end(),
                [&field](const ExternalDataFieldDefinition &candidate) {
                    return candidate.path == field.path;
                });
            QString label = field.name.empty() ? path
                : QStringLiteral("%1 — %2").arg(QString::fromStdString(field.name), path);
            if (!pinned)
                label += QStringLiteral("  [discovered]");
            field_combo_->addItem(label, path);
        }
    }
    int index = field_combo_->findData(previous);
    if (index >= 0)
        field_combo_->setCurrentIndex(index);
    else if (!previous.isEmpty())
        field_combo_->setEditText(previous);
    update_formatter_visibility();
}

void ExternalDataBindingDialog::load_existing()
{
    if (!has_initial_) {
        update_formatter_visibility();
        return;
    }
    int source_index = source_combo_->findData(QString::fromStdString(initial_.source_id));
    if (source_index >= 0)
        source_combo_->setCurrentIndex(source_index);
    populate_fields();
    int field_index = field_combo_->findData(QString::fromStdString(initial_.field_path));
    if (field_index >= 0)
        field_combo_->setCurrentIndex(field_index);
    else
        field_combo_->setEditText(QString::fromStdString(initial_.field_path));
    if (initial_.has_fallback_value)
        fallback_edit_->setText(value_text(initial_.fallback_value));
    const auto &formatter = initial_.formatter_config;
    prefix_edit_->setText(QString::fromStdString(formatter.prefix));
    suffix_edit_->setText(QString::fromStdString(formatter.suffix));
    number_format_check_->setChecked(formatter.number_format_enabled);
    decimal_places_spin_->setValue(formatter.decimal_places);
    thousands_check_->setChecked(formatter.thousands_separator);
    case_combo_->setCurrentIndex(case_combo_->findData(static_cast<int>(formatter.text_case)));
    date_format_edit_->setText(QString::fromStdString(formatter.date_time_format));
    empty_mode_combo_->setCurrentIndex(empty_mode_combo_->findData(static_cast<int>(formatter.empty_value_mode)));
    empty_replacement_edit_->setText(QString::fromStdString(formatter.empty_replacement));
    for (const auto &rule : formatter.conditional_replacements) {
        const int row = replacement_table_->rowCount();
        replacement_table_->insertRow(row);
        replacement_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(rule.match)));
        replacement_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(rule.replacement)));
        auto *case_item = new QTableWidgetItem();
        case_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        case_item->setCheckState(rule.case_sensitive ? Qt::Checked : Qt::Unchecked);
        replacement_table_->setItem(row, 2, case_item);
    }
    update_formatter_visibility();
}

ExternalDataType ExternalDataBindingDialog::selected_field_type() const
{
    if (!title_)
        return ExternalDataType::String;
    const std::string source_id = source_combo_->currentData().toString().toStdString();
    QString field_q = field_combo_->currentData().toString();
    if (field_q.isEmpty())
        field_q = field_combo_->currentText().trimmed();
    const std::string field_path = field_q.toStdString();
    if (!field_options_.empty()) {
        const auto field = std::find_if(
            field_options_.begin(), field_options_.end(),
            [&field_path](const ExternalDataFieldDefinition &candidate) {
                return candidate.path == field_path;
            });
        if (field != field_options_.end())
            return field->type;
    }
    for (const auto &source : title_->external_data_sources) {
        if (source.id == source_id)
            return external_data_field_type(source, field_path);
    }
    return ExternalDataType::String;
}

void ExternalDataBindingDialog::update_formatter_visibility()
{
    const ExternalDataType type = selected_field_type();
    const bool numeric = type == ExternalDataType::Integer || type == ExternalDataType::Float;
    number_format_check_->setEnabled(numeric);
    decimal_places_spin_->setEnabled(numeric && number_format_check_->isChecked());
    thousands_check_->setEnabled(numeric && number_format_check_->isChecked());
    date_format_edit_->setEnabled(type == ExternalDataType::DateTime);
    const auto empty_mode = static_cast<ExternalDataEmptyValueMode>(empty_mode_combo_->currentData().toInt());
    empty_replacement_edit_->setEnabled(empty_mode == ExternalDataEmptyValueMode::Replacement);
}

bool ExternalDataBindingDialog::collect_binding(bool report_error)
{
    ExternalPropertyBinding binding;
    binding.enabled = true;
    binding.property_path = property_path_;
    binding.source_id = source_combo_->currentData().toString().toStdString();
    QString field = field_combo_->currentData().toString();
    if (field.isEmpty())
        field = field_combo_->currentText().trimmed();
    binding.field_path = field.toStdString();
    if (binding.source_id.empty() || binding.field_path.empty()) {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Warning, "BindingUI", [this]() {
                return std::string("binding validation failed property=") +
                       ExternalDataLog::sanitize(property_path_, 240) +
                       " reason=missing-source-or-field";
            });
        if (report_error)
            QMessageBox::warning(this, QStringLiteral("External Data Binding"),
                                 QStringLiteral("Select both a source and a field."));
        return false;
    }

    const QString fallback = fallback_edit_->text();
    if (!fallback.isEmpty()) {
        if (!parse_value(fallback, selected_field_type(), binding.fallback_value)) {
            if (report_error)
                QMessageBox::warning(this, QStringLiteral("External Data Binding"),
                                     QStringLiteral("The fallback value is invalid for the selected field type."));
            return false;
        }
        binding.has_fallback_value = true;
    }

    auto &formatter = binding.formatter_config;
    formatter.prefix = prefix_edit_->text().toStdString();
    formatter.suffix = suffix_edit_->text().toStdString();
    formatter.number_format_enabled = number_format_check_->isChecked();
    formatter.decimal_places = decimal_places_spin_->value();
    formatter.thousands_separator = thousands_check_->isChecked();
    formatter.text_case = static_cast<ExternalDataTextCase>(case_combo_->currentData().toInt());
    formatter.date_time_format = date_format_edit_->text().toStdString();
    formatter.empty_value_mode = static_cast<ExternalDataEmptyValueMode>(empty_mode_combo_->currentData().toInt());
    formatter.empty_replacement = empty_replacement_edit_->text().toStdString();
    for (int row = 0; row < replacement_table_->rowCount(); ++row) {
        const auto *match_item = replacement_table_->item(row, 0);
        const auto *replace_item = replacement_table_->item(row, 1);
        const QString match = match_item ? match_item->text() : QString();
        if (match.isEmpty())
            continue;
        ExternalDataConditionalReplacement rule;
        rule.match = match.toStdString();
        rule.replacement = replace_item ? replace_item->text().toStdString() : std::string{};
        const auto *case_item = replacement_table_->item(row, 2);
        rule.case_sensitive = !case_item || case_item->checkState() == Qt::Checked;
        formatter.conditional_replacements.push_back(std::move(rule));
    }
    result_ = std::move(binding);
    return true;
}

void ExternalDataBindingDialog::update_preview()
{
    if (!collect_binding(false)) {
        raw_preview_->setText(QStringLiteral("—"));
        formatted_preview_->setText(QStringLiteral("Select a source and field"));
        state_preview_->setText(QStringLiteral("—"));
        return;
    }
    ExternalDataResolution resolution;
    const auto table_preview = preview_values_.find(result_.field_path);
    const bool has_table_preview = table_preview != preview_values_.end();
    if (has_table_preview) {
        resolution.value = table_preview->second;
        resolution.origin = ExternalDataValueOrigin::LiveExternal;
    } else {
        resolution = ExternalDataManager::instance().resolve(result_, authored_value_);
    }
    raw_preview_->setText(value_text(resolution.value));
    formatted_preview_->setText(QString::fromStdString(format_external_data_value(
        resolution.value, result_.formatter_config, result_.formatter)));
    const ExternalDataSourceState state = ExternalDataManager::instance().source_state(result_.source_id);
    QString status = has_table_preview
        ? QStringLiteral("Table row preview · %1").arg(state_label(state.connection_state))
        : QStringLiteral("%1 · %2")
              .arg(origin_label(resolution.origin), state_label(state.connection_state));
    if (!state.error_message.empty())
        status += QStringLiteral(" · %1").arg(QString::fromStdString(state.error_message));
    if (state.last_update_timestamp_ms > 0) {
        status += QStringLiteral(" · %1").arg(
            QDateTime::fromMSecsSinceEpoch(state.last_update_timestamp_ms)
                .toString(Qt::ISODateWithMs));
    }
    state_preview_->setText(status);
}
