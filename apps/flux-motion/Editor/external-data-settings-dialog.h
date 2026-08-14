#pragma once

#include "title-data.h"

#include <QDialog>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class ExternalDataSettingsDialog final : public QDialog {
public:
    explicit ExternalDataSettingsDialog(std::shared_ptr<Title> title,
                                        QWidget *parent = nullptr);
    ~ExternalDataSettingsDialog() override;

private:
    void build_ui();
    void populate_source_list();
    void select_source(int row);
    void load_source_to_form(int row);
    bool save_form_to_source(int row, bool report_errors);
    void clear_form();
    void update_control_visibility();
    void update_runtime_status();
    void refresh_discovered_fields();
    void add_field_row(const ExternalDataFieldDefinition *field = nullptr,
                       const ExternalDataValue *manual_value = nullptr,
                       bool pinned = true, bool discovered = false,
                       const ExternalDataValue *live_value = nullptr);
    void load_bindings_for_source(const std::string &source_id);
    bool save_bindings_for_source(const std::string &source_id, bool report_errors);
    void add_binding_row(const std::string &layer_id = {},
                         const ExternalPropertyBinding *binding = nullptr);
    void update_binding_property_combo(int row);
    void commit_and_accept();

    std::shared_ptr<Title> title_;
    struct PendingBinding {
        std::string layer_id;
        ExternalPropertyBinding binding;
    };

    std::vector<ExternalDataSourceDefinition> sources_;
    std::vector<PendingBinding> pending_bindings_;
    std::vector<std::string> original_source_ids_;
    int current_row_ = -1;
    bool loading_ = false;

    QListWidget *source_list_ = nullptr;
    QPushButton *add_source_button_ = nullptr;
    QPushButton *remove_source_button_ = nullptr;
    QPushButton *duplicate_source_button_ = nullptr;

    QLineEdit *name_edit_ = nullptr;
    QLineEdit *id_edit_ = nullptr;
    QComboBox *provider_combo_ = nullptr;
    QCheckBox *enabled_check_ = nullptr;
    QLineEdit *location_edit_ = nullptr;
    QPushButton *browse_button_ = nullptr;
    QComboBox *refresh_mode_combo_ = nullptr;
    QSpinBox *polling_spin_ = nullptr;
    QSpinBox *rate_limit_spin_ = nullptr;
    QCheckBox *keep_last_check_ = nullptr;
    QSpinBox *stale_spin_ = nullptr;
    QLineEdit *root_path_edit_ = nullptr;
    QPlainTextEdit *headers_edit_ = nullptr;
    QLineEdit *token_edit_ = nullptr;
    QSpinBox *timeout_spin_ = nullptr;
    QSpinBox *retry_count_spin_ = nullptr;
    QSpinBox *retry_backoff_spin_ = nullptr;
    QSpinBox *reconnect_initial_spin_ = nullptr;
    QSpinBox *reconnect_max_spin_ = nullptr;
    QCheckBox *csv_headers_check_ = nullptr;
    QSpinBox *csv_row_spin_ = nullptr;
    QPlainTextEdit *csv_mapping_edit_ = nullptr;
    QLineEdit *text_field_edit_ = nullptr;
    QTableWidget *fields_table_ = nullptr;
    QPushButton *add_field_button_ = nullptr;
    QPushButton *remove_field_button_ = nullptr;
    QTableWidget *bindings_table_ = nullptr;
    std::vector<ExternalDataFormatterConfig> binding_formatter_configs_;
    std::vector<std::string> binding_legacy_formatters_;
    QPushButton *add_binding_button_ = nullptr;
    QPushButton *remove_binding_button_ = nullptr;

    QLabel *state_value_ = nullptr;
    QLabel *last_update_value_ = nullptr;
    QLabel *error_value_ = nullptr;
    QTableWidget *live_preview_table_ = nullptr;
    QPushButton *test_connection_button_ = nullptr;
    QPushButton *connect_button_ = nullptr;
    QPushButton *disconnect_button_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QTimer *status_timer_ = nullptr;
};
