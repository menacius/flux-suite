#pragma once

#include "core/media-types.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTreeWidget;

namespace flux {

class MetadataExportDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MetadataExportDialog(const RenderProfile &profile, QWidget *parent = nullptr);

    QString metadataMode() const;
    QString preservationRule() const;
    bool includeMarkers() const;
    QString exportTemplate() const;
    QJsonObject metadataFields() const;

private:
    void populateFields(const QJsonObject &fields);
    void updateEnabledState();
    void filterFields(const QString &text);

    QComboBox *exportMode_ = nullptr;
    QComboBox *preservation_ = nullptr;
    QCheckBox *includeMarkers_ = nullptr;
    QComboBox *template_ = nullptr;
    QLineEdit *search_ = nullptr;
    QTreeWidget *fields_ = nullptr;
};

} // namespace flux
