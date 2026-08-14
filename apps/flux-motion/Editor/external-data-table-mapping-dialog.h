#pragma once

#include "title-data.h"

#include <QDialog>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class ExternalDataTableMappingDialog final : public QDialog {
public:
    ExternalDataTableMappingDialog(std::shared_ptr<Title> title,
                                   const LiveTextTableBinding *existing = nullptr,
                                   QWidget *parent = nullptr);
    ~ExternalDataTableMappingDialog() override = default;

    LiveTextTableBinding mapping() const { return result_; }
    bool remove_requested() const { return remove_requested_; }

private:
    void build_ui();
    void populate_sources();
    void populate_tables();
    void rebuild_column_mapping();
    void load_existing();
    void refresh_preview();
    void configure_column(int row);
    bool collect_mapping(bool report_error);
    ExternalDataTableSnapshot current_table() const;
    std::vector<ExternalDataFieldDefinition> current_table_fields() const;

    std::shared_ptr<Title> title_;
    LiveTextTableBinding initial_;
    bool has_initial_ = false;
    bool initial_applied_ = false;
    LiveTextTableBinding result_;
    bool remove_requested_ = false;

    QComboBox *source_combo_ = nullptr;
    QComboBox *table_combo_ = nullptr;
    QComboBox *mode_combo_ = nullptr;
    QComboBox *row_id_combo_ = nullptr;
    QSpinBox *start_row_spin_ = nullptr;
    QSpinBox *maximum_rows_spin_ = nullptr;
    QCheckBox *ignore_empty_check_ = nullptr;
    QCheckBox *preserve_manual_check_ = nullptr;
    QPushButton *refresh_button_ = nullptr;
    QTableWidget *mapping_table_ = nullptr;
    QTableWidget *preview_table_ = nullptr;
    QLabel *status_label_ = nullptr;
    QPushButton *remove_mapping_button_ = nullptr;
    QTimer *preview_timer_ = nullptr;
    uint64_t last_revision_ = 0;

    std::vector<ExternalPropertyBinding> column_bindings_;
    std::string column_context_source_id_;
    std::string column_context_table_path_;
};
