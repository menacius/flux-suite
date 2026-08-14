#pragma once

#include "title-data.h"

#include <QDialog>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class ExternalDataBindingDialog final : public QDialog {
public:
    ExternalDataBindingDialog(std::shared_ptr<Title> title,
                              std::string property_path,
                              ExternalDataValue authored_value,
                              const ExternalPropertyBinding *existing = nullptr,
                              QWidget *parent = nullptr,
                              std::string locked_source_id = {},
                              std::vector<ExternalDataFieldDefinition> field_options = {},
                              std::map<std::string, ExternalDataValue> preview_values = {});
    ~ExternalDataBindingDialog() override = default;

    ExternalPropertyBinding binding() const { return result_; }
    bool remove_requested() const { return remove_requested_; }

private:
    void build_ui();
    void populate_sources();
    void populate_fields();
    void load_existing();
    void update_formatter_visibility();
    void update_preview();
    bool collect_binding(bool report_error);
    ExternalDataType selected_field_type() const;

    std::shared_ptr<Title> title_;
    std::string property_path_;
    ExternalDataValue authored_value_;
    ExternalPropertyBinding initial_;
    bool has_initial_ = false;
    ExternalPropertyBinding result_;
    bool remove_requested_ = false;

    QComboBox *source_combo_ = nullptr;
    QComboBox *field_combo_ = nullptr;
    QLineEdit *fallback_edit_ = nullptr;
    QLineEdit *prefix_edit_ = nullptr;
    QLineEdit *suffix_edit_ = nullptr;
    QCheckBox *number_format_check_ = nullptr;
    QSpinBox *decimal_places_spin_ = nullptr;
    QCheckBox *thousands_check_ = nullptr;
    QComboBox *case_combo_ = nullptr;
    QLineEdit *date_format_edit_ = nullptr;
    QComboBox *empty_mode_combo_ = nullptr;
    QLineEdit *empty_replacement_edit_ = nullptr;
    QTableWidget *replacement_table_ = nullptr;
    QPushButton *add_replacement_button_ = nullptr;
    QPushButton *remove_replacement_button_ = nullptr;
    QLabel *raw_preview_ = nullptr;
    QLabel *formatted_preview_ = nullptr;
    QLabel *state_preview_ = nullptr;
    QPushButton *remove_binding_button_ = nullptr;
    QTimer *preview_timer_ = nullptr;
    uint64_t last_field_revision_ = 0;
    std::string locked_source_id_;
    std::vector<ExternalDataFieldDefinition> field_options_;
    std::map<std::string, ExternalDataValue> preview_values_;
};
