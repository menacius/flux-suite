#include "app/metadata-export-dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace flux {
namespace {
struct MetadataField { const char *group; const char *label; const char *key; };
constexpr MetadataField kFields[] = {
    {"Dublin Core", "Title", "title"}, {"Dublin Core", "Creator / Artist", "artist"},
    {"Dublin Core", "Description", "description"}, {"Dublin Core", "Publisher", "publisher"},
    {"Basic", "Album", "album"}, {"Basic", "Genre", "genre"}, {"Basic", "Comment", "comment"},
    {"Rights Management", "Copyright", "copyright"},
    {"Media Management", "Creation Time", "creation_time"},
    {"Basic Job Ticket", "Encoded By", "encoded_by"},
};
}

MetadataExportDialog::MetadataExportDialog(const RenderProfile &profile, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Metadata Export"));
    resize(780, 690);
    setMinimumSize(620, 520);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setObjectName(QStringLiteral("MetadataExportDialog"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    auto *top = new QFormLayout();
    exportMode_ = new QComboBox(this);
    exportMode_->addItem(tr("Embed in Output File"), QStringLiteral("embed"));
    exportMode_->addItem(tr("Do Not Export Metadata"), QStringLiteral("none"));
    exportMode_->setCurrentIndex(qMax(0, exportMode_->findData(profile.metadataMode)));
    top->addRow(tr("Export Options:"), exportMode_);
    root->addLayout(top);

    auto *sourceGroup = new QGroupBox(tr("Source Metadata"), this);
    auto *sourceLayout = new QVBoxLayout(sourceGroup);
    auto *sourceForm = new QFormLayout();
    preservation_ = new QComboBox(sourceGroup);
    preservation_->addItem(tr("Preserve All"), QStringLiteral("all"));
    preservation_->addItem(tr("Preserve Common Metadata"), QStringLiteral("common"));
    preservation_->addItem(tr("Discard Source Metadata"), QStringLiteral("none"));
    preservation_->setCurrentIndex(qMax(0, preservation_->findData(profile.metadataPreservation)));
    sourceForm->addRow(tr("Preservation Rules:"), preservation_);
    sourceLayout->addLayout(sourceForm);
    includeMarkers_ = new QCheckBox(tr("Include markers / chapters"), sourceGroup);
    includeMarkers_->setChecked(profile.metadataIncludeMarkers);
    sourceLayout->addWidget(includeMarkers_);
    root->addWidget(sourceGroup);

    auto *outputGroup = new QGroupBox(tr("Output File Metadata"), this);
    auto *outputLayout = new QVBoxLayout(outputGroup);
    auto *templateForm = new QFormLayout();
    template_ = new QComboBox(outputGroup);
    template_->addItem(tr("All Metadata"), QStringLiteral("all"));
    template_->addItem(tr("Copyright and Contact"), QStringLiteral("rights"));
    template_->addItem(tr("Basic Descriptive Metadata"), QStringLiteral("basic"));
    template_->setCurrentIndex(qMax(0, template_->findData(profile.metadataTemplate)));
    templateForm->addRow(tr("Export Template:"), template_);
    outputLayout->addLayout(templateForm);

    search_ = new QLineEdit(outputGroup);
    search_->setPlaceholderText(tr("Search metadata fields"));
    search_->setClearButtonEnabled(true);
    outputLayout->addWidget(search_);

    fields_ = new QTreeWidget(outputGroup);
    fields_->setColumnCount(2);
    fields_->setHeaderLabels({tr("Metadata Field"), tr("Value")});
    fields_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    fields_->setAlternatingRowColors(true);
    fields_->setRootIsDecorated(true);
    outputLayout->addWidget(fields_, 1);
    populateFields(profile.metadataFields);
    root->addWidget(outputGroup, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(exportMode_, &QComboBox::currentIndexChanged, this, &MetadataExportDialog::updateEnabledState);
    connect(search_, &QLineEdit::textChanged, this, &MetadataExportDialog::filterFields);
    updateEnabledState();
}

void MetadataExportDialog::populateFields(const QJsonObject &values)
{
    QMap<QString, QTreeWidgetItem *> groups;
    for (const auto &field : kFields) {
        const QString groupName = QString::fromLatin1(field.group);
        auto *group = groups.value(groupName, nullptr);
        if (!group) {
            group = new QTreeWidgetItem(fields_, {groupName});
            group->setFirstColumnSpanned(true);
            group->setExpanded(groupName == QStringLiteral("Dublin Core") || groupName == QStringLiteral("Basic"));
            groups.insert(groupName, group);
        }
        auto *item = new QTreeWidgetItem(group, {tr(field.label), values.value(QString::fromLatin1(field.key)).toString()});
        item->setData(0, Qt::UserRole, QString::fromLatin1(field.key));
        item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        item->setCheckState(0, values.contains(QString::fromLatin1(field.key)) ? Qt::Checked : Qt::Unchecked);
    }
}

QString MetadataExportDialog::metadataMode() const { return exportMode_->currentData().toString(); }
QString MetadataExportDialog::preservationRule() const { return preservation_->currentData().toString(); }
bool MetadataExportDialog::includeMarkers() const { return includeMarkers_->isChecked(); }
QString MetadataExportDialog::exportTemplate() const { return template_->currentData().toString(); }

QJsonObject MetadataExportDialog::metadataFields() const
{
    QJsonObject result;
    for (int i = 0; i < fields_->topLevelItemCount(); ++i) {
        auto *group = fields_->topLevelItem(i);
        for (int c = 0; c < group->childCount(); ++c) {
            auto *item = group->child(c);
            const QString key = item->data(0, Qt::UserRole).toString();
            const QString value = item->text(1).trimmed();
            if (item->checkState(0) == Qt::Checked && !key.isEmpty() && !value.isEmpty())
                result.insert(key, value);
        }
    }
    return result;
}

void MetadataExportDialog::updateEnabledState()
{
    const bool enabled = metadataMode() != QStringLiteral("none");
    preservation_->setEnabled(enabled);
    includeMarkers_->setEnabled(enabled);
    template_->setEnabled(enabled);
    search_->setEnabled(enabled);
    fields_->setEnabled(enabled);
}

void MetadataExportDialog::filterFields(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < fields_->topLevelItemCount(); ++i) {
        auto *group = fields_->topLevelItem(i);
        bool anyVisible = needle.isEmpty() || group->text(0).contains(needle, Qt::CaseInsensitive);
        for (int c = 0; c < group->childCount(); ++c) {
            auto *item = group->child(c);
            const bool visible = needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive)
                                 || item->text(1).contains(needle, Qt::CaseInsensitive)
                                 || group->text(0).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!visible);
            anyVisible = anyVisible || visible;
        }
        group->setHidden(!anyVisible);
    }
}

} // namespace flux
