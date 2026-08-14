#include "effects-presets-panel.h"

#include "effect-preset-catalog.h"
#include "transition-preset-catalog.h"
#include "title-assets.h"
#include "title-data.h"
#include "title-localization.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QHash>
#include <QLineEdit>
#include <QMimeData>
#include <QPalette>
#include <QSet>
#include <QScrollBar>
#include <QStyle>
#include <QTreeWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int kPresetPathRole = Qt::UserRole;
constexpr int kPresetKindRole = Qt::UserRole + 1;
constexpr int kCategoryPathRole = Qt::UserRole + 2;
constexpr int kMaxIndividuallyWatchedPresetFiles = 512;
constexpr const char *kAudioEffectMimeType = "application/x-fxm-audio-effect";
constexpr int kMaxCatalogPresetFiles = 4096;
constexpr qint64 kMaxCatalogSourceBytes = 64 * 1024 * 1024;
enum class PresetKind { None = 0, Effect = 1, Transition = 2, AudioEffect = 3 };

QString category_display_name(const QString &segment, const QString &category_key)
{
    if (segment.compare(QStringLiteral("Animation Presets"), Qt::CaseInsensitive) == 0)
        return fxm_tr("OBSTitles.AnimationPresets");
    if (segment.compare(QStringLiteral("Transitions"), Qt::CaseInsensitive) == 0)
        return fxm_tr("OBSTitles.Transitions");
    if (segment.compare(QStringLiteral("Effects"), Qt::CaseInsensitive) == 0)
        return fxm_tr("OBSTitles.Effects");
    if (segment.compare(QStringLiteral("Audio Effects"), Qt::CaseInsensitive) == 0)
        return fxm_tr("OBSTitles.AudioEffects");
    if (category_key == QStringLiteral("transitions/text"))
        return fxm_tr("OBSTitles.TextTransitions");
    if (category_key == QStringLiteral("transitions/general"))
        return fxm_tr("OBSTitles.GeneralTransitions");
    return segment;
}

class EffectPresetTreeWidget final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override
    {
        return {
            QString::fromUtf8(fxm::effects::kEffectPresetMimeType),
            QString::fromUtf8(fxm::transitions::kTransitionPresetMimeType),
            QString::fromUtf8(kAudioEffectMimeType),
        };
    }

    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.size() != 1)
            return nullptr;
        const QString file_path = items.front()->data(0, kPresetPathRole).toString();
        if (file_path.isEmpty())
            return nullptr;

        auto *mime = new QMimeData;
        const PresetKind kind = static_cast<PresetKind>(items.front()->data(0, kPresetKindRole).toInt());
        if (kind == PresetKind::Transition) {
            mime->setData(QString::fromUtf8(fxm::transitions::kTransitionPresetMimeType),
                          fxm::transitions::encode_transition_preset_mime(file_path));
        } else if (kind == PresetKind::Effect) {
            mime->setData(QString::fromUtf8(fxm::effects::kEffectPresetMimeType),
                          fxm::effects::encode_effect_preset_mime(file_path));
        } else if (kind == PresetKind::AudioEffect) {
            mime->setData(QString::fromUtf8(kAudioEffectMimeType),
                          QByteArray::number(file_path.toInt() - 1));
        } else {
            delete mime;
            return nullptr;
        }
        return mime;
    }
};

QString panel_style()
{
    const QPalette palette = qApp ? qApp->palette() : QPalette();
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    const QColor text = palette.color(QPalette::Text);
    const QColor mid = palette.color(QPalette::Mid);
    const QColor highlight = palette.color(QPalette::Highlight);
    const QColor highlighted_text = palette.color(QPalette::HighlightedText);

    return QStringLiteral(
        "QWidget#FluxMotionEffectsPresetsPanel{background:%1;color:%2;}"
        "QLineEdit{background:%3;color:%2;border:1px solid %4;border-radius:3px;padding:5px 7px;}"
        "QLineEdit:focus{border-color:%5;}"
        "QTreeWidget{background:%3;color:%2;border:1px solid %4;outline:0;}"
        "QTreeWidget::item{height:23px;padding:1px 3px;}"
        "QTreeWidget::item:selected{background:%5;color:%6;}"
        "QTreeWidget::branch{background:transparent;}")
        .arg(window.name(QColor::HexRgb), text.name(QColor::HexRgb),
             base.name(QColor::HexRgb), mid.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb), highlighted_text.name(QColor::HexRgb));
}

} // namespace

EffectsPresetsPanel::EffectsPresetsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("FluxMotionEffectsPresetsPanel"));
    setMinimumWidth(150);
    setStyleSheet(panel_style());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    search_ = new QLineEdit(this);
    search_->setClearButtonEnabled(true);
    search_->setPlaceholderText(fxm_tr("OBSTitles.SearchEffectsPresets"));
    layout->addWidget(search_);

    tree_ = new EffectPresetTreeWidget(this);
    tree_->setObjectName(QStringLiteral("FluxMotionEffectsPresetsTree"));
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);
    tree_->setAnimated(false);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setDragEnabled(true);
    tree_->setDragDropMode(QAbstractItemView::DragOnly);
    tree_->setDefaultDropAction(Qt::CopyAction);
    tree_->setIndentation(18);
    layout->addWidget(tree_, 1);

    watcher_ = new QFileSystemWatcher(this);
    reload_timer_ = new QTimer(this);
    reload_timer_->setSingleShot(true);
    reload_timer_->setInterval(120);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            [this]() { reload_timer_->start(); });
    connect(watcher_, &QFileSystemWatcher::fileChanged, this,
            [this]() { reload_timer_->start(); });
    connect(reload_timer_, &QTimer::timeout, this, &EffectsPresetsPanel::reload);

    connect(search_, &QLineEdit::textChanged, this, &EffectsPresetsPanel::apply_filter);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QString path = item->data(0, kPresetPathRole).toString();
                const PresetKind kind = static_cast<PresetKind>(item->data(0, kPresetKindRole).toInt());
                if (!path.isEmpty() && kind == PresetKind::Effect)
                    emit effect_preset_activated(path);
                else if (kind == PresetKind::AudioEffect)
                    emit audio_effect_activated(item->data(0, kPresetPathRole).toInt() - 1);
            });

    reload();
}

void EffectsPresetsPanel::reload()
{
    QString selected_preset_path;
    QSet<QString> expanded_categories;
    const bool had_existing_items = tree_ && tree_->topLevelItemCount() > 0;
    const int previous_scroll = tree_ && tree_->verticalScrollBar()
        ? tree_->verticalScrollBar()->value() : 0;
    if (tree_) {
        if (QTreeWidgetItem *selected = tree_->currentItem())
            selected_preset_path = selected->data(0, kPresetPathRole).toString();
        std::function<void(QTreeWidgetItem *)> remember_expansion =
            [&](QTreeWidgetItem *item) {
                if (!item)
                    return;
                const QString category_path = item->data(0, kCategoryPathRole).toString();
                if (!category_path.isEmpty() && item->isExpanded())
                    expanded_categories.insert(category_path);
                for (int i = 0; i < item->childCount(); ++i)
                    remember_expansion(item->child(i));
            };
        for (int i = 0; i < tree_->topLevelItemCount(); ++i)
            remember_expansion(tree_->topLevelItem(i));
        tree_->setUpdatesEnabled(false);
        tree_->clear();
    }

    const QString root_path = fxm::effects::effect_presets_root_path();
    const QStringList root_categories = {
        QStringLiteral("Transitions"),
        QStringLiteral("Effects"),
        QStringLiteral("Audio Effects"),
    };

    struct PresetEntry {
        QString file_path;
        QString display_name;
        QStringList category_path;
        PresetKind kind = PresetKind::None;
    };
    QVector<PresetEntry> entries;
    QSet<QString> seen_preset_keys;

    if (!root_path.isEmpty()) {
        const QStringList filters = {
            QStringLiteral("*") + QString::fromUtf8(fxm::effects::kEffectPresetExtension),
            QStringLiteral("*") + QString::fromUtf8(fxm::transitions::kTextTransitionExtension),
            QStringLiteral("*") + QString::fromUtf8(fxm::transitions::kGeneralTransitionExtension),
        };
        entries.reserve(std::min(256, kMaxCatalogPresetFiles));
        QDirIterator iterator(root_path, filters, QDir::Files | QDir::Readable,
                              QDirIterator::NoIteratorFlags);
        qint64 catalog_source_bytes = 0;
        while (iterator.hasNext() && entries.size() < kMaxCatalogPresetFiles) {
            iterator.next();
            const QFileInfo file = iterator.fileInfo();
            const qint64 file_size = std::max<qint64>(0, file.size());
            if (catalog_source_bytes + file_size > kMaxCatalogSourceBytes)
                break;
            catalog_source_bytes += file_size;

            const QString suffix = QStringLiteral(".") + file.suffix().toLower();
            PresetEntry entry;
            entry.file_path = file.absoluteFilePath();
            if (suffix == QString::fromUtf8(fxm::effects::kEffectPresetExtension)) {
                fxm::effects::EffectPresetDescriptor descriptor;
                if (!fxm::effects::load_effect_preset_file(entry.file_path, &descriptor))
                    continue;
                entry.display_name = descriptor.display_name;
                entry.category_path = descriptor.category_path;
                entry.kind = PresetKind::Effect;
            } else {
                fxm::transitions::TransitionPresetDescriptor descriptor;
                if (!fxm::transitions::load_transition_preset_file(entry.file_path, &descriptor))
                    continue;
                entry.display_name = descriptor.display_name;
                entry.category_path = descriptor.category_path;
                entry.kind = PresetKind::Transition;
            }
            if (!entry.category_path.isEmpty()) {
                if (entry.category_path.value(0).compare(QStringLiteral("Animation Presets"),
                                                        Qt::CaseInsensitive) == 0)
                    continue;
                for (QString &part : entry.category_path) {
                    if (part.compare(QStringLiteral("Light and Optical"), Qt::CaseInsensitive) == 0)
                        part = QStringLiteral("Light & Optical");
                    else if (part.compare(QStringLiteral("Noise & Grain"), Qt::CaseInsensitive) == 0)
                        part = QStringLiteral("Noise and Grain");
                }
                QStringList normalized_category = entry.category_path;
                for (QString &part : normalized_category) {
                    part = part.trimmed().toLower();
                    part.replace(QStringLiteral(" & "), QStringLiteral(" and "));
                }
                const QString key = QString::number(static_cast<int>(entry.kind)) + QLatin1Char('|') +
                    normalized_category.join(QLatin1Char('/')) + QLatin1Char('|') +
                    entry.display_name.trimmed().toLower();
                if (seen_preset_keys.contains(key))
                    continue;
                seen_preset_keys.insert(key);
                entries.push_back(entry);
            }
        }
    }

    // Audio DSP entries are built-ins rather than visual .fxmeffect files.
    // They are inserted below after the ordinary file-backed catalog is built.

    std::sort(entries.begin(), entries.end(), [](const PresetEntry &a, const PresetEntry &b) {
        const int category_compare = QString::compare(
            a.category_path.join(QLatin1Char('/')),
            b.category_path.join(QLatin1Char('/')), Qt::CaseInsensitive);
        if (category_compare != 0)
            return category_compare < 0;
        return QString::compare(a.display_name, b.display_name, Qt::CaseInsensitive) < 0;
    });

    QHash<QString, QTreeWidgetItem *> category_items;
    auto ensure_category = [this, &category_items](const QStringList &path) -> QTreeWidgetItem * {
        QTreeWidgetItem *parent = nullptr;
        QStringList accumulated;
        for (const QString &raw_segment : path) {
            const QString segment = raw_segment.trimmed();
            if (segment.isEmpty())
                continue;
            accumulated.push_back(segment.toLower());
            const QString key = accumulated.join(QLatin1Char('/'));
            QTreeWidgetItem *item = category_items.value(key, nullptr);
            if (!item) {
                item = parent
                    ? new QTreeWidgetItem(parent, QStringList(category_display_name(segment, key)))
                    : new QTreeWidgetItem(tree_, QStringList(category_display_name(segment, key)));
                item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                item->setData(0, kCategoryPathRole, key);
                item->setFlags((item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable) &
                               ~Qt::ItemIsDragEnabled);
                category_items.insert(key, item);
            }
            parent = item;
        }
        return parent;
    };

    for (const QString &category : root_categories) {
        QTreeWidgetItem *root_item = ensure_category({category});
        if (root_item && !had_existing_items)
            root_item->setExpanded(category == QStringLiteral("Effects"));
    }

    {
        QTreeWidgetItem *audio_root = ensure_category({QStringLiteral("Audio Effects")});
        struct BuiltinAudioEffect { const char *name_key; AudioEffectType type; };
        const BuiltinAudioEffect builtins[] = {
            {"OBSTitles.AudioEffectGain", AudioEffectType::Gain},
            {"OBSTitles.AudioEffectFade", AudioEffectType::Fade},
            {"OBSTitles.AudioEffectHighPass", AudioEffectType::HighPass},
            {"OBSTitles.AudioEffectLowPass", AudioEffectType::LowPass},
            {"OBSTitles.AudioEffectCompressorLimiter", AudioEffectType::CompressorLimiter},
        };
        for (const auto &builtin : builtins) {
            auto *item = new QTreeWidgetItem(audio_root, QStringList(fxm_tr(builtin.name_key)));
            item->setIcon(0, fxm_icon("sound.svg"));
            item->setData(0, kPresetPathRole, static_cast<int>(builtin.type) + 1);
            item->setData(0, kPresetKindRole, static_cast<int>(PresetKind::AudioEffect));
            item->setFlags(item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        }
        if (audio_root && !had_existing_items) audio_root->setExpanded(true);
    }

    for (const PresetEntry &entry : entries) {
        QTreeWidgetItem *parent = ensure_category(entry.category_path);
        if (!parent)
            continue;
        auto *preset_item = new QTreeWidgetItem(parent, QStringList(entry.display_name));
        preset_item->setIcon(0, fxm_icon(
            entry.kind == PresetKind::Transition ? "timeline-modes.svg" : "lightning.svg"));
        preset_item->setData(0, kPresetPathRole, entry.file_path);
        preset_item->setData(0, kPresetKindRole, static_cast<int>(entry.kind));
        preset_item->setToolTip(0, entry.file_path);
        preset_item->setFlags(preset_item->flags() | Qt::ItemIsEnabled |
                              Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    }

    /* Keep the library compact: hide empty categories and promote a preset
     * when it is the sole item in a subcategory. This is applied uniformly
     * to Effects, Transitions and Animation Presets. */
    std::function<void(QTreeWidgetItem *)> normalize_category_tree =
        [&](QTreeWidgetItem *parent) {
            if (!parent)
                return;
            for (int i = parent->childCount() - 1; i >= 0; --i) {
                QTreeWidgetItem *child = parent->child(i);
                const bool is_category = child &&
                    child->data(0, kPresetPathRole).toString().isEmpty();
                if (!is_category)
                    continue;
                normalize_category_tree(child);
                if (child->childCount() == 0) {
                    delete parent->takeChild(i);
                    continue;
                }
                if (child->childCount() == 1) {
                    QTreeWidgetItem *only = child->child(0);
                    const bool only_is_preset = only &&
                        !only->data(0, kPresetPathRole).toString().isEmpty();
                    if (only_is_preset) {
                        only = child->takeChild(0);
                        delete parent->takeChild(i);
                        parent->insertChild(i, only);
                    }
                }
            }
        };
    for (int i = tree_->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem *root = tree_->topLevelItem(i);
        normalize_category_tree(root);
        if (root && root->childCount() == 0)
            delete tree_->takeTopLevelItem(i);
    }

    if (watcher_) {
        const QStringList old_paths = watcher_->directories() + watcher_->files();
        if (!old_paths.isEmpty())
            watcher_->removePaths(old_paths);

        QStringList watch_paths;
        if (!root_path.isEmpty() && QDir(root_path).exists()) {
            watch_paths << root_path;
            const int watched_file_count = std::min(
                static_cast<int>(entries.size()), kMaxIndividuallyWatchedPresetFiles);
            for (int i = 0; i < watched_file_count; ++i)
                watch_paths << entries.at(i).file_path;
            watcher_->addPaths(watch_paths);
        }
    }

    std::function<void(QTreeWidgetItem *)> restore_state =
        [&](QTreeWidgetItem *item) {
            if (!item)
                return;
            const QString category_path = item->data(0, kCategoryPathRole).toString();
            if (!category_path.isEmpty() && expanded_categories.contains(category_path))
                item->setExpanded(true);
            if (!selected_preset_path.isEmpty() &&
                item->data(0, kPresetPathRole).toString() == selected_preset_path)
                tree_->setCurrentItem(item);
            for (int i = 0; i < item->childCount(); ++i)
                restore_state(item->child(i));
        };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        restore_state(tree_->topLevelItem(i));

    apply_filter(search_ ? search_->text() : QString());
    tree_->setUpdatesEnabled(true);
    if (tree_->verticalScrollBar())
        tree_->verticalScrollBar()->setValue(previous_scroll);
}

bool EffectsPresetsPanel::filter_item(QTreeWidgetItem *item, const QString &query)
{
    if (!item)
        return false;

    const bool is_preset = !item->data(0, kPresetPathRole).toString().isEmpty();
    if (is_preset) {
        const bool visible = query.isEmpty() || item->text(0).contains(query, Qt::CaseInsensitive);
        item->setHidden(!visible);
        return visible;
    }

    bool child_visible = false;
    for (int i = 0; i < item->childCount(); ++i)
        child_visible = filter_item(item->child(i), query) || child_visible;

    const bool top_level = item->parent() == nullptr;
    const bool visible = top_level ? (query.isEmpty() || child_visible) : child_visible;
    item->setHidden(!visible);
    if (!query.isEmpty() && child_visible)
        item->setExpanded(true);
    return visible;
}

void EffectsPresetsPanel::apply_filter(const QString &query)
{
    const QString normalized = query.trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        filter_item(tree_->topLevelItem(i), normalized);
}
