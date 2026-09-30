#include "scene-mask-dock.h"

#include "title-data.h"
#include "title-source.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QDial>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace {

constexpr const char *kTitleSourceId = "flux_motion_source";
constexpr uint8_t kPreviewFlag = 1;
constexpr uint8_t kProgramFlag = 2;

QString scene_mask_key(const std::string &layer_id, const char *suffix)
{
    const QByteArray encoded = QByteArray(layer_id.data(),
                                          static_cast<int>(layer_id.size()))
                                   .toBase64(QByteArray::Base64UrlEncoding |
                                             QByteArray::OmitTrailingEquals);
    return QStringLiteral(PROP_SCENE_MASK_PREFIX) +
           QString::fromLatin1(encoded.constData(), encoded.size()) + QLatin1Char('_') +
           QString::fromLatin1(suffix);
}

class SceneMaskJoystick final : public QWidget {
public:
    explicit SceneMaskJoystick(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(76, 76);
        setMaximumSize(92, 92);
        setCursor(Qt::CrossCursor);
        setToolTip(QStringLiteral(
            "Drag to move the scene. Hold Shift for fine movement; double-click to reset."));
    }

    void set_position(double x, double y)
    {
        x_ = x;
        y_ = y;
        update();
    }

    std::function<void(double, double)> position_changed;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF pad = QRectF(rect()).adjusted(5.0, 5.0, -5.0, -5.0);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawRoundedRect(pad, 8.0, 8.0);

        const QPointF center = pad.center();
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0, Qt::DashLine));
        painter.drawLine(QPointF(pad.left() + 8.0, center.y()),
                         QPointF(pad.right() - 8.0, center.y()));
        painter.drawLine(QPointF(center.x(), pad.top() + 8.0),
                         QPointF(center.x(), pad.bottom() - 8.0));

        QPointF puck = center;
        if (dragging_) {
            const QRectF inner = pad.adjusted(13.0, 13.0, -13.0, -13.0);
            puck.setX(std::clamp(last_pointer_.x(), inner.left(), inner.right()));
            puck.setY(std::clamp(last_pointer_.y(), inner.top(), inner.bottom()));
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawEllipse(puck, 8.0, 8.0);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        dragging_ = true;
        press_pointer_ = event->position();
        last_pointer_ = press_pointer_;
        press_x_ = x_;
        press_y_ = y_;
        event->accept();
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!dragging_ || !(event->buttons() & Qt::LeftButton))
            return;
        last_pointer_ = event->position();
        const double sensitivity = event->modifiers().testFlag(Qt::ShiftModifier)
            ? 0.25 : 2.0;
        x_ = std::clamp(press_x_ + (last_pointer_.x() - press_pointer_.x()) * sensitivity,
                        -9999.0, 9999.0);
        y_ = std::clamp(press_y_ + (last_pointer_.y() - press_pointer_.y()) * sensitivity,
                        -9999.0, 9999.0);
        if (position_changed)
            position_changed(x_, y_);
        event->accept();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            event->accept();
            update();
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        x_ = 0.0;
        y_ = 0.0;
        if (position_changed)
            position_changed(x_, y_);
        event->accept();
        update();
    }

private:
    bool dragging_ = false;
    QPointF press_pointer_;
    QPointF last_pointer_;
    double press_x_ = 0.0;
    double press_y_ = 0.0;
    double x_ = 0.0;
    double y_ = 0.0;
};

struct VisibleSourceCollection {
    std::map<QString, uint8_t> sources;
    std::set<obs_source_t *> visited_scenes;
    uint8_t flag = 0;
};

bool collect_scene_item(obs_scene_t *, obs_sceneitem_t *item, void *opaque)
{
    auto *collection = static_cast<VisibleSourceCollection *>(opaque);
    if (!collection || !item || !obs_sceneitem_visible(item))
        return true;

    obs_source_t *source = obs_sceneitem_get_source(item);
    if (!source)
        return true;

    const char *id = obs_source_get_id(source);
    if (id && std::strcmp(id, kTitleSourceId) == 0) {
        const char *name = obs_source_get_name(source);
        if (name && *name)
            collection->sources[QString::fromUtf8(name)] |= collection->flag;
    }

    if (obs_sceneitem_is_group(item)) {
        obs_sceneitem_group_enum_items(item, collect_scene_item, opaque);
        return true;
    }

    obs_scene_t *nested = obs_scene_from_source(source);
    if (nested && collection->visited_scenes.insert(source).second)
        obs_scene_enum_items(nested, collect_scene_item, opaque);
    return true;
}

void collect_scene_sources(obs_source_t *scene_source, uint8_t flag,
                           std::map<QString, uint8_t> &sources)
{
    if (!scene_source)
        return;
    obs_scene_t *scene = obs_scene_from_source(scene_source);
    if (!scene)
        return;
    VisibleSourceCollection collection;
    collection.sources = sources;
    collection.flag = flag;
    collection.visited_scenes.insert(scene_source);
    obs_scene_enum_items(scene, collect_scene_item, &collection);
    sources = std::move(collection.sources);
}

bool collect_active_source(void *opaque, obs_source_t *source)
{
    auto *sources = static_cast<std::map<QString, uint8_t> *>(opaque);
    if (!sources || !source || !obs_source_active(source))
        return true;
    const char *id = obs_source_get_id(source);
    const char *name = obs_source_get_name(source);
    if (id && name && *name && std::strcmp(id, kTitleSourceId) == 0)
        (*sources)[QString::fromUtf8(name)] |= kProgramFlag;
    return true;
}

std::shared_ptr<Title> resolve_title(obs_data_t *settings)
{
    if (!settings)
        return {};
    const std::string title_id = obs_data_get_string(settings, PROP_TITLE_ID);
    if (title_id.empty())
        return {};
    const std::string project_path =
        obs_data_get_string(settings, PROP_PROJECT_PATH);
    if (project_path.empty())
        return TitleDataStore::instance().get_title_snapshot(title_id);

    struct CachedProject {
        qint64 modified_msecs = -1;
        TitleProject project;
    };
    static std::map<std::string, CachedProject> cache;
    const QFileInfo info(QString::fromStdString(project_path));
    const qint64 modified_msecs = info.exists()
        ? info.lastModified().toMSecsSinceEpoch() : -1;
    auto cache_it = cache.find(project_path);
    if (cache_it == cache.end() ||
        cache_it->second.modified_msecs != modified_msecs) {
        TitleProject project;
        if (!TitleDataStore::instance().read_project(project_path, &project))
            return {};
        cache_it = cache.insert_or_assign(
            project_path, CachedProject{modified_msecs, std::move(project)}).first;
    }
    const TitleProject &project = cache_it->second.project;
    const auto it = std::find_if(
        project.titles.begin(), project.titles.end(),
        [&title_id](const std::shared_ptr<Title> &title) {
            return title && title->id == title_id;
        });
    return it == project.titles.end() ? std::shared_ptr<Title>{} : *it;
}

struct SourceTransformValues {
    double x = 0.0;
    double y = 0.0;
    double zoom = 100.0;
    bool valid = false;
};

SourceTransformValues update_source_transform(const QString &source_name,
                                              const std::string &layer_id,
                                              double x, double y, double zoom)
{
    SourceTransformValues result;
    obs_source_t *source = obs_get_source_by_name(source_name.toUtf8().constData());
    if (!source)
        return result;
    const char *id = obs_source_get_id(source);
    if (!id || std::strcmp(id, kTitleSourceId) != 0) {
        obs_source_release(source);
        return result;
    }
    obs_data_t *settings = obs_source_get_settings(source);
    const QString x_key = scene_mask_key(layer_id, "x");
    const QString y_key = scene_mask_key(layer_id, "y");
    const QString zoom_key = scene_mask_key(layer_id, "zoom_percent");
    obs_data_set_double(settings, x_key.toUtf8().constData(), x);
    obs_data_set_double(settings, y_key.toUtf8().constData(), y);
    obs_data_set_double(settings, zoom_key.toUtf8().constData(), zoom);
    obs_source_update(source, settings);
    obs_data_release(settings);

    settings = obs_source_get_settings(source);
    result.x = obs_data_get_double(settings, x_key.toUtf8().constData());
    result.y = obs_data_get_double(settings, y_key.toUtf8().constData());
    const double stored_zoom =
        obs_data_get_double(settings, zoom_key.toUtf8().constData());
    result.zoom = std::clamp(stored_zoom > 0.0 ? stored_zoom : 100.0,
                             1.0, 800.0);
    result.valid = true;
    obs_data_release(settings);
    obs_source_release(source);
    return result;
}

void update_source_bool_setting(const QString &source_name,
                                const QString &key, bool value)
{
    obs_source_t *source = obs_get_source_by_name(source_name.toUtf8().constData());
    if (!source)
        return;
    const char *id = obs_source_get_id(source);
    if (!id || std::strcmp(id, kTitleSourceId) != 0) {
        obs_source_release(source);
        return;
    }
    obs_data_t *settings = obs_source_get_settings(source);
    obs_data_set_bool(settings, key.toUtf8().constData(), value);
    obs_source_update(source, settings);
    obs_data_release(settings);
    obs_source_release(source);
}

void update_source_int_setting(const QString &source_name,
                               const QString &key, long long value)
{
    obs_source_t *source = obs_get_source_by_name(source_name.toUtf8().constData());
    if (!source)
        return;
    const char *id = obs_source_get_id(source);
    if (!id || std::strcmp(id, kTitleSourceId) != 0) {
        obs_source_release(source);
        return;
    }
    obs_data_t *settings = obs_source_get_settings(source);
    obs_data_set_int(settings, key.toUtf8().constData(), value);
    obs_source_update(source, settings);
    obs_data_release(settings);
    obs_source_release(source);
}

QString position_text(double x, double y)
{
    return QStringLiteral("X %1   Y %2")
        .arg(x, 0, 'f', 1)
        .arg(y, 0, 'f', 1);
}

QString status_text(bool preview, bool program)
{
    if (preview && program)
        return QStringLiteral("MONITOR · also in Preview");
    if (program)
        return QStringLiteral("MONITOR");
    return QStringLiteral("PREVIEW ONLY");
}

} // namespace

void open_scene_mask_source_controls(obs_source_t *source,
                                     const char *layer_id_text)
{
    if (!source || !layer_id_text || !*layer_id_text)
        return;
    const char *source_id = obs_source_get_id(source);
    if (!source_id || std::strcmp(source_id, kTitleSourceId) != 0)
        return;

    const std::string layer_id(layer_id_text);
    const QString source_name = QString::fromUtf8(obs_source_get_name(source));
    obs_data_t *settings = obs_source_get_settings(source);
    if (!settings)
        return;
    const auto title = resolve_title(settings);
    QString layer_name = QString::fromStdString(layer_id);
    if (title) {
        const auto found = std::find_if(
            title->layers.begin(), title->layers.end(),
            [&layer_id](const std::shared_ptr<Layer> &layer) {
                return layer && layer->id == layer_id;
            });
        if (found != title->layers.end() && *found && !(*found)->name.empty())
            layer_name = QString::fromStdString((*found)->name);
    }
    const QString x_key = scene_mask_key(layer_id, "x");
    const QString y_key = scene_mask_key(layer_id, "y");
    const QString zoom_key = scene_mask_key(layer_id, "zoom_percent");
    const double initial_x = obs_data_get_double(settings, x_key.toUtf8().constData());
    const double initial_y = obs_data_get_double(settings, y_key.toUtf8().constData());
    const double stored_zoom =
        obs_data_get_double(settings, zoom_key.toUtf8().constData());
    const double initial_zoom = std::clamp(
        stored_zoom > 0.0 ? stored_zoom : 100.0, 1.0, 800.0);
    obs_data_release(settings);

    auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Scene Mask Controls — %1")
                              .arg(layer_name));
    dialog.setModal(true);
    dialog.setMinimumWidth(390);

    auto *outer = new QVBoxLayout(&dialog);
    auto *heading = new QLabel(
        QStringLiteral("%1 — %2").arg(source_name, layer_name), &dialog);
    QFont heading_font = heading->font();
    heading_font.setBold(true);
    heading->setFont(heading_font);
    outer->addWidget(heading);

    auto *controls = new QHBoxLayout();
    auto *position_column = new QVBoxLayout();
    auto *position_label = new QLabel(QStringLiteral("POSITION"), &dialog);
    position_label->setAlignment(Qt::AlignCenter);
    auto *joystick = new SceneMaskJoystick(&dialog);
    auto *center = new QPushButton(QStringLiteral("Center"), &dialog);
    position_column->addWidget(position_label);
    position_column->addWidget(joystick, 0, Qt::AlignCenter);
    position_column->addWidget(center);

    auto *zoom_column = new QVBoxLayout();
    auto *zoom_label = new QLabel(QStringLiteral("ZOOM"), &dialog);
    zoom_label->setAlignment(Qt::AlignCenter);
    auto *dial = new QDial(&dialog);
    dial->setRange(1, 800);
    dial->setNotchesVisible(true);
    dial->setFixedSize(92, 92);
    auto *wide_tele = new QHBoxLayout();
    wide_tele->addWidget(new QLabel(QStringLiteral("W"), &dialog));
    wide_tele->addStretch(1);
    wide_tele->addWidget(new QLabel(QStringLiteral("T"), &dialog));
    auto *reset_zoom = new QPushButton(QStringLiteral("Reset 100%"), &dialog);
    zoom_column->addWidget(zoom_label);
    zoom_column->addWidget(dial, 0, Qt::AlignCenter);
    zoom_column->addLayout(wide_tele);
    zoom_column->addWidget(reset_zoom);
    controls->addLayout(position_column, 1);
    controls->addLayout(zoom_column, 1);
    outer->addLayout(controls);

    auto *numeric = new QGridLayout();
    auto make_spin = [&dialog](double minimum, double maximum,
                               const QString &suffix = {}) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setRange(minimum, maximum);
        spin->setDecimals(1);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
        spin->setSuffix(suffix);
        return spin;
    };
    auto *x_spin = make_spin(-9999.0, 9999.0);
    auto *y_spin = make_spin(-9999.0, 9999.0);
    auto *zoom_spin = make_spin(1.0, 800.0, QStringLiteral(" %"));
    numeric->addWidget(new QLabel(QStringLiteral("X"), &dialog), 0, 0);
    numeric->addWidget(x_spin, 0, 1);
    numeric->addWidget(new QLabel(QStringLiteral("Y"), &dialog), 0, 2);
    numeric->addWidget(y_spin, 0, 3);
    numeric->addWidget(new QLabel(QStringLiteral("Zoom"), &dialog), 1, 0);
    numeric->addWidget(zoom_spin, 1, 1, 1, 3);
    outer->addLayout(numeric);

    joystick->set_position(initial_x, initial_y);
    x_spin->setValue(initial_x);
    y_spin->setValue(initial_y);
    zoom_spin->setValue(initial_zoom);
    dial->setValue(static_cast<int>(std::lround(initial_zoom)));

    auto sync_transform_controls = [=](const SourceTransformValues &values) {
        if (!values.valid)
            return;
        const QSignalBlocker block_x(x_spin);
        const QSignalBlocker block_y(y_spin);
        const QSignalBlocker block_zoom(zoom_spin);
        const QSignalBlocker block_dial(dial);
        x_spin->setValue(values.x);
        y_spin->setValue(values.y);
        zoom_spin->setValue(values.zoom);
        dial->setValue(static_cast<int>(std::lround(values.zoom)));
        joystick->set_position(values.x, values.y);
    };
    auto apply_transform = [=]() {
        const SourceTransformValues effective = update_source_transform(
            source_name, layer_id, x_spin->value(), y_spin->value(),
            zoom_spin->value());
        sync_transform_controls(effective);
    };
    joystick->position_changed = [=](double x, double y) {
        {
            const QSignalBlocker block_x(x_spin);
            const QSignalBlocker block_y(y_spin);
            x_spin->setValue(x);
            y_spin->setValue(y);
        }
        apply_transform();
    };
    QObject::connect(x_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     &dialog, [=](double) {
                         joystick->set_position(x_spin->value(), y_spin->value());
                         apply_transform();
                     });
    QObject::connect(y_spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                     &dialog, [=](double) {
                         joystick->set_position(x_spin->value(), y_spin->value());
                         apply_transform();
                     });
    QObject::connect(dial, &QDial::valueChanged, &dialog, [=](int value) {
        const QSignalBlocker blocker(zoom_spin);
        zoom_spin->setValue(static_cast<double>(value));
        apply_transform();
    });
    QObject::connect(
        zoom_spin, qOverload<double>(&QDoubleSpinBox::valueChanged), &dialog,
        [=](double value) {
            {
                const QSignalBlocker blocker(dial);
                dial->setValue(static_cast<int>(std::lround(value)));
            }
            apply_transform();
        });
    QObject::connect(center, &QPushButton::clicked, &dialog, [=]() {
        {
            const QSignalBlocker block_x(x_spin);
            const QSignalBlocker block_y(y_spin);
            x_spin->setValue(0.0);
            y_spin->setValue(0.0);
        }
        apply_transform();
    });
    QObject::connect(reset_zoom, &QPushButton::clicked, &dialog, [=]() {
        zoom_spin->setValue(100.0);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::accept);
    outer->addWidget(buttons);
    dialog.exec();
}

struct SceneMaskDock::MaskEntry {
    QString source_name;
    QString title_name;
    QString layer_id;
    QString layer_name;
    QString target_scene;
    double x = 0.0;
    double y = 0.0;
    double zoom = 100.0;
    bool smooth_motion = false;
    int smoothness = 35;
    bool preview = false;
    bool program = false;

    QString identity() const
    {
        return (program ? QStringLiteral("M") : QStringLiteral("P")) +
               QChar(0x1f) + source_name + QChar(0x1f) + layer_id;
    }
};

struct SceneMaskDock::MaskCard {
    QString identity;
    QWidget *root = nullptr;
    QLabel *status = nullptr;
    QLabel *target = nullptr;
    QLabel *position = nullptr;
    QLabel *zoom_value = nullptr;
    SceneMaskJoystick *joystick = nullptr;
    QDial *zoom = nullptr;
    QCheckBox *smooth = nullptr;
    QSlider *smoothness = nullptr;
    QLabel *smoothness_value = nullptr;
    QTimer *motion_timer = nullptr;
    QString source_name;
    std::string layer_id;
    double current_x = 0.0;
    double current_y = 0.0;
    double current_zoom = 100.0;
    double target_x = 0.0;
    double target_y = 0.0;
    double target_zoom = 100.0;
    int smoothness_amount = 35;
};

static void publish_card_transform(SceneMaskDock::MaskCard *card)
{
    if (!card)
        return;
    card->current_x = std::clamp(card->current_x, -9999.0, 9999.0);
    card->current_y = std::clamp(card->current_y, -9999.0, 9999.0);
    card->current_zoom = std::clamp(card->current_zoom, 1.0, 800.0);
    const double requested_x = card->current_x;
    const double requested_y = card->current_y;
    const double requested_zoom = card->current_zoom;
    const SourceTransformValues effective = update_source_transform(
        card->source_name, card->layer_id, requested_x, requested_y,
        requested_zoom);
    if (effective.valid) {
        card->current_x = effective.x;
        card->current_y = effective.y;
        card->current_zoom = effective.zoom;
        if (std::abs(effective.x - requested_x) > 0.000001 ||
            std::abs(effective.y - requested_y) > 0.000001 ||
            std::abs(effective.zoom - requested_zoom) > 0.000001) {
            card->target_x = effective.x;
            card->target_y = effective.y;
            card->target_zoom = effective.zoom;
            if (card->motion_timer)
                card->motion_timer->stop();
        }
    }
    card->position->setText(position_text(card->current_x, card->current_y));
    card->zoom_value->setText(
        QStringLiteral("%1%").arg(card->current_zoom, 0, 'f', 0));
    card->joystick->set_position(card->current_x, card->current_y);
    if (!card->zoom->isSliderDown()) {
        const QSignalBlocker blocker(card->zoom);
        card->zoom->setValue(static_cast<int>(std::lround(card->current_zoom)));
    }
}

static void finish_card_motion(SceneMaskDock::MaskCard *card)
{
    if (!card)
        return;
    card->current_x = card->target_x;
    card->current_y = card->target_y;
    card->current_zoom = card->target_zoom;
    if (card->motion_timer)
        card->motion_timer->stop();
    publish_card_transform(card);
}

static void set_card_motion_target(SceneMaskDock::MaskCard *card,
                                   double x, double y, double zoom)
{
    if (!card)
        return;
    card->target_x = std::clamp(x, -9999.0, 9999.0);
    card->target_y = std::clamp(y, -9999.0, 9999.0);
    card->target_zoom = std::clamp(zoom, 1.0, 800.0);
    if (!card->smooth || !card->smooth->isChecked()) {
        finish_card_motion(card);
        return;
    }
    if (card->motion_timer && !card->motion_timer->isActive())
        card->motion_timer->start();
}

static void advance_card_motion(SceneMaskDock::MaskCard *card)
{
    if (!card)
        return;
    /* Exponential interpolation is stable with uneven UI cadence. Higher
     * smoothness values lengthen the response while preserving an exact final
     * target for joystick, dial, Center and Reset operations alike. */
    const double normalized =
        static_cast<double>(std::clamp(card->smoothness_amount, 1, 100)) /
        100.0;
    const double tau_ms = 20.0 + 480.0 * normalized * normalized;
    const double alpha = 1.0 - std::exp(-16.0 / tau_ms);
    card->current_x += (card->target_x - card->current_x) * alpha;
    card->current_y += (card->target_y - card->current_y) * alpha;
    card->current_zoom += (card->target_zoom - card->current_zoom) * alpha;
    const bool settled =
        std::abs(card->target_x - card->current_x) < 0.05 &&
        std::abs(card->target_y - card->current_y) < 0.05 &&
        std::abs(card->target_zoom - card->current_zoom) < 0.05;
    if (settled) {
        finish_card_motion(card);
        return;
    }
    publish_card_transform(card);
}

static std::vector<SceneMaskDock::MaskEntry> collect_mask_entries()
{
    std::map<QString, uint8_t> visible_sources;
    obs_enum_sources(collect_active_source, &visible_sources);
    obs_source_t *program_scene = obs_frontend_get_current_scene();
    collect_scene_sources(program_scene, kProgramFlag, visible_sources);
    if (program_scene)
        obs_source_release(program_scene);

    if (obs_frontend_preview_program_mode_active()) {
        obs_source_t *preview_scene = obs_frontend_get_current_preview_scene();
        collect_scene_sources(preview_scene, kPreviewFlag, visible_sources);
        if (preview_scene)
            obs_source_release(preview_scene);
    }

    std::vector<SceneMaskDock::MaskEntry> entries;
    for (const auto &[source_name, flags] : visible_sources) {
        obs_source_t *source =
            obs_get_source_by_name(source_name.toUtf8().constData());
        if (!source)
            continue;
        obs_data_t *settings = obs_source_get_settings(source);
        const auto title = resolve_title(settings);
        if (title) {
            for (const auto &layer : title->layers) {
                if (!layer || !layer->use_as_scene_mask ||
                    !layer_type_can_be_scene_mask(layer->type))
                    continue;
                SceneMaskDock::MaskEntry entry;
                entry.source_name = source_name;
                entry.title_name = QString::fromStdString(title->name);
                entry.layer_id = QString::fromStdString(layer->id);
                entry.layer_name = QString::fromStdString(
                    layer->name.empty() ? layer->id : layer->name);
                const QString scene_key = scene_mask_key(layer->id, "scene");
                const QString x_key = scene_mask_key(layer->id, "x");
                const QString y_key = scene_mask_key(layer->id, "y");
                const QString zoom_key = scene_mask_key(layer->id, "zoom_percent");
                const QString smooth_key = scene_mask_key(layer->id, "smooth_motion");
                const QString smoothness_key = scene_mask_key(layer->id, "smoothness");
                entry.target_scene = QString::fromUtf8(
                    obs_data_get_string(settings, scene_key.toUtf8().constData()));
                entry.x = obs_data_get_double(settings, x_key.toUtf8().constData());
                entry.y = obs_data_get_double(settings, y_key.toUtf8().constData());
                const double stored_zoom =
                    obs_data_get_double(settings, zoom_key.toUtf8().constData());
                entry.zoom = std::clamp(stored_zoom > 0.0 ? stored_zoom : 100.0,
                                        1.0, 800.0);
                entry.smooth_motion = obs_data_get_bool(
                    settings, smooth_key.toUtf8().constData());
                entry.smoothness = std::clamp(
                    static_cast<int>(obs_data_get_int(
                        settings, smoothness_key.toUtf8().constData())),
                    1, 100);
                entry.preview = (flags & kPreviewFlag) != 0;
                entry.program = (flags & kProgramFlag) != 0;
                entries.push_back(std::move(entry));
            }
        }
        obs_data_release(settings);
        obs_source_release(source);
    }
    std::sort(entries.begin(), entries.end(),
              [](const SceneMaskDock::MaskEntry &a,
                 const SceneMaskDock::MaskEntry &b) {
                  if (a.program != b.program)
                      return a.program > b.program;
                  return a.identity() < b.identity();
              });
    return entries;
}

SceneMaskDock::SceneMaskDock(QWidget *parent) : QDockWidget(parent)
{
    setObjectName(QStringLiteral("FluxMotionSceneMasksDock"));
    setWindowTitle(QStringLiteral("Flux Motion — Scene Masks"));
    setAllowedAreas(Qt::AllDockWidgetAreas);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    container_ = new QWidget(scroll_);
    cards_layout_ = new QVBoxLayout(container_);
    cards_layout_->setContentsMargins(6, 6, 6, 6);
    cards_layout_->setSpacing(5);
    empty_label_ = new QLabel(
        QStringLiteral("No Scene Masks are visible in Preview or Monitor."),
        container_);
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setWordWrap(true);
    cards_layout_->addWidget(empty_label_);
    cards_layout_->addStretch(1);
    scroll_->setWidget(container_);
    setWidget(scroll_);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(500);
    connect(refresh_timer_, &QTimer::timeout, this, [this]() { refresh(); });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            refresh();
            refresh_timer_->start();
        } else {
            refresh_timer_->stop();
        }
    });
    refresh_timer_->start();
    QTimer::singleShot(0, this, [this]() { refresh(); });
}

SceneMaskDock::~SceneMaskDock() = default;

void SceneMaskDock::refresh()
{
    const auto entries = collect_mask_entries();
    bool same_identity = entries.size() == cards_.size();
    if (same_identity) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].identity() != cards_[i]->identity) {
                same_identity = false;
                break;
            }
        }
    }
    if (!same_identity)
        rebuild_cards(entries);
    else
        update_cards(entries);
}

void SceneMaskDock::rebuild_cards(const std::vector<MaskEntry> &entries)
{
    while (QLayoutItem *item = cards_layout_->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        delete item;
    }
    cards_.clear();

    empty_label_ = new QLabel(
        QStringLiteral("No Scene Masks are visible in Preview or Monitor."),
        container_);
    empty_label_->setAlignment(Qt::AlignCenter);
    empty_label_->setWordWrap(true);
    empty_label_->setVisible(entries.empty());
    cards_layout_->addWidget(empty_label_);

    QString current_section;
    for (const auto &entry : entries) {
        const QString section = entry.program
            ? QStringLiteral("MONITOR") : QStringLiteral("PREVIEW");
        if (section != current_section) {
            current_section = section;
            auto *section_header = new QLabel(section, container_);
            QFont section_font = section_header->font();
            section_font.setBold(true);
            section_header->setFont(section_font);
            section_header->setStyleSheet(QStringLiteral(
                "padding: 4px 6px; border-bottom: 2px solid palette(highlight);"));
            cards_layout_->addWidget(section_header);
        }
        auto card = std::make_unique<MaskCard>();
        card->identity = entry.identity();
        card->source_name = entry.source_name;
        card->layer_id = entry.layer_id.toStdString();
        card->current_x = card->target_x = entry.x;
        card->current_y = card->target_y = entry.y;
        card->current_zoom = card->target_zoom = entry.zoom;
        card->smoothness_amount = entry.smoothness;
        card->root = new QFrame(container_);
        card->root->setObjectName(QStringLiteral("sceneMaskControlCard"));
        card->root->setStyleSheet(QStringLiteral(
            "QFrame#sceneMaskControlCard { border: 1px solid palette(mid); "
            "border-radius: 6px; }"));
        auto *outer = new QVBoxLayout(card->root);
        outer->setContentsMargins(6, 5, 6, 6);
        outer->setSpacing(3);

        auto *heading = new QLabel(
            QStringLiteral("%1 — %2").arg(entry.source_name, entry.layer_name),
            card->root);
        QFont heading_font = heading->font();
        heading_font.setBold(true);
        heading->setFont(heading_font);
        outer->addWidget(heading);

        card->status = new QLabel(card->root);
        card->target = new QLabel(card->root);
        card->target->setWordWrap(true);
        auto *meta = new QHBoxLayout();
        meta->setSpacing(6);
        meta->addWidget(card->status);
        meta->addStretch(1);
        meta->addWidget(card->target);
        outer->addLayout(meta);

        auto *controls = new QHBoxLayout();
        controls->setSpacing(8);
        auto *position_box = new QVBoxLayout();
        auto *position_caption = new QLabel(QStringLiteral("POSITION"), card->root);
        position_caption->setAlignment(Qt::AlignCenter);
        card->joystick = new SceneMaskJoystick(card->root);
        card->position = new QLabel(card->root);
        card->position->setAlignment(Qt::AlignCenter);
        auto *reset = new QPushButton(QStringLiteral("Center"), card->root);
        reset->setMaximumHeight(24);
        position_box->addWidget(position_caption);
        position_box->addWidget(card->joystick, 0, Qt::AlignCenter);
        position_box->addWidget(card->position);
        position_box->addWidget(reset);

        auto *zoom_box = new QVBoxLayout();
        auto *zoom_caption = new QLabel(QStringLiteral("ZOOM"), card->root);
        zoom_caption->setAlignment(Qt::AlignCenter);
        card->zoom = new QDial(card->root);
        card->zoom->setRange(1, 800);
        card->zoom->setNotchesVisible(true);
        card->zoom->setWrapping(false);
        card->zoom->setFixedSize(74, 74);
        auto *wide_tele = new QHBoxLayout();
        wide_tele->addWidget(new QLabel(QStringLiteral("W"), card->root));
        wide_tele->addStretch(1);
        wide_tele->addWidget(new QLabel(QStringLiteral("T"), card->root));
        card->zoom_value = new QLabel(card->root);
        card->zoom_value->setAlignment(Qt::AlignCenter);
        auto *zoom_reset = new QPushButton(QStringLiteral("Reset 100%"), card->root);
        zoom_reset->setMaximumHeight(24);
        zoom_box->addWidget(zoom_caption);
        zoom_box->addWidget(card->zoom, 0, Qt::AlignCenter);
        zoom_box->addLayout(wide_tele);
        zoom_box->addWidget(card->zoom_value);
        zoom_box->addWidget(zoom_reset);

        controls->addLayout(position_box, 1);
        controls->addLayout(zoom_box, 1);
        outer->addLayout(controls);

        card->smooth = new QCheckBox(QStringLiteral("Smooth motion"), card->root);
        card->smooth->setChecked(entry.smooth_motion);
        card->smooth->setToolTip(QStringLiteral(
            "Ease position and zoom changes, including Center and Reset."));
        card->smoothness = new QSlider(Qt::Horizontal, card->root);
        card->smoothness->setRange(1, 100);
        card->smoothness->setValue(entry.smoothness);
        card->smoothness->setToolTip(QStringLiteral(
            "Higher values produce slower, smoother motion."));
        card->smoothness_value = new QLabel(
            QStringLiteral("%1%").arg(entry.smoothness), card->root);
        card->smoothness_value->setMinimumWidth(34);
        card->smoothness_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *smooth_row = new QHBoxLayout();
        smooth_row->setSpacing(5);
        smooth_row->addWidget(card->smooth);
        smooth_row->addWidget(card->smoothness, 1);
        smooth_row->addWidget(card->smoothness_value);
        outer->addLayout(smooth_row);

        card->motion_timer = new QTimer(card->root);
        card->motion_timer->setInterval(16);

        MaskCard *card_ptr = card.get();
        card->joystick->position_changed =
            [card_ptr](double x, double y) {
                set_card_motion_target(card_ptr, x, y, card_ptr->target_zoom);
            };
        connect(reset, &QPushButton::clicked, card->root,
                [card_ptr]() {
                    set_card_motion_target(card_ptr, 0.0, 0.0,
                                           card_ptr->target_zoom);
                });
        connect(card->zoom, &QDial::valueChanged, card->root,
                [card_ptr](int value) {
                    set_card_motion_target(card_ptr, card_ptr->target_x,
                                           card_ptr->target_y,
                                           static_cast<double>(value));
                });
        connect(zoom_reset, &QPushButton::clicked, card->root,
                [card_ptr]() {
                    set_card_motion_target(card_ptr, card_ptr->target_x,
                                           card_ptr->target_y, 100.0);
                });
        connect(card->smooth, &QCheckBox::toggled, card->root,
                [card_ptr](bool enabled) {
                    update_source_bool_setting(
                        card_ptr->source_name,
                        scene_mask_key(card_ptr->layer_id, "smooth_motion"),
                        enabled);
                    if (!enabled && card_ptr->motion_timer &&
                        card_ptr->motion_timer->isActive())
                        finish_card_motion(card_ptr);
                });
        connect(card->smoothness, &QSlider::valueChanged, card->root,
                [card_ptr](int value) {
                    card_ptr->smoothness_amount = std::clamp(value, 1, 100);
                    card_ptr->smoothness_value->setText(
                        QStringLiteral("%1%").arg(card_ptr->smoothness_amount));
                    update_source_int_setting(
                        card_ptr->source_name,
                        scene_mask_key(card_ptr->layer_id, "smoothness"),
                        card_ptr->smoothness_amount);
                });
        connect(card->motion_timer, &QTimer::timeout, card->root,
                [card_ptr]() { advance_card_motion(card_ptr); });

        cards_layout_->addWidget(card->root);
        cards_.push_back(std::move(card));
    }
    cards_layout_->addStretch(1);
    update_cards(entries);
}

void SceneMaskDock::update_cards(const std::vector<MaskEntry> &entries)
{
    if (empty_label_)
        empty_label_->setVisible(entries.empty());
    for (std::size_t i = 0; i < entries.size() && i < cards_.size(); ++i) {
        const auto &entry = entries[i];
        auto &card = *cards_[i];
        card.status->setText(status_text(entry.preview, entry.program));
        card.target->setText(entry.target_scene.isEmpty()
            ? QStringLiteral("Target scene: Not assigned")
            : QStringLiteral("Target scene: %1").arg(entry.target_scene));
        {
            const QSignalBlocker blocker(card.smooth);
            card.smooth->setChecked(entry.smooth_motion);
        }
        {
            const QSignalBlocker blocker(card.smoothness);
            card.smoothness_amount = entry.smoothness;
            card.smoothness->setValue(entry.smoothness);
            card.smoothness_value->setText(
                QStringLiteral("%1%").arg(entry.smoothness));
        }
        if (!card.motion_timer->isActive()) {
            card.current_x = card.target_x = entry.x;
            card.current_y = card.target_y = entry.y;
            card.current_zoom = card.target_zoom = entry.zoom;
            card.position->setText(position_text(entry.x, entry.y));
            card.joystick->set_position(entry.x, entry.y);
            if (!card.zoom->isSliderDown()) {
                const QSignalBlocker blocker(card.zoom);
                card.zoom->setValue(static_cast<int>(std::lround(entry.zoom)));
            }
            card.zoom_value->setText(
                QStringLiteral("%1%").arg(entry.zoom, 0, 'f', 0));
        }
    }
}

