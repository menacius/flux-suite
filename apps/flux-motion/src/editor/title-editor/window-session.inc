namespace {

static QString fxm_editor_caption(const QString &base)
{
    return base + QStringLiteral(" — ") + QStringLiteral(FXM_BUILD_DISPLAY);
}
double editor_playback_ui_frame_duration()
{
    /* Playback follows the project/OBS cadence, including project rates above
     * 60 fps. The 1 ms floor only protects QTimer from an invalid zero value. */
    return std::max(1.0 / 1000.0, obs_frame_duration());
}

int editor_playback_ui_timer_interval_ms()
{
    return std::max(1, static_cast<int>(std::floor(
        editor_playback_ui_frame_duration() * 1000.0)));
}

QString editor_recent_color_hex(const QColor &color)
{
    if (!color.isValid())
        return QString();
    if (color.alpha() < 255) {
        return QStringLiteral("#%1%2%3%4")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'))
            .arg(color.alpha(), 2, 16, QLatin1Char('0'))
            .toUpper();
    }
    return color.name(QColor::HexRgb).toUpper();
}

bool editor_parse_recent_color_hex(const QString &hex, QColor &color)
{
    QString s = hex.trimmed();
    if (s.startsWith(QLatin1Char('#')))
        s.remove(0, 1);

    bool ok = false;
    if (s.size() == 6) {
        const int r = s.mid(0, 2).toInt(&ok, 16);
        if (!ok) return false;
        const int g = s.mid(2, 2).toInt(&ok, 16);
        if (!ok) return false;
        const int b = s.mid(4, 2).toInt(&ok, 16);
        if (!ok) return false;
        color = QColor(r, g, b, 255);
        return color.isValid();
    }

    if (s.size() == 8) {
        const int r = s.mid(0, 2).toInt(&ok, 16);
        if (!ok) return false;
        const int g = s.mid(2, 2).toInt(&ok, 16);
        if (!ok) return false;
        const int b = s.mid(4, 2).toInt(&ok, 16);
        if (!ok) return false;
        const int a = s.mid(6, 2).toInt(&ok, 16);
        if (!ok) return false;
        color = QColor(r, g, b, a);
        return color.isValid();
    }

    return false;
}

QString editor_color_swatch_style(const QColor &color, bool enabled = true)
{
    const QPalette pal = qApp->palette();
    const QString border = pal.color(QPalette::Mid).name(QColor::HexRgb);
    const QString highlight = pal.color(QPalette::Highlight).name(QColor::HexRgb);
    if (!enabled || !color.isValid()) {
        const QString bg = pal.color(QPalette::Window).name(QColor::HexRgb);
        return QStringLiteral("QToolButton{background:%1;border:1px dashed %2;border-radius:3px;}"
                              "QToolButton:hover{border:1px dashed %2;}")
            .arg(bg, border);
    }

    const QString bg = color.name(color.alpha() < 255 ? QColor::HexArgb : QColor::HexRgb);
    return QStringLiteral("QToolButton{background:%1;border:1px solid %2;border-radius:3px;}"
                          "QToolButton:hover{border:2px solid %3;}")
        .arg(bg, border, highlight);
}

QString editor_slugify(QString text)
{
    text = text.trimmed().toLower();
    QString slug;
    bool previous_dash = false;
    for (const QChar ch : text) {
        const bool valid = ch.isLetterOrNumber();
        if (valid) {
            slug.append(ch);
            previous_dash = false;
        } else if (!previous_dash && !slug.isEmpty()) {
            slug.append(QLatin1Char('-'));
            previous_dash = true;
        }
    }
    while (slug.endsWith(QLatin1Char('-')))
        slug.chop(1);
    return slug.isEmpty() ? QStringLiteral("library") : slug;
}

QString editor_color_libraries_path()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/Flux Motion");
    QDir dir(base);
    dir.mkpath(QStringLiteral("palettes"));
    return dir.filePath(QStringLiteral("palettes/user-color-libraries.palette.json"));
}

QString fxm_load_selected_color_library_slug()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Color"));
    return settings.value(QStringLiteral("selectedColorLibrarySlug")).toString();
}

void fxm_save_selected_color_library_slug(const QString &slug)
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Color"));
    settings.setValue(QStringLiteral("selectedColorLibrarySlug"), slug);
    settings.sync();
}


const Layer *editor_layer_by_id_for_parenting(const std::shared_ptr<Title> &title, const std::string &id)
{
    if (!title || id.empty()) return nullptr;
    for (const auto &candidate : title->layers) {
        if (candidate && candidate->id == id) return candidate.get();
    }
    return nullptr;
}

static double editor_asset_time_for_parenting_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting)
{
    if (!title || layer.asset_owner_id.empty() ||
        !visiting.insert(layer.asset_owner_id).second)
        return playhead;
    const Layer *asset = editor_layer_by_id_for_parenting(
        title, layer.asset_owner_id);
    if (!asset || asset->type != LayerType::Asset) {
        visiting.erase(layer.asset_owner_id);
        return playhead;
    }
    const double owner_time = editor_asset_time_for_parenting_impl(
        title, *asset, playhead, visiting);
    const double resolved = fxm::asset_runtime::resolve_local_time(
        title->id, *asset, owner_time - asset->in_time);
    visiting.erase(layer.asset_owner_id);
    return resolved;
}

static double editor_asset_time_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    std::set<std::string> visiting;
    return editor_asset_time_for_parenting_impl(title, layer, playhead, visiting);
}

static QTransform editor_layer_local_transform_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    bool include_general_transitions = false)
{
    QTransform local;
    const double resolved_time = editor_asset_time_for_parenting(
        title, layer, playhead);
    const double lt = std::max(0.0, resolved_time - layer.in_time);
    const LayerTransitionVisualState transition = include_general_transitions
        ? evaluate_layer_general_transitions(
              layer.transitions, layer.in_time, layer.out_time, resolved_time)
        : LayerTransitionVisualState{};
    const LayerVector3Value position = evaluated_layer_position_3d(layer, lt);
    local.translate(position.x + transition.translate_x,
                    position.y + transition.translate_y);
    local.rotate(layer.rotation.evaluate(lt));
    local.scale(layer.scale.evaluate(lt).x * transition.scale,
                layer.scale.evaluate(lt).y * transition.scale);
    return local;
}

static std::vector<std::string> editor_group_chain_for_parenting(
    const std::shared_ptr<Title> &title, const std::string &group_parent_id)
{
    std::vector<std::string> chain;
    std::string cursor = group_parent_id;
    std::set<std::string> visited;
    while (!cursor.empty() && visited.insert(cursor).second) {
        const Layer *group = editor_layer_by_id_for_parenting(title, cursor);
        if (!group || !layer_type_is_container(group->type))
            break;
        chain.push_back(cursor);
        cursor = group->parent_id;
    }
    return chain;
}

static std::vector<std::string> editor_group_chain_for_layer_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer)
{
    std::vector<std::string> chain;
    if (layer_type_is_container(layer.type))
        chain.push_back(layer.id);
    const auto ancestors = editor_group_chain_for_parenting(title, layer.parent_id);
    chain.insert(chain.end(), ancestors.begin(), ancestors.end());
    return chain;
}

static std::string editor_common_group_for_parenting(
    const std::shared_ptr<Title> &title, const std::string &child_group_parent_id,
    const Layer &transform_parent)
{
    const auto child_groups = editor_group_chain_for_parenting(title, child_group_parent_id);
    const auto parent_groups = editor_group_chain_for_layer_parenting(title, transform_parent);
    const std::set<std::string> parent_set(parent_groups.begin(), parent_groups.end());
    for (const auto &group_id : child_groups) {
        if (parent_set.find(group_id) != parent_set.end())
            return group_id;
    }
    return {};
}

static QTransform editor_layer_world_transform_for_parenting_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting, bool include_general_transitions);

static QTransform editor_structural_parent_basis_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const std::string &group_parent_id, const std::string &transform_parent_id,
    std::set<std::string> &visiting, bool include_general_transitions)
{
    QTransform group_world;
    if (!group_parent_id.empty()) {
        if (const Layer *group_parent = editor_layer_by_id_for_parenting(title, group_parent_id);
            group_parent && layer_type_is_container(group_parent->type)) {
            group_world = editor_layer_world_transform_for_parenting_impl(
                title, *group_parent, playhead, visiting,
                include_general_transitions);
        }
    }

    if (transform_parent_id.empty())
        return group_world;
    const Layer *transform_parent = editor_layer_by_id_for_parenting(title, transform_parent_id);
    if (!transform_parent)
        return group_world;

    const QTransform transform_world = editor_layer_world_transform_for_parenting_impl(
        title, *transform_parent, playhead, visiting,
        include_general_transitions);
    const std::string common_group_id = editor_common_group_for_parenting(
        title, group_parent_id, *transform_parent);
    if (common_group_id.empty())
        return group_world * transform_world;

    const Layer *common_group = editor_layer_by_id_for_parenting(title, common_group_id);
    if (!common_group)
        return group_world * transform_world;
    std::set<std::string> common_visiting;
    const QTransform common_world = editor_layer_world_transform_for_parenting_impl(
        title, *common_group, playhead, common_visiting,
        include_general_transitions);
    bool invertible = false;
    const QTransform common_inverse = common_world.inverted(&invertible);
    return invertible ? group_world * common_inverse * transform_world
                      : group_world * transform_world;
}

static QTransform editor_parent_bind_transform_for_parenting(
    const Layer &layer)
{
    if (!layer.parent_bind_enabled)
        return QTransform();
    const auto &m = layer.parent_bind_matrix;
    return QTransform(
        m[0], m[4], 0.0,
        m[1], m[5], 0.0,
        m[3], m[7], 1.0);
}

static QTransform editor_parent_basis_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const std::string &group_parent_id, const std::string &transform_parent_id,
    std::set<std::string> &visiting, bool include_general_transitions)
{
    const QTransform structural = editor_structural_parent_basis_for_parenting(
        title, layer, playhead, group_parent_id, transform_parent_id, visiting,
        include_general_transitions);
    return editor_parent_bind_transform_for_parenting(layer) * structural;
}

static QTransform editor_layer_world_transform_for_parenting_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting, bool include_general_transitions)
{
    if (!layer.id.empty() && !visiting.insert(layer.id).second)
        return QTransform();
    const QTransform parent_basis = editor_parent_basis_for_parenting(
        title, layer, playhead, layer.parent_id, layer.transform_parent_id,
        visiting, include_general_transitions);
    /* QTransform products are applied in written order. Keep the established
     * Group contract: layer-local transform first, then the container/parent
     * basis. This is algebraically identical to the previous recursive
     * translate/rotate/scale implementation for Group-only layers. */
    const QTransform world = editor_layer_local_transform_for_parenting(
        title, layer, playhead, include_general_transitions) * parent_basis;
    if (!layer.id.empty())
        visiting.erase(layer.id);
    return world;
}

QTransform editor_layer_world_transform_for_parenting(const std::shared_ptr<Title> &title,
                                                      const Layer &layer,
                                                      double playhead,
                                                      int /*depth*/ = 0)
{
    std::set<std::string> visiting;
    const QTransform legacy = editor_layer_world_transform_for_parenting_impl(
        title, layer, playhead, visiting, false);
    return title ? fxm::transform3d::projected_or_legacy(
                       *title, layer, playhead, legacy)
                 : legacy;
}

static QTransform editor_parent_world_transform_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    std::set<std::string> visiting;
    return editor_parent_basis_for_parenting(
        title, layer, playhead, layer.parent_id, layer.transform_parent_id,
        visiting, false);
}

static QPointF editor_evaluated_local_position_xy_for_parenting(
    const Layer &layer, double local_time)
{
    const LayerVector3Value value =
        evaluated_layer_position_3d(layer, local_time);
    return QPointF(value.x, value.y);
}

static void editor_set_local_position_xy_for_parenting(
    Layer &layer, double local_time, const QPointF &value)
{
    if (layer.dimension_mode == LayerDimensionMode::ThreeD ||
        layer.position_3d_path_enabled) {
        promote_layer_position_to_3d_path(layer);
        set_animated_component(layer.position_3d, local_time, 0, value.x());
        set_animated_component(layer.position_3d, local_time, 1, value.y());
        mirror_position_3d_to_legacy(layer, local_time);
        return;
    }
    set_animated_x(layer.position, local_time, value.x());
    set_animated_y(layer.position, local_time, value.y());
}

static QPointF editor_canvas_delta_to_parent_local_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const QPointF &canvas_delta)
{
    const QTransform parent_world =
        editor_parent_world_transform_for_parenting(title, layer, playhead);
    bool invertible = false;
    const QTransform canvas_to_parent = parent_world.inverted(&invertible);
    if (!invertible) return canvas_delta;
    return canvas_to_parent.map(canvas_delta) -
           canvas_to_parent.map(QPointF(0.0, 0.0));
}

static void editor_translate_layer_in_canvas_for_parenting(
    const std::shared_ptr<Title> &title, Layer &layer, double playhead,
    const QPointF &canvas_delta)
{
    const double local_time = std::clamp(
        playhead - layer.in_time, 0.0,
        std::max(0.0, layer.out_time - layer.in_time));
    const QPointF current =
        editor_evaluated_local_position_xy_for_parenting(layer, local_time);
    editor_set_local_position_xy_for_parenting(
        layer, local_time,
        current + editor_canvas_delta_to_parent_local_for_parenting(
                      title, layer, playhead, canvas_delta));
}

static QTransform editor_layer_world_transform_for_animation_bounds(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    std::set<std::string> visiting;
    const QTransform legacy = editor_layer_world_transform_for_parenting_impl(
        title, layer, playhead, visiting, true);
    return title ? fxm::transform3d::projected_or_legacy(
                       *title, layer, playhead, legacy)
                 : legacy;
}

bool editor_paths_geometrically_equal(const QPainterPath &a, const QPainterPath &b)
{
    if (a.isEmpty() || b.isEmpty())
        return a.isEmpty() && b.isEmpty();

    QPainterPath only_a = a.subtracted(b);
    QPainterPath only_b = b.subtracted(a);
    only_a.setFillRule(Qt::OddEvenFill);
    only_b.setFillRule(Qt::OddEvenFill);
    return only_a.isEmpty() && only_b.isEmpty();
}

int editor_boolean_gradient_type(int gradient_type)
{
    switch (std::clamp(gradient_type, 0, 4)) {
    case 1: return 1;
    case 2: return 2;
    case 4: return 1;
    case 0:
    case 3:
    default: return 0;
    }
}

double editor_normalized_degrees(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    return degrees < 0.0 ? degrees + 360.0 : degrees;
}

void editor_remap_gradient_to_canvas_bounds(int gradient_type,
                                            const QRectF &source_box,
                                            const QTransform &source_world,
                                            const QRectF &result_canvas_bounds,
                                            float &center_x, float &center_y,
                                            float &focal_x, float &focal_y,
                                            float &scale, float &angle_degrees)
{
    if (source_box.width() <= 1e-9 || source_box.height() <= 1e-9 ||
        result_canvas_bounds.width() <= 1e-9 || result_canvas_bounds.height() <= 1e-9)
        return;

    const QPointF source_center(source_box.left() + (double)center_x * source_box.width(),
                                source_box.top() + (double)center_y * source_box.height());
    const QPointF source_focal(source_box.left() + (double)focal_x * source_box.width(),
                               source_box.top() + (double)focal_y * source_box.height());
    const double source_angle = (double)angle_degrees * std::acos(-1.0) / 180.0;
    const QPointF source_axis(std::cos(source_angle), std::sin(source_angle));

    const QPointF world_center = source_world.map(source_center);
    const QPointF world_focal = source_world.map(source_focal);
    auto normalize_result_point = [&](const QPointF &point) {
        return QPointF((point.x() - result_canvas_bounds.left()) / result_canvas_bounds.width(),
                       (point.y() - result_canvas_bounds.top()) / result_canvas_bounds.height());
    };

    const QPointF normalized_center = normalize_result_point(world_center);
    const QPointF normalized_focal = normalize_result_point(world_focal);
    center_x = (float)std::clamp(normalized_center.x(), -100.0, 100.0);
    center_y = (float)std::clamp(normalized_center.y(), -100.0, 100.0);
    focal_x = (float)std::clamp(normalized_focal.x(), -100.0, 100.0);
    focal_y = (float)std::clamp(normalized_focal.y(), -100.0, 100.0);

    const int type = editor_boolean_gradient_type(gradient_type);
    double source_extent = 1.0;
    double result_base = 1.0;
    if (type == 1) {
        source_extent = std::max(1.0, std::max(source_box.width(), source_box.height()) *
                                      0.5 * std::clamp((double)scale, 0.01, 100.0));
        result_base = std::max(1.0, std::max(result_canvas_bounds.width(),
                                             result_canvas_bounds.height()) * 0.5);
    } else {
        source_extent = std::max(1.0, std::hypot(source_box.width(), source_box.height()) *
                                      0.5 * std::clamp((double)scale, 0.01, 100.0));
        result_base = std::max(1.0, std::hypot(result_canvas_bounds.width(),
                                               result_canvas_bounds.height()) * 0.5);
    }

    /* Conical gradients have no radius in the renderer, but mapping a stable
     * reference vector still preserves their canvas-space orientation. */
    if (type == 2)
        source_extent = std::max(1.0, std::hypot(source_box.width(), source_box.height()) * 0.5);

    const QPointF world_axis_point = source_world.map(source_center + source_axis * source_extent);
    const QPointF world_axis = world_axis_point - world_center;
    const double world_extent = std::hypot(world_axis.x(), world_axis.y());
    if (world_extent > 1e-9) {
        angle_degrees = (float)editor_normalized_degrees(
            std::atan2(world_axis.y(), world_axis.x()) * 180.0 / std::acos(-1.0));
        if (type != 2)
            scale = (float)std::clamp(world_extent / result_base, 0.01, 100.0);
    }
}

QString editor_font_awesome_family()
{
    static const QString family = []() {
        const QStringList preferred = {
            QStringLiteral("Font Awesome 6 Free"),
            QStringLiteral("Font Awesome 5 Free"),
            QStringLiteral("FontAwesome")
        };
        QFontDatabase database;
        for (const QString &candidate : preferred) {
            if (database.hasFamily(candidate))
                return candidate;
        }
        return QString();
    }();
    return family;
}

QIcon editor_font_awesome_icon(ushort codepoint, const QString &fallback, QWidget *widget)
{
    const int extent = 30;
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font;
    const QString family = editor_font_awesome_family();
    QString glyph = fallback;
    if (!family.isEmpty()) {
        font.setFamily(family);
        font.setWeight(QFont::Black);
        glyph = QString(QChar(codepoint));
    } else {
        font.setBold(true);
    }
    font.setPixelSize(std::max(11, extent - 9));
    painter.setFont(font);
    painter.setPen((widget ? widget->palette() : qApp->palette()).color(QPalette::ButtonText));
    painter.drawText(pixmap.rect(), Qt::AlignCenter, glyph);
    return QIcon(pixmap);
}

bool editor_parenting_would_cycle(const std::shared_ptr<Title> &title,
                                  const std::string &layer_id,
                                  const std::string &parent_id)
{
    if (!title || layer_id.empty() || parent_id.empty())
        return false;
    std::set<std::string> visited;
    std::function<bool(const std::string &)> depends_on_layer;
    depends_on_layer = [&](const std::string &candidate_id) {
        if (candidate_id.empty()) return false;
        if (candidate_id == layer_id) return true;
        if (!visited.insert(candidate_id).second) return false;
        const Layer *candidate = editor_layer_by_id_for_parenting(title, candidate_id);
        if (!candidate) return false;
        return depends_on_layer(candidate->transform_parent_id) ||
               depends_on_layer(candidate->parent_id);
    };
    return depends_on_layer(parent_id);
}

bool editor_layer_has_ancestor_for_grouping(const std::shared_ptr<Title> &title,
                                            const Layer &layer,
                                            const std::string &ancestor_id)
{
    std::string cursor = layer.parent_id;
    int guard = 0;
    while (!cursor.empty() && guard++ < 64) {
        if (cursor == ancestor_id)
            return true;
        const Layer *parent = editor_layer_by_id_for_parenting(title, cursor);
        if (!parent)
            break;
        cursor = parent->parent_id;
    }
    return false;
}

QRectF editor_layer_local_rect_for_grouping(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    const double local_time = std::max(
        0.0, editor_asset_time_for_parenting(title, layer, playhead) -
             layer.in_time);
    const double width = std::max(0.0, eval_box_width(layer, local_time));
    const double height = std::max(0.0, eval_box_height(layer, local_time));
    const double origin_x = eval_origin_x(layer, local_time);
    const double origin_y = eval_origin_y(layer, local_time);
    return QRectF(-origin_x * width, -origin_y * height, width, height);
}

QRectF editor_layer_canvas_bounds_for_grouping(const std::shared_ptr<Title> &title,
                                                const Layer &layer,
                                                double playhead)
{
    if (!title)
        return {};
    if (layer_type_is_container(layer.type)) {
        QRectF combined;
        bool have = false;
        for (const auto &candidate : title->layers) {
            if (!candidate || layer_type_is_container(candidate->type) ||
                !editor_layer_has_ancestor_for_grouping(title, *candidate, layer.id))
                continue;
            const QRectF child = editor_layer_canvas_bounds_for_grouping(
                title, *candidate, playhead);
            if (!child.isValid() || child.isEmpty())
                continue;
            combined = have ? combined.united(child) : child;
            have = true;
        }
        if (have)
            return combined.normalized();
        const QPointF origin = editor_layer_world_transform_for_parenting(
            title, layer, playhead).map(QPointF(0.0, 0.0));
        return QRectF(origin - QPointF(16.0, 16.0), QSizeF(32.0, 32.0));
    }

    const QRectF local = editor_layer_local_rect_for_grouping(title, layer, playhead);
    if (!local.isValid() || local.isEmpty())
        return {};
    const QTransform world = editor_layer_world_transform_for_parenting(
        title, layer, playhead);
    QPolygonF polygon;
    polygon << world.map(local.topLeft()) << world.map(local.topRight())
            << world.map(local.bottomRight()) << world.map(local.bottomLeft());
    return polygon.boundingRect().normalized();
}

static QRectF editor_layer_canvas_bounds_for_animation(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    if (!title)
        return {};

    const Layer *visibility_cursor = &layer;
    int visibility_guard = 0;
    while (visibility_cursor && visibility_guard++ < 64) {
        const double cursor_time = editor_asset_time_for_parenting(
            title, *visibility_cursor, playhead);
        if (!visibility_cursor->visible ||
            cursor_time < visibility_cursor->in_time - 1e-6 ||
            cursor_time > visibility_cursor->out_time + 1e-6)
            return {};
        visibility_cursor = visibility_cursor->parent_id.empty()
            ? nullptr
            : editor_layer_by_id_for_parenting(
                  title, visibility_cursor->parent_id);
    }

    const double resolved_time = editor_asset_time_for_parenting(
        title, layer, playhead);

    if (layer_type_is_container(layer.type)) {
        QRectF combined;
        bool have = false;
        for (const auto &candidate : title->layers) {
            if (!candidate || layer_type_is_container(candidate->type) ||
                !editor_layer_has_ancestor_for_grouping(title, *candidate, layer.id))
                continue;
            const QRectF child = editor_layer_canvas_bounds_for_animation(
                title, *candidate, playhead);
            if (!child.isValid() || child.isEmpty())
                continue;
            combined = have ? combined.united(child) : child;
            have = true;
        }
        if (have)
            return combined.normalized();
        const QPointF origin = editor_layer_world_transform_for_animation_bounds(
            title, layer, playhead).map(QPointF(0.0, 0.0));
        return QRectF(origin - QPointF(16.0, 16.0), QSizeF(32.0, 32.0));
    }

    const QRectF local = editor_layer_local_rect_for_grouping(
        title, layer, playhead);
    if (!local.isValid() || local.isEmpty())
        return {};

    const double local_time = std::max(0.0, resolved_time - layer.in_time);
    auto quad_value = [local_time](const AnimatedVec2Property &property,
                                   float legacy_x, float legacy_y) {
        if (property.is_animated() || property.static_value.x != 0.0 ||
            property.static_value.y != 0.0)
            return property.evaluate(local_time);
        return Vec2Value{legacy_x, legacy_y};
    };
    const Vec2Value qtl = quad_value(
        layer.transform_quad_tl, layer.transform_quad_tl_x,
        layer.transform_quad_tl_y);
    const Vec2Value qtr = quad_value(
        layer.transform_quad_tr, layer.transform_quad_tr_x,
        layer.transform_quad_tr_y);
    const Vec2Value qbr = quad_value(
        layer.transform_quad_br, layer.transform_quad_br_x,
        layer.transform_quad_br_y);
    const Vec2Value qbl = quad_value(
        layer.transform_quad_bl, layer.transform_quad_bl_x,
        layer.transform_quad_bl_y);

    QPolygonF local_polygon;
    local_polygon
        << local.topLeft() + QPointF(qtl.x * local.width(), qtl.y * local.height())
        << local.topRight() + QPointF(qtr.x * local.width(), qtr.y * local.height())
        << local.bottomRight() + QPointF(qbr.x * local.width(), qbr.y * local.height())
        << local.bottomLeft() + QPointF(qbl.x * local.width(), qbl.y * local.height());

    /* Text Animator displacement/scale/blur is the only text-specific
     * animation envelope. Legacy text transitions are migrated before this
     * editor path is reached. */
    QRectF text_transition_bounds = local_polygon.boundingRect();
    const double animator_padding = text_animator_stack_visual_padding(
        layer.text_animators, local.width(), local.height());
    if (animator_padding > 0.0)
        text_transition_bounds = text_transition_bounds.adjusted(
            -animator_padding, -animator_padding,
             animator_padding,  animator_padding);
    if (text_transition_bounds != local_polygon.boundingRect()) {
        local_polygon.clear();
        local_polygon << text_transition_bounds.topLeft()
                      << text_transition_bounds.topRight()
                      << text_transition_bounds.bottomRight()
                      << text_transition_bounds.bottomLeft();
    }

    const QTransform world = editor_layer_world_transform_for_animation_bounds(
        title, layer, playhead);
    QPolygonF canvas_polygon;
    for (const QPointF &point : local_polygon)
        canvas_polygon << world.map(point);
    return canvas_polygon.boundingRect().normalized();
}

QRectF editor_title_animation_bounds(const std::shared_ptr<Title> &title)
{
    if (!title)
        return {};

    /* Independent nested assets must not consult the wall clock while the
     * reusable asset envelope is calculated. Evaluate their complete source
     * timelines deterministically, then keep the resulting union fixed on the
     * Asset Layer instance. */
    for (const auto &layer : title->layers) {
        if (layer && layer->type == LayerType::Asset)
            layer->asset_playback_mode = 0;
    }

    double duration = std::max(0.0, title->duration);
    for (const auto &layer : title->layers) {
        if (!layer)
            continue;
        duration = std::max(duration, layer->out_time);
        if (layer->type == LayerType::Asset)
            duration = std::max(duration, layer->asset_duration);
    }

    const double native_step = std::max(1.0 / 240.0, obs_frame_duration());
    constexpr int kMaximumEnvelopeSamples = 12000;
    int sample_count = std::max(1, static_cast<int>(std::ceil(
        duration / native_step)));
    sample_count = std::min(sample_count, kMaximumEnvelopeSamples);

    QRectF combined;
    bool have_bounds = false;
    for (int sample = 0; sample <= sample_count; ++sample) {
        const double time = sample_count > 0
            ? duration * static_cast<double>(sample) /
                  static_cast<double>(sample_count)
            : 0.0;
        for (const auto &layer : title->layers) {
            if (!layer || !layer->parent_id.empty())
                continue;
            const QRectF bounds = editor_layer_canvas_bounds_for_animation(
                title, *layer, time);
            if (!bounds.isValid() || bounds.isEmpty())
                continue;
            combined = have_bounds ? combined.united(bounds) : bounds;
            have_bounds = true;
        }
    }
    return have_bounds ? combined.normalized() : QRectF();
}

void editor_offset_position_track(AnimatedVec2Property &position,
                                  double dx, double dy)
{
    position.static_value.x += dx;
    position.static_value.y += dy;
    for (VectorKeyframe &keyframe : position.keyframes) {
        keyframe.value.x += dx;
        keyframe.value.y += dy;
    }
}

void editor_offset_layer_position_track(Layer &layer, double dx, double dy)
{
    if (layer.position_3d_path_enabled) {
        promote_layer_position_to_3d_path(layer);
        layer.position_3d.static_value.x += dx;
        layer.position_3d.static_value.y += dy;
        for (Vector3Keyframe &keyframe : layer.position_3d.keyframes) {
            keyframe.value.x += dx;
            keyframe.value.y += dy;
        }
        mirror_position_3d_track_to_legacy(layer);
    } else {
        editor_offset_position_track(layer.position, dx, dy);
    }
}

static void editor_offset_layer_position_track_3d(Layer &layer,
                                                  double dx, double dy,
                                                  double dz)
{
    constexpr double kZPromotionEpsilon = 1.0e-7;
    const bool needs_3d_track =
        layer.dimension_mode == LayerDimensionMode::ThreeD ||
        layer.position_3d_path_enabled || std::abs(dz) > kZPromotionEpsilon;
    if (!needs_3d_track) {
        editor_offset_layer_position_track(layer, dx, dy);
        return;
    }

    promote_layer_position_to_3d_path(layer);
    layer.position_3d_path_enabled = true;
    if (std::abs(dz) > kZPromotionEpsilon)
        layer.dimension_mode = LayerDimensionMode::ThreeD;
    layer.position_3d.static_value.x += dx;
    layer.position_3d.static_value.y += dy;
    layer.position_3d.static_value.z += dz;
    for (Vector3Keyframe &keyframe : layer.position_3d.keyframes) {
        keyframe.value.x += dx;
        keyframe.value.y += dy;
        keyframe.value.z += dz;
    }
    mirror_position_3d_track_to_legacy(layer);
}


void editor_set_local_transform_from_world(const std::shared_ptr<Title> &title,
                                           Layer &layer,
                                           const QTransform &world,
                                           const std::string &new_group_parent_id,
                                           const std::string &new_transform_parent_id,
                                           double playhead);

struct EditorWorldTransformSnapshot {
    bool uses_3d = false;
    bool has_3d_matrix = false;
    QTransform affine;
    QMatrix4x4 world_3d;
};

/* Reparenting is a track operation, not a single-frame operation.  The old
 * editor contract captured only the current world transform and rewrote one
 * local key. Every other key remained expressed in the previous parent basis,
 * which made an otherwise local animation jump as soon as it was grouped or
 * assigned a transform parent. */
struct EditorWorldTransformTrackSnapshot {
    std::vector<double> title_times;
    std::vector<EditorWorldTransformSnapshot> world_samples;
    bool multi_sampled = false;
};

static bool editor_scalar_property_varies(const AnimatedProperty &property)
{
    if (property.keyframes.size() < 2)
        return false;
    constexpr double kEpsilon = 1.0e-7;
    const double reference = property.evaluate(property.keyframes.front().time);
    for (std::size_t index = 0; index + 1 < property.keyframes.size(); ++index) {
        const double start = property.keyframes[index].time;
        const double end = property.keyframes[index + 1].time;
        for (double alpha : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            const double value = property.evaluate(start + (end - start) * alpha);
            if (std::abs(value - reference) > kEpsilon)
                return true;
        }
    }
    return false;
}

static bool editor_vector_property_varies(const AnimatedVec2Property &property)
{
    if (property.keyframes.size() < 2)
        return false;
    constexpr double kEpsilon = 1.0e-6;
    const Vec2Value reference = property.evaluate(property.keyframes.front().time);
    auto differs = [&](const Vec2Value &value) {
        return std::abs(value.x - reference.x) > kEpsilon ||
               std::abs(value.y - reference.y) > kEpsilon ||
               std::abs(value.z - reference.z) > kEpsilon;
    };
    for (std::size_t index = 0; index + 1 < property.keyframes.size(); ++index) {
        const double start = property.keyframes[index].time;
        const double end = property.keyframes[index + 1].time;
        for (double alpha : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            if (differs(property.evaluate(start + (end - start) * alpha)))
                return true;
        }
    }
    return false;
}

static bool editor_vector3_property_varies(const AnimatedVec3Property &property)
{
    if (property.keyframes.size() < 2)
        return false;
    constexpr double kEpsilon = 1.0e-6;
    const Vec3Value reference = property.evaluate(property.keyframes.front().time);
    auto differs = [&](const Vec3Value &value) {
        return std::abs(value.x - reference.x) > kEpsilon ||
               std::abs(value.y - reference.y) > kEpsilon ||
               std::abs(value.z - reference.z) > kEpsilon;
    };
    for (std::size_t index = 0; index + 1 < property.keyframes.size(); ++index) {
        const double start = property.keyframes[index].time;
        const double end = property.keyframes[index + 1].time;
        for (double alpha : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            if (differs(property.evaluate(start + (end - start) * alpha)))
                return true;
        }
    }
    return false;
}

static bool editor_layer_transform_is_animated(const Layer &layer)
{
    return editor_vector_property_varies(layer.position) ||
        (layer.position_3d_path_enabled &&
         editor_vector3_property_varies(layer.position_3d)) ||
        editor_scalar_property_varies(layer.position_z) ||
        editor_vector_property_varies(layer.scale) ||
        editor_scalar_property_varies(layer.scale_z) ||
        editor_scalar_property_varies(layer.rotation) ||
        editor_scalar_property_varies(layer.rotation_x) ||
        editor_scalar_property_varies(layer.rotation_y) ||
        editor_scalar_property_varies(layer.orientation_x) ||
        editor_scalar_property_varies(layer.orientation_y) ||
        editor_scalar_property_varies(layer.orientation_z) ||
        editor_vector_property_varies(layer.origin_prop) ||
        editor_scalar_property_varies(layer.anchor_z);
}

static std::size_t editor_layer_transform_keyframe_count(const Layer &layer)
{
    return layer.position.keyframes.size() + layer.position_3d.keyframes.size() +
        layer.position_z.keyframes.size() + layer.scale.keyframes.size() +
        layer.scale_z.keyframes.size() + layer.rotation.keyframes.size() +
        layer.rotation_x.keyframes.size() + layer.rotation_y.keyframes.size() +
        layer.orientation_x.keyframes.size() +
        layer.orientation_y.keyframes.size() +
        layer.orientation_z.keyframes.size() +
        layer.origin_prop.keyframes.size() + layer.anchor_z.keyframes.size();
}

static uint64_t editor_elapsed_us(
    const std::chrono::steady_clock::time_point &started)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
}

static bool editor_transform_chain_is_animated_impl(
    const std::shared_ptr<Title> &title, const std::string &layer_id,
    std::set<std::string> &visited)
{
    if (!title || layer_id.empty() || !visited.insert(layer_id).second)
        return false;
    const Layer *layer = editor_layer_by_id_for_parenting(title, layer_id);
    if (!layer)
        return false;
    if (editor_layer_transform_is_animated(*layer))
        return true;
    return editor_transform_chain_is_animated_impl(
               title, layer->parent_id, visited) ||
           editor_transform_chain_is_animated_impl(
               title, layer->transform_parent_id, visited);
}

static bool editor_transform_chain_is_animated(
    const std::shared_ptr<Title> &title, const std::string &layer_id)
{
    std::set<std::string> visited;
    return editor_transform_chain_is_animated_impl(title, layer_id, visited);
}

template<typename Property>
static void editor_append_property_key_times(const Layer &owner,
                                             const Property &property,
                                             std::vector<double> &times)
{
    const double start = std::max(0.0, owner.in_time);
    const double end = std::max(start, owner.out_time);
    for (const auto &keyframe : property.keyframes)
        times.push_back(std::clamp(owner.in_time + keyframe.time, start, end));
}

static void editor_append_layer_transform_key_times(const Layer &layer,
                                                    std::vector<double> &times)
{
    editor_append_property_key_times(layer, layer.position, times);
    editor_append_property_key_times(layer, layer.position_3d, times);
    editor_append_property_key_times(layer, layer.position_z, times);
    editor_append_property_key_times(layer, layer.scale, times);
    editor_append_property_key_times(layer, layer.scale_z, times);
    editor_append_property_key_times(layer, layer.rotation, times);
    editor_append_property_key_times(layer, layer.rotation_x, times);
    editor_append_property_key_times(layer, layer.rotation_y, times);
    editor_append_property_key_times(layer, layer.orientation_x, times);
    editor_append_property_key_times(layer, layer.orientation_y, times);
    editor_append_property_key_times(layer, layer.orientation_z, times);
    editor_append_property_key_times(layer, layer.origin_prop, times);
    editor_append_property_key_times(layer, layer.anchor_z, times);
}

static void editor_append_transform_chain_key_times_impl(
    const std::shared_ptr<Title> &title, const std::string &layer_id,
    std::vector<double> &times, std::set<std::string> &visited)
{
    if (!title || layer_id.empty() || !visited.insert(layer_id).second)
        return;
    const Layer *layer = editor_layer_by_id_for_parenting(title, layer_id);
    if (!layer)
        return;
    editor_append_layer_transform_key_times(*layer, times);
    editor_append_transform_chain_key_times_impl(
        title, layer->parent_id, times, visited);
    editor_append_transform_chain_key_times_impl(
        title, layer->transform_parent_id, times, visited);
}

static void editor_append_transform_chain_key_times(
    const std::shared_ptr<Title> &title, const std::string &layer_id,
    std::vector<double> &times)
{
    std::set<std::string> visited;
    editor_append_transform_chain_key_times_impl(title, layer_id, times,
                                                 visited);
}

static bool editor_parent_bind_is_identity(const Layer &layer)
{
    if (!layer.parent_bind_enabled)
        return true;
    constexpr double epsilon = 1.0e-9;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            const double expected = row == column ? 1.0 : 0.0;
            if (std::abs(layer.parent_bind_matrix[
                    static_cast<std::size_t>(row * 4 + column)] - expected) > epsilon)
                return false;
        }
    }
    return true;
}

static void editor_store_parent_bind_matrix(Layer &layer,
                                            const QMatrix4x4 &matrix)
{
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            layer.parent_bind_matrix[static_cast<std::size_t>(row * 4 + column)] =
                static_cast<double>(matrix(row, column));
    layer.parent_bind_enabled = true;
    if (editor_parent_bind_is_identity(layer))
        layer.parent_bind_enabled = false;
}

static void editor_store_parent_bind_transform(Layer &layer,
                                               const QTransform &transform)
{
    layer.parent_bind_matrix = {
        transform.m11(), transform.m21(), 0.0, transform.dx(),
        transform.m12(), transform.m22(), 0.0, transform.dy(),
        0.0,             0.0,             1.0, 0.0,
        0.0,             0.0,             0.0, 1.0
    };
    layer.parent_bind_enabled = true;
    if (editor_parent_bind_is_identity(layer))
        layer.parent_bind_enabled = false;
}

/* Development Version 216: reparenting is represented by one static bind
 * matrix evaluated at the user's playhead. The authored local animation is
 * never sampled or rewritten. This matches normal DCC parenting semantics:
 * the layer does not jump at the parenting frame, retains its exact authored
 * keyframes/curves, and then inherits subsequent motion from the new parent. */
static bool editor_reparent_layer_with_parent_bind(
    const std::shared_ptr<Title> &title, Layer &layer,
    const std::string &new_group_parent_id,
    const std::string &new_transform_parent_id, double playhead)
{
    const auto started = std::chrono::steady_clock::now();
    const std::size_t keyframes_before =
        editor_layer_transform_keyframe_count(layer);
    if (!title) {
        FXM_LOG_WARNING("Grouping", QStringLiteral(
            "Bind reparent rejected layer=%1 reason=no-title")
            .arg(QString::fromStdString(layer.id)));
        return false;
    }

    Layer destination_probe = layer;
    destination_probe.parent_id = new_group_parent_id;
    destination_probe.transform_parent_id = new_transform_parent_id;
    destination_probe.parent_bind_enabled = false;

    const bool source_uses_3d =
        fxm::transform3d::layer_or_ancestor_uses_3d(*title, layer);
    const bool destination_uses_3d =
        fxm::transform3d::layer_or_ancestor_uses_3d(*title, destination_probe);
    const std::string old_group_parent_id = layer.parent_id;
    const std::string old_transform_parent_id = layer.transform_parent_id;

    if (source_uses_3d || destination_uses_3d) {
        const QMatrix4x4 source_basis =
            fxm::transform3d::layer_parent_world_matrix(*title, layer, playhead);
        const QMatrix4x4 destination_basis =
            fxm::transform3d::layer_parent_world_matrix(
                *title, destination_probe, playhead);
        bool invertible = false;
        const QMatrix4x4 destination_inverse =
            destination_basis.inverted(&invertible);
        if (!invertible) {
            FXM_LOG_WARNING("Coordinates", QStringLiteral(
                "Bind reparent rejected layer=%1 reason=non-invertible-3d-destination "
                "newGroup=%2 newTransformParent=%3 costUs=%4")
                .arg(QString::fromStdString(layer.id))
                .arg(QString::fromStdString(new_group_parent_id))
                .arg(QString::fromStdString(new_transform_parent_id))
                .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
            return false;
        }
        editor_store_parent_bind_matrix(
            layer, destination_inverse * source_basis);
    } else {
        std::set<std::string> source_visiting;
        const QTransform source_basis = editor_parent_basis_for_parenting(
            title, layer, playhead, layer.parent_id, layer.transform_parent_id,
            source_visiting, false);
        std::set<std::string> destination_visiting;
        const QTransform destination_basis =
            editor_structural_parent_basis_for_parenting(
                title, destination_probe, playhead, new_group_parent_id,
                new_transform_parent_id, destination_visiting, false);
        bool invertible = false;
        const QTransform destination_inverse =
            destination_basis.inverted(&invertible);
        if (!invertible) {
            FXM_LOG_WARNING("Coordinates", QStringLiteral(
                "Bind reparent rejected layer=%1 reason=non-invertible-destination "
                "newGroup=%2 newTransformParent=%3 costUs=%4")
                .arg(QString::fromStdString(layer.id))
                .arg(QString::fromStdString(new_group_parent_id))
                .arg(QString::fromStdString(new_transform_parent_id))
                .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
            return false;
        }
        editor_store_parent_bind_transform(
            layer, source_basis * destination_inverse);
    }

    layer.parent_id = new_group_parent_id;
    layer.transform_parent_id = new_transform_parent_id;
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Bind reparent applied layer=%1 name=\"%2\" mode=%3 "
        "oldGroup=%4 newGroup=%5 oldTransformParent=%6 newTransformParent=%7 "
        "bindEnabled=%8 keyframesBefore=%9 keyframesAfter=%10 costUs=%11")
        .arg(QString::fromStdString(layer.id))
        .arg(QString::fromStdString(layer.name))
        .arg(source_uses_3d || destination_uses_3d
                 ? QStringLiteral("3d") : QStringLiteral("2d"))
        .arg(QString::fromStdString(old_group_parent_id))
        .arg(QString::fromStdString(new_group_parent_id))
        .arg(QString::fromStdString(old_transform_parent_id))
        .arg(QString::fromStdString(new_transform_parent_id))
        .arg(layer.parent_bind_enabled ? 1 : 0)
        .arg(static_cast<qulonglong>(keyframes_before))
        .arg(static_cast<qulonglong>(
            editor_layer_transform_keyframe_count(layer)))
        .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
    return true;
}

static std::vector<double> editor_reparent_sample_times(
    const std::shared_ptr<Title> &title, const Layer &layer,
    const std::string &new_group_parent_id,
    const std::string &new_transform_parent_id, double playhead)
{
    const auto started = std::chrono::steady_clock::now();
    const bool source_basis_animated =
        editor_transform_chain_is_animated(title, layer.parent_id) ||
        editor_transform_chain_is_animated(title, layer.transform_parent_id);
    const bool destination_basis_animated =
        editor_transform_chain_is_animated(title, new_group_parent_id) ||
        editor_transform_chain_is_animated(title, new_transform_parent_id);
    const bool animated = editor_layer_transform_is_animated(layer) ||
        source_basis_animated || destination_basis_animated;
    if (!animated) {
        FXM_LOG_TRACE("Grouping", QStringLiteral(
            "Reparent sampling layer=%1 mode=single-static sampleCount=1 costUs=%2")
            .arg(QString::fromStdString(layer.id))
            .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
        return {playhead};
    }

    const double start = std::max(0.0, layer.in_time);
    const double end = std::max(start, layer.out_time);
    const double duration = end - start;
    std::vector<double> times{start, end,
                              std::clamp(playhead, start, end)};
    editor_append_layer_transform_key_times(layer, times);
    editor_append_transform_chain_key_times(title, layer.parent_id, times);
    editor_append_transform_chain_key_times(title, layer.transform_parent_id,
                                            times);
    editor_append_transform_chain_key_times(title, new_group_parent_id, times);
    editor_append_transform_chain_key_times(title, new_transform_parent_id,
                                            times);
    const std::size_t authored_sample_candidates = times.size();
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
        [](double a, double b) { return std::abs(a - b) <= 1.0e-9; }),
        times.end());
    const std::size_t authored_unique_samples = times.size();
    int adaptive_samples = 0;

    /* A changing parent basis cannot be represented by merely offsetting the
     * child's existing keys. Supplement the authored times, but cap the total
     * number of evaluation samples rather than the number per second. A long
     * title must never turn one eight-key animation into thousands of keys. */
    if ((source_basis_animated || destination_basis_animated) &&
        duration > 1.0e-9) {
        constexpr double kSupplementalSamplesPerSecond = 4.0;
        constexpr std::size_t kMaximumReparentSamples = 64;
        const std::size_t available = authored_unique_samples <
                kMaximumReparentSamples
            ? kMaximumReparentSamples - authored_unique_samples
            : 0;
        adaptive_samples = static_cast<int>(std::min<std::size_t>(
            available,
            static_cast<std::size_t>(std::ceil(
                duration * kSupplementalSamplesPerSecond))));
        times.reserve(times.size() + static_cast<std::size_t>(adaptive_samples));
        for (int sample = 1; sample <= adaptive_samples; ++sample) {
            const double alpha = double(sample) /
                                 double(adaptive_samples + 1);
            times.push_back(start + duration * alpha);
        }
    }

    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
        [](double a, double b) { return std::abs(a - b) <= 1.0e-9; }),
        times.end());
    FXM_LOG_DEBUG("Grouping", QStringLiteral(
        "Reparent sampling layer=%1 name=\"%2\" duration=%3 "
        "layerAnimated=%4 sourceBasisAnimated=%5 destinationBasisAnimated=%6 "
        "authoredCandidates=%7 authoredUnique=%8 adaptiveSamples=%9 "
        "sampleCap=%10 finalSamples=%11 transformKeyframes=%12 costUs=%13")
        .arg(QString::fromStdString(layer.id),
             QString::fromStdString(layer.name))
        .arg(duration, 0, 'f', 3)
        .arg(editor_layer_transform_is_animated(layer) ? 1 : 0)
        .arg(source_basis_animated ? 1 : 0)
        .arg(destination_basis_animated ? 1 : 0)
        .arg(static_cast<qulonglong>(authored_sample_candidates))
        .arg(static_cast<qulonglong>(authored_unique_samples))
        .arg(adaptive_samples)
        .arg(64)
        .arg(static_cast<qulonglong>(times.size()))
        .arg(static_cast<qulonglong>(
            editor_layer_transform_keyframe_count(layer)))
        .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
    return times;
}

EditorWorldTransformSnapshot editor_capture_world_transform_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead)
{
    EditorWorldTransformSnapshot snapshot;
    snapshot.affine = editor_layer_world_transform_for_parenting(
        title, layer, playhead);
    snapshot.has_3d_matrix = title &&
        fxm::transform3d::layer_supports_3d(layer);
    snapshot.uses_3d = snapshot.has_3d_matrix &&
        fxm::transform3d::layer_or_ancestor_uses_3d(*title, layer);
    snapshot.world_3d.setToIdentity();
    if (snapshot.has_3d_matrix)
        snapshot.world_3d = fxm::transform3d::layer_world_matrix(
            *title, layer, playhead);
    return snapshot;
}

EditorWorldTransformTrackSnapshot editor_capture_world_transform_track_for_parenting(
    const std::shared_ptr<Title> &title, const Layer &layer,
    const std::string &new_group_parent_id,
    const std::string &new_transform_parent_id, double playhead)
{
    const auto started = std::chrono::steady_clock::now();
    EditorWorldTransformTrackSnapshot track;
    track.title_times = editor_reparent_sample_times(
        title, layer, new_group_parent_id, new_transform_parent_id, playhead);
    track.multi_sampled = track.title_times.size() > 1;
    track.world_samples.reserve(track.title_times.size());
    for (double title_time : track.title_times)
        track.world_samples.push_back(
            editor_capture_world_transform_for_parenting(
                title, layer, title_time));
    FXM_LOG_DEBUG("Grouping", QStringLiteral(
        "Captured reparent world track layer=%1 samples=%2 multiSampled=%3 "
        "oldGroup=%4 newGroup=%5 oldTransformParent=%6 "
        "newTransformParent=%7 costUs=%8")
        .arg(QString::fromStdString(layer.id))
        .arg(static_cast<qulonglong>(track.world_samples.size()))
        .arg(track.multi_sampled ? 1 : 0)
        .arg(QString::fromStdString(layer.parent_id),
             QString::fromStdString(new_group_parent_id),
             QString::fromStdString(layer.transform_parent_id),
             QString::fromStdString(new_transform_parent_id))
        .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
    return track;
}

static bool editor_decompose_local_3d(const QMatrix4x4 &local,
                                      QVector3D &translation,
                                      QVector3D &rotation,
                                      QVector3D &scale)
{
    const QVector3D raw_x(local(0, 0), local(1, 0), local(2, 0));
    const QVector3D raw_y(local(0, 1), local(1, 1), local(2, 1));
    const QVector3D raw_z(local(0, 2), local(1, 2), local(2, 2));
    const float scale_x = raw_x.length();
    if (scale_x < 1.0e-8f)
        return false;

    /* Reparenting between rotated, non-uniformly scaled ancestors can produce
     * a small shear term even though the layer model is TRS-only. Remove that
     * shear with a stable Gram-Schmidt basis and retain the closest representable
     * local transform rather than feeding a non-orthogonal matrix to Euler
     * extraction. Exact TRS parent chains remain exact. */
    QVector3D x = raw_x / scale_x;
    QVector3D y_orthogonal = raw_y -
        x * QVector3D::dotProduct(raw_y, x);
    const float scale_y = y_orthogonal.length();
    if (scale_y < 1.0e-8f)
        return false;
    QVector3D y = y_orthogonal / scale_y;
    QVector3D z = QVector3D::crossProduct(x, y).normalized();
    if (z.lengthSquared() < 1.0e-12f)
        return false;
    float scale_z = QVector3D::dotProduct(raw_z, z);
    if (std::abs(scale_z) < 1.0e-8f)
        return false;
    /* scale_z intentionally keeps its sign. A mirrored parent chain is
     * represented by a negative local Z scale while the rotation basis stays
     * right-handed, matching the renderer's culling/winding semantics. */
    scale = QVector3D(scale_x, scale_y, scale_z);

    /* Extract the complete local rotation basis as Rz * Ry * Rx. The caller
     * stores it in Rotation XYZ and resets Orientation XYZ, preserving the
     * visible basis after reparenting without applying Orientation twice.
     * Generic quaternion-to-Euler conversion can use different ordering
     * conventions near gimbal lock across math libraries and Qt versions. */
    const double r00 = x.x();
    const double r10 = x.y();
    const double r20 = x.z();
    const double r21 = y.z();
    const double r22 = z.z();
    const double r01 = y.x();
    const double r11 = y.y();
    const double y_angle = std::asin(std::clamp(-r20, -1.0, 1.0));
    const double cos_y = std::cos(y_angle);
    double x_angle = 0.0;
    double z_angle = 0.0;
    if (std::abs(cos_y) > 1.0e-7) {
        x_angle = std::atan2(r21, r22);
        z_angle = std::atan2(r10, r00);
    } else {
        /* At gimbal lock choose X=0 and solve the remaining Z rotation. */
        z_angle = std::atan2(-r01, r11);
    }
    constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;
    rotation = QVector3D(static_cast<float>(x_angle * kRadiansToDegrees),
                         static_cast<float>(y_angle * kRadiansToDegrees),
                         static_cast<float>(z_angle * kRadiansToDegrees));
    translation = QVector3D(local(0, 3), local(1, 3), local(2, 3));
    return std::isfinite(translation.x()) && std::isfinite(translation.y()) &&
           std::isfinite(translation.z()) && std::isfinite(rotation.x()) &&
           std::isfinite(rotation.y()) && std::isfinite(rotation.z());
}

void editor_set_local_transform_from_world(
    const std::shared_ptr<Title> &title, Layer &layer,
    const EditorWorldTransformSnapshot &snapshot,
    const std::string &new_group_parent_id,
    const std::string &new_transform_parent_id, double playhead)
{
    Layer parent_probe = layer;
    parent_probe.parent_id = new_group_parent_id;
    parent_probe.transform_parent_id = new_transform_parent_id;
    const bool destination_uses_3d = title &&
        fxm::transform3d::layer_or_ancestor_uses_3d(*title, parent_probe);
    if (!title || !snapshot.has_3d_matrix ||
        (!snapshot.uses_3d && !destination_uses_3d)) {
        editor_set_local_transform_from_world(
            title, layer, snapshot.affine, new_group_parent_id,
            new_transform_parent_id, playhead);
        return;
    }

    bool invertible = false;
    const QMatrix4x4 parent_inverse =
        fxm::transform3d::layer_parent_world_matrix(
            *title, parent_probe, playhead).inverted(&invertible);
    const QMatrix4x4 local = invertible
        ? parent_inverse * snapshot.world_3d : snapshot.world_3d;

    QVector3D matrix_translation, euler, scale;
    if (!editor_decompose_local_3d(local, matrix_translation, euler, scale)) {
        editor_set_local_transform_from_world(
            title, layer, snapshot.affine, new_group_parent_id,
            new_transform_parent_id, playhead);
        return;
    }

    const double local_time = std::clamp(
        playhead - layer.in_time, 0.0,
        std::max(0.0, layer.out_time - layer.in_time));
    const double anchor_z = layer.dimension_mode == LayerDimensionMode::ThreeD
        ? layer.anchor_z.evaluate(local_time) : 0.0;
    const QVector3D linear_z(local(0, 2), local(1, 2), local(2, 2));
    const QVector3D position = matrix_translation +
        linear_z * static_cast<float>(anchor_z);

    const bool requires_local_3d = std::abs(position.z()) > 1.0e-5 ||
        std::abs(euler.x()) > 1.0e-4 || std::abs(euler.y()) > 1.0e-4 ||
        std::abs(scale.z() - 1.0f) > 1.0e-5;
    if (requires_local_3d)
        layer.dimension_mode = LayerDimensionMode::ThreeD;

    layer.parent_id = new_group_parent_id;
    layer.transform_parent_id = new_transform_parent_id;
    if (layer.dimension_mode == LayerDimensionMode::ThreeD) {
        promote_layer_position_to_3d_path(layer);
        set_animated_value(layer.position_3d, local_time,
                           {position.x(), position.y(), position.z()});
        mirror_position_3d_to_legacy(layer, local_time);
    } else {
        Vec2Value next_position = layer.position.evaluate(local_time);
        next_position.x = position.x();
        next_position.y = position.y();
        next_position.z = position.z();
        set_animated_value(layer.position, local_time, next_position);
        set_animated_value(layer.position_z, local_time, position.z());
    }
    Vec2Value next_scale = layer.scale.evaluate(local_time);
    next_scale.x = scale.x();
    next_scale.y = scale.y();
    next_scale.z = scale.z();
    set_animated_value(layer.scale, local_time, next_scale);
    set_animated_value(layer.scale_z, local_time, scale.z());
    set_animated_value(layer.rotation_x, local_time, euler.x());
    set_animated_value(layer.rotation_y, local_time, euler.y());
    set_animated_value(layer.rotation, local_time, euler.z());
    /* The decomposed basis is stored in Rotation XYZ. Orientation stays
     * available for subsequent authoring but is reset to avoid double-rotation. */
    set_animated_value(layer.orientation_x, local_time, 0.0);
    set_animated_value(layer.orientation_y, local_time, 0.0);
    set_animated_value(layer.orientation_z, local_time, 0.0);
}

void editor_set_local_transform_from_world(const std::shared_ptr<Title> &title,
                                           Layer &layer,
                                           const QTransform &world,
                                           const std::string &new_group_parent_id,
                                           const std::string &new_transform_parent_id,
                                           double playhead)
{
    std::set<std::string> visiting;
    const QTransform parent_world = editor_parent_basis_for_parenting(
        title, layer, playhead, new_group_parent_id, new_transform_parent_id,
        visiting, false);
    bool invertible = false;
    const QTransform parent_inverse = parent_world.inverted(&invertible);
    const QTransform local = invertible ? world * parent_inverse : world;

    const double scale_x = std::hypot(local.m11(), local.m12());
    const double determinant = local.determinant();
    double scale_y = scale_x > 1e-9
        ? determinant / scale_x
        : std::hypot(local.m21(), local.m22());
    if (std::abs(scale_y) < 1e-9)
        scale_y = scale_y < 0.0 ? -1e-9 : 1e-9;
    const double rotation = std::atan2(local.m12(), local.m11()) *
                            180.0 / std::acos(-1.0);
    const double local_time = std::clamp(
        playhead - layer.in_time, 0.0,
        std::max(0.0, layer.out_time - layer.in_time));

    layer.parent_id = new_group_parent_id;
    layer.transform_parent_id = new_transform_parent_id;
    editor_set_local_position_xy_for_parenting(
        layer, local_time, QPointF(local.dx(), local.dy()));
    set_animated_x(layer.scale, local_time, scale_x);
    set_animated_y(layer.scale, local_time, scale_y);
    set_animated_value(layer.rotation, local_time, rotation);
}

static bool editor_collapse_constant_track(AnimatedProperty &property)
{
    if (property.keyframes.empty() || editor_scalar_property_varies(property))
        return false;
    property.static_value = property.keyframes.front().value;
    property.keyframes.clear();
    return true;
}

static bool editor_collapse_constant_track(AnimatedVec2Property &property)
{
    if (property.keyframes.empty() || editor_vector_property_varies(property))
        return false;
    property.static_value = property.keyframes.front().value;
    property.keyframes.clear();
    return true;
}

static bool editor_collapse_constant_track(AnimatedVec3Property &property)
{
    if (property.keyframes.empty() || editor_vector3_property_varies(property))
        return false;
    property.static_value = property.keyframes.front().value;
    property.keyframes.clear();
    return true;
}

void editor_restore_world_transform_track_for_parenting(
    const std::shared_ptr<Title> &title, Layer &layer,
    const EditorWorldTransformTrackSnapshot &track,
    const std::string &new_group_parent_id,
    const std::string &new_transform_parent_id, double playhead)
{
    const auto started = std::chrono::steady_clock::now();
    const std::size_t keyframes_before =
        editor_layer_transform_keyframe_count(layer);
    const std::string old_group_parent_id = layer.parent_id;
    const std::string old_transform_parent_id = layer.transform_parent_id;
    if (track.world_samples.empty()) {
        FXM_LOG_WARNING("Grouping", QStringLiteral(
            "Skipped reparent restore layer=%1 reason=empty-world-track")
            .arg(QString::fromStdString(layer.id)));
        return;
    }
    if (!track.multi_sampled || track.world_samples.size() == 1 ||
        track.title_times.size() != track.world_samples.size()) {
        editor_set_local_transform_from_world(
            title, layer, track.world_samples.front(), new_group_parent_id,
            new_transform_parent_id, playhead);
        FXM_LOG_DEBUG("Grouping", QStringLiteral(
            "Restored single-sample reparent layer=%1 oldGroup=%2 newGroup=%3 "
            "oldTransformParent=%4 newTransformParent=%5 "
            "keyframesBefore=%6 keyframesAfter=%7 costUs=%8")
            .arg(QString::fromStdString(layer.id),
                 QString::fromStdString(old_group_parent_id),
                 QString::fromStdString(new_group_parent_id),
                 QString::fromStdString(old_transform_parent_id),
                 QString::fromStdString(new_transform_parent_id))
            .arg(static_cast<qulonglong>(keyframes_before))
            .arg(static_cast<qulonglong>(
                editor_layer_transform_keyframe_count(layer)))
            .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
        return;
    }

    Layer destination_probe = layer;
    destination_probe.parent_id = new_group_parent_id;
    destination_probe.transform_parent_id = new_transform_parent_id;
    const bool use_3d = title &&
        (layer.dimension_mode == LayerDimensionMode::ThreeD ||
         fxm::transform3d::layer_or_ancestor_uses_3d(
             *title, destination_probe) ||
         std::any_of(track.world_samples.begin(), track.world_samples.end(),
             [](const EditorWorldTransformSnapshot &sample) {
                 return sample.uses_3d;
             }));

    layer.parent_id = new_group_parent_id;
    layer.transform_parent_id = new_transform_parent_id;
    if (use_3d) {
        layer.dimension_mode = LayerDimensionMode::ThreeD;
        promote_layer_position_to_3d_path(layer);
        layer.position_3d_path_enabled = true;
        layer.position_3d.keyframes.clear();
        layer.position.keyframes.clear();
        layer.position_z.keyframes.clear();
        layer.rotation_x.keyframes.clear();
        layer.rotation_y.keyframes.clear();
        layer.orientation_x.keyframes.clear();
        layer.orientation_y.keyframes.clear();
        layer.orientation_z.keyframes.clear();
        layer.scale_z.keyframes.clear();
    } else {
        layer.position.keyframes.clear();
    }
    layer.scale.keyframes.clear();
    layer.rotation.keyframes.clear();

    /* Seed each active channel so set_animated_value() writes a complete
     * linear frame sequence instead of collapsing the first sample to a static
     * value. Per-frame baking is used whenever the authored transform or either
     * the source/destination parent basis is animated. */
    const double first_local_time = std::clamp(
        track.title_times.front() - layer.in_time, 0.0,
        std::max(0.0, layer.out_time - layer.in_time));
    if (use_3d) {
        add_or_replace_keyframe(layer.position_3d, first_local_time,
                                layer.position_3d.static_value);
        add_or_replace_keyframe(layer.position_z, first_local_time,
                                layer.position_z.static_value);
        add_or_replace_keyframe(layer.rotation_x, first_local_time,
                                layer.rotation_x.static_value);
        add_or_replace_keyframe(layer.rotation_y, first_local_time,
                                layer.rotation_y.static_value);
        add_or_replace_keyframe(layer.orientation_x, first_local_time,
                                layer.orientation_x.static_value);
        add_or_replace_keyframe(layer.orientation_y, first_local_time,
                                layer.orientation_y.static_value);
        add_or_replace_keyframe(layer.orientation_z, first_local_time,
                                layer.orientation_z.static_value);
        add_or_replace_keyframe(layer.scale_z, first_local_time,
                                layer.scale_z.static_value);
    } else {
        add_or_replace_keyframe(layer.position, first_local_time,
                                layer.position.static_value);
    }
    add_or_replace_keyframe(layer.scale, first_local_time,
                            layer.scale.static_value);
    add_or_replace_keyframe(layer.rotation, first_local_time,
                            layer.rotation.static_value);

    for (size_t index = 0; index < track.world_samples.size(); ++index) {
        editor_set_local_transform_from_world(
            title, layer, track.world_samples[index], new_group_parent_id,
            new_transform_parent_id, track.title_times[index]);
    }
    std::size_t collapsed_tracks = 0;
    if (use_3d) {
        collapsed_tracks += editor_collapse_constant_track(layer.position_3d) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.scale) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.scale_z) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.rotation) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.rotation_x) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.rotation_y) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.orientation_x) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.orientation_y) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.orientation_z) ? 1 : 0;
        layer.position_3d.recalculate_rove_times();
        mirror_position_3d_track_to_legacy(layer);
        collapsed_tracks += editor_collapse_constant_track(layer.position_z) ? 1 : 0;
    } else {
        collapsed_tracks += editor_collapse_constant_track(layer.position) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.scale) ? 1 : 0;
        collapsed_tracks += editor_collapse_constant_track(layer.rotation) ? 1 : 0;
    }
    FXM_LOG_INFO("Grouping", QStringLiteral(
        "Restored sampled reparent layer=%1 name=\"%2\" samples=%3 use3D=%4 "
        "oldGroup=%5 newGroup=%6 oldTransformParent=%7 newTransformParent=%8 "
        "keyframesBefore=%9 keyframesAfter=%10 collapsedTracks=%11 costUs=%12")
        .arg(QString::fromStdString(layer.id),
             QString::fromStdString(layer.name))
        .arg(static_cast<qulonglong>(track.world_samples.size()))
        .arg(use_3d ? 1 : 0)
        .arg(QString::fromStdString(old_group_parent_id),
             QString::fromStdString(new_group_parent_id),
             QString::fromStdString(old_transform_parent_id),
             QString::fromStdString(new_transform_parent_id))
        .arg(static_cast<qulonglong>(keyframes_before))
        .arg(static_cast<qulonglong>(
            editor_layer_transform_keyframe_count(layer)))
        .arg(static_cast<qulonglong>(collapsed_tracks))
        .arg(static_cast<qulonglong>(editor_elapsed_us(started))));
}

std::vector<std::shared_ptr<Layer>> editor_top_level_selected_layers(
    const std::shared_ptr<Title> &title,
    const std::vector<std::string> &ids)
{
    std::vector<std::shared_ptr<Layer>> result;
    if (!title)
        return result;
    const std::set<std::string> selected(ids.begin(), ids.end());
    for (const auto &layer : title->layers) {
        if (!layer || selected.find(layer->id) == selected.end() ||
            layer->linked_media_stream)
            continue;
        bool has_selected_ancestor = false;
        std::string cursor = layer->parent_id;
        int guard = 0;
        while (!cursor.empty() && guard++ < 64) {
            if (selected.find(cursor) != selected.end()) {
                has_selected_ancestor = true;
                break;
            }
            const Layer *parent = editor_layer_by_id_for_parenting(title, cursor);
            if (!parent)
                break;
            cursor = parent->parent_id;
        }
        if (!has_selected_ancestor)
            result.push_back(layer);
    }
    return result;
}

std::vector<std::string> editor_selection_with_group_descendants(
    const std::shared_ptr<Title> &title,
    const std::vector<std::string> &ids)
{
    if (!title)
        return {};
    std::set<std::string> expanded(ids.begin(), ids.end());
    /* Linked audio-stream rows are structural children of their Video layer.
     * They cannot be copied, deleted, grouped, or moved independently.  A
     * selected Video, however, always carries all of its streams. */
    for (auto it = expanded.begin(); it != expanded.end();) {
        const auto layer = title->find_layer(*it);
        if (layer && layer->linked_media_stream &&
            expanded.find(layer->linked_media_layer_id) == expanded.end())
            it = expanded.erase(it);
        else
            ++it;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &layer : title->layers) {
            if (!layer || layer->parent_id.empty() ||
                expanded.find(layer->id) != expanded.end())
                continue;
            auto parent = title->find_layer(layer->parent_id);
            const bool regular_child = parent && layer_type_is_container(parent->type);
            const bool linked_video_stream =
                parent && parent->type == LayerType::Video &&
                layer->type == LayerType::Audio && layer->linked_media_stream &&
                layer->linked_media_layer_id == parent->id;
            if ((regular_child || linked_video_stream) &&
                expanded.find(parent->id) != expanded.end()) {
                expanded.insert(layer->id);
                changed = true;
            }
        }
    }
    std::vector<std::string> ordered;
    ordered.reserve(expanded.size());
    for (const auto &layer : title->layers) {
        if (layer && expanded.find(layer->id) != expanded.end())
            ordered.push_back(layer->id);
    }
    return ordered;
}


std::string editor_group_order_scope(const std::shared_ptr<Title> &title,
                                     const Layer &layer)
{
    if (!title || layer.parent_id.empty()) return {};
    const auto parent = title->find_layer(layer.parent_id);
    if (!parent) return {};
    if (layer_type_is_container(parent->type)) return layer.parent_id;
    if (parent->type == LayerType::Video && layer.type == LayerType::Audio &&
        layer.linked_media_stream && layer.linked_media_layer_id == parent->id)
        return layer.parent_id;
    return {};
}

std::vector<std::shared_ptr<Layer>> editor_canonical_group_order(
    const std::shared_ptr<Title> &title,
    const std::map<std::string, std::vector<std::shared_ptr<Layer>>> &siblings_by_scope)
{
    std::vector<std::shared_ptr<Layer>> ordered;
    if (!title) return ordered;

    std::set<std::string> emitted;
    std::function<void(const std::shared_ptr<Layer> &)> append_subtree;
    append_subtree = [&](const std::shared_ptr<Layer> &layer) {
        if (!layer || !emitted.insert(layer->id).second)
            return;
        const auto children = siblings_by_scope.find(layer->id);
        if (children != siblings_by_scope.end()) {
            for (const auto &child : children->second)
                append_subtree(child);
        }
        ordered.push_back(layer);
    };

    const auto roots = siblings_by_scope.find(std::string());
    if (roots != siblings_by_scope.end()) {
        for (const auto &root : roots->second)
            append_subtree(root);
    }
    for (const auto &layer : title->layers)
        append_subtree(layer);
    return ordered;
}

bool editor_reorder_selected_group_siblings(const std::shared_ptr<Title> &title,
                                            const std::vector<std::string> &selected_ids,
                                            int action)
{
    if (!title || selected_ids.empty() || action == 0)
        return false;
    const auto selected_roots = editor_top_level_selected_layers(title, selected_ids);
    std::set<std::string> moving;
    for (const auto &layer : selected_roots)
        if (layer) moving.insert(layer->id);
    if (moving.empty()) return false;

    std::map<std::string, std::vector<std::shared_ptr<Layer>>> scopes;
    for (const auto &layer : title->layers) {
        if (!layer) continue;
        scopes[editor_group_order_scope(title, *layer)].push_back(layer);
    }

    bool changed = false;
    for (auto &[scope, siblings] : scopes) {
        const bool scope_has_selection = std::any_of(
            siblings.begin(), siblings.end(), [&](const auto &layer) {
                return layer && moving.find(layer->id) != moving.end();
            });
        if (!scope_has_selection) continue;

        auto is_moving = [&](const std::shared_ptr<Layer> &layer) {
            return layer && moving.find(layer->id) != moving.end();
        };
        const auto before = siblings;
        if (action >= 2 || action <= -2) {
            std::vector<std::shared_ptr<Layer>> selected;
            std::vector<std::shared_ptr<Layer>> stationary;
            selected.reserve(siblings.size());
            stationary.reserve(siblings.size());
            for (const auto &layer : siblings)
                (is_moving(layer) ? selected : stationary).push_back(layer);
            siblings.clear();
            if (action >= 2) {
                siblings.insert(siblings.end(), stationary.begin(), stationary.end());
                siblings.insert(siblings.end(), selected.begin(), selected.end());
            } else {
                siblings.insert(siblings.end(), selected.begin(), selected.end());
                siblings.insert(siblings.end(), stationary.begin(), stationary.end());
            }
        } else if (action > 0) {
            for (int i = static_cast<int>(siblings.size()) - 2; i >= 0; --i) {
                if (is_moving(siblings[static_cast<size_t>(i)]) &&
                    !is_moving(siblings[static_cast<size_t>(i + 1)]))
                    std::swap(siblings[static_cast<size_t>(i)],
                              siblings[static_cast<size_t>(i + 1)]);
            }
        } else {
            for (size_t i = 1; i < siblings.size(); ++i) {
                if (is_moving(siblings[i]) && !is_moving(siblings[i - 1]))
                    std::swap(siblings[i], siblings[i - 1]);
            }
        }
        changed = changed || siblings != before;
    }

    if (!changed) return false;
    auto reordered = editor_canonical_group_order(title, scopes);
    if (reordered.size() != title->layers.size())
        return false;
    title->layers = std::move(reordered);
    return true;
}

// Editor-session-only defaults. These intentionally live only in process memory:
// closing/reopening the editor window within the same OBS run keeps the user's
// last foreground/background and new-layer style, but restarting OBS resets them
// to the compiled/project defaults instead of reading/writing QSettings.

namespace {
const QByteArray &fxm_about_graphic_svg()
{
    static const QByteArray source_svg = [] {
        (void)fxm_satoshi_ui_font();

        const QString path = fxm_asset_path("icons/flux-motion-about.svg");
        if (path.isEmpty())
            return QByteArray();

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return QByteArray();

        QByteArray svg = file.readAll();
        // Keep the supplied typography and optical spacing. Only the text
        // inside the three identified lines changes between builds.
        const auto replace_text_content = [&svg](const QByteArray &id,
                                                 const QByteArray &content) {
            const QByteArray marker = "<text id=\"" + id + "\"";
            const qsizetype element_start = svg.indexOf(marker);
            if (element_start < 0)
                return;
            const qsizetype content_start = svg.indexOf('>', element_start);
            if (content_start < 0)
                return;
            const qsizetype content_end = svg.indexOf("</text>", content_start + 1);
            if (content_end < 0)
                return;
            svg.replace(content_start + 1, content_end - content_start - 1,
                        content);
        };

        const QByteArray version_markup =
            (QStringLiteral("<tspan class=\"cls-16\">") +
             QStringLiteral(FXM_VERSION_LABEL).toHtmlEscaped() +
             QStringLiteral("</tspan>"))
                .toUtf8();
        replace_text_content("fxm-version", version_markup);
        replace_text_content("fxm-development-version", QByteArray());
        return svg;
    }();
    return source_svg;
}
}

QPixmap render_fxm_about_graphic_pixmap(const QSize &logical_size,
                                        qreal device_pixel_ratio)
{
    const QByteArray &source_svg = fxm_about_graphic_svg();
    if (source_svg.isEmpty() || logical_size.isEmpty())
        return QPixmap();

    QSvgRenderer renderer(source_svg);
    if (!renderer.isValid())
        return QPixmap();

    const qreal dpr = std::max<qreal>(1.0, device_pixel_ratio);
    const QSize pixel_size(qMax(1, qRound(logical_size.width() * dpr)),
                           qMax(1, qRound(logical_size.height() * dpr)));
    QPixmap pixmap(pixel_size);
    pixmap.fill(Qt::transparent);

    QSizeF artwork_size = renderer.defaultSize();
    if (artwork_size.isEmpty())
        artwork_size = QSizeF(948.81, 363.98);
    artwork_size.scale(QSizeF(pixel_size), Qt::KeepAspectRatio);
    const QRectF target(
        (pixel_size.width() - artwork_size.width()) * 0.5,
        (pixel_size.height() - artwork_size.height()) * 0.5,
        artwork_size.width(), artwork_size.height());

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, target);
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

class FluxMotionAboutGraphic final : public QWidget {
public:
    explicit FluxMotionAboutGraphic(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMinimumSize(520, 200);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        return QSize(760, 292);
    }

    bool hasHeightForWidth() const override
    {
        return true;
    }

    int heightForWidth(int width) const override
    {
        return qMax(1, qRound(width * 363.98 / 948.81));
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (QWidget *top_level = window())
                top_level->close();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        const QRect available = rect().adjusted(8, 8, -8, -8);
        const QPixmap artwork = render_fxm_about_graphic_pixmap(
            available.size(), devicePixelRatioF());
        if (artwork.isNull())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(available, artwork);
    }
};

bool g_editor_session_sidebar_colors_initialized = false;
QColor g_editor_session_foreground_color;
QColor g_editor_session_background_color;
bool g_editor_session_new_layer_defaults_initialized = false;
Layer g_editor_session_new_layer_style;

} // namespace

QPixmap fxm_about_graphic_pixmap(const QSize &logical_size,
                                 qreal device_pixel_ratio)
{
    return render_fxm_about_graphic_pixmap(logical_size,
                                           device_pixel_ratio);
}

TitleEditor::TitleEditor(QWidget *parent)
    : TitleEditor(parent, Qt::Window,
                  fxm::editor::EditorExecutionContext::standalone())
{
}

TitleEditor::TitleEditor(QWidget *parent, Qt::WindowFlags flags)
    : TitleEditor(parent, flags,
                  fxm::editor::EditorExecutionContext::standalone())
{
}

TitleEditor::TitleEditor(
    QWidget *parent, Qt::WindowFlags flags,
    fxm::editor::EditorExecutionContext execution_context)
    : QMainWindow(parent, flags), execution_context_(execution_context)
{
    fxm_apply_brand_icon(this);
    fxm_apply_satoshi_ui_font(this);
    setWindowTitle(fxm_editor_caption(fxm_tr("OBSTitles.EditorWindowTitle")));
    resize(1280, 760);
    setMinimumSize(900, 600);

    /* Dark background */
    setAutoFillBackground(true);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    /* The complete lower edge belongs to the bottom dock area. Nested docks can
     * therefore split it horizontally into bottom-left and bottom-right slots. */
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    build_ui();
    for (QTabWidget *tabs : findChildren<QTabWidget *>())
        tabs->setTabPosition(QTabWidget::North);
    for (QWidget *field : findChildren<QWidget *>())
        fxm_apply_editor_field_style(field);
    const QPointer<TitleEditor> external_data_guard(this);
    external_data_callback_id_ =
        ExternalDataManager::instance().on_change(
            [external_data_guard](const ExternalDataUpdate &update) {
                if (!external_data_guard)
                    return;
                QTimer::singleShot(0, external_data_guard,
                    [external_data_guard, update]() {
                        if (!external_data_guard ||
                            external_data_guard->shutting_down_ ||
                            !external_data_guard->title_ ||
                            !title_uses_external_update(
                                *external_data_guard->title_, update))
                            return;
                        const QString next_visual_hash =
                            CacheManager::instance().contentHashForTitle(
                                *external_data_guard->title_);
                        if (!external_data_guard->external_data_visual_hash_.isEmpty() &&
                            external_data_guard->external_data_visual_hash_ ==
                                next_visual_hash)
                            return;
                        external_data_guard->external_data_visual_hash_ =
                            next_visual_hash;
                        if (external_data_guard->canvas_)
                            external_data_guard->canvas_->refresh_preview();
                    });
            });
    load_sidebar_default_colors();
    load_new_layer_defaults();
    update_sidebar_color_swatches(nullptr);

    play_timer_ = new QTimer(this);
    play_timer_->setTimerType(Qt::PreciseTimer);
    // Transport is always driven by the project/OBS frame rate. It must not
    // change when the editor is moved between monitors with different refresh rates.
    play_timer_->setInterval(editor_playback_ui_timer_interval_ms());
    connect(play_timer_, &QTimer::timeout, this, &TitleEditor::tick);

    gui_refresh_timer_ = new QTimer(this);
    gui_refresh_timer_->setTimerType(Qt::PreciseTimer);
    gui_refresh_timer_->setInterval(16);
    connect(gui_refresh_timer_, &QTimer::timeout, this, [this]() {
        // This timer is interaction-only. Never leave a precise monitor-rate
        // timer running while the editor is idle.
        const bool pointer_drag = QApplication::mouseButtons() != Qt::NoButton;
        if (!pointer_drag || dock_layout_transition_ || playing_ ||
            !isVisible() || isMinimized() || !isActiveWindow()) {
            gui_refresh_timer_->stop();
            return;
        }
        if (canvas_ && canvas_->underMouse())
            canvas_->update();
        if (timeline_ && timeline_->underMouse())
            timeline_->update();
    });

    layout_settle_timer_ = new QTimer(this);
    layout_settle_timer_->setSingleShot(true);
    layout_settle_timer_->setInterval(90);
    connect(layout_settle_timer_, &QTimer::timeout, this, [this]() {
        dock_layout_transition_ = false;
        update_display_refresh_pacing();
        if (canvas_ && isVisible() && !isMinimized())
            canvas_->update();
    });

    cache_invalidation_timer_ = new QTimer(this);
    cache_invalidation_timer_->setSingleShot(true);
    cache_invalidation_timer_->setInterval(180);
    connect(cache_invalidation_timer_, &QTimer::timeout, this, [this]() {
        CacheManager::instance().setInteractiveBypass(false);
        if (!title_)
            return;
        CacheManager::instance().invalidateAll(title_);
        CacheManager::instance().reprioritize(title_, playhead_);
        /* Editing invalidates every affected frame. Restore the editor's
         * full-timeline prerender intent instead of leaving only a small
         * playhead neighbourhood queued. */
        CacheManager::instance().queueWholeTimeline(title_);
        if (timeline_) timeline_->update();
    });

    status_activity_timer_ = new QTimer(this);
    status_activity_timer_->setSingleShot(true);
    connect(status_activity_timer_, &QTimer::timeout, this,
            &TitleEditor::update_statusbar_autosave_summary);

    /* Inline typing owns its canvas refresh already. Rebuilding the complete
     * Properties panel, Timeline model and persisted live title on every
     * character was a much larger cost than shaping the new glyph itself.
     * Keep live output responsive, but coalesce those secondary consumers. */
    inline_text_panel_refresh_timer_ = new QTimer(this);
    inline_text_panel_refresh_timer_->setSingleShot(true);
    inline_text_panel_refresh_timer_->setInterval(120);
    connect(inline_text_panel_refresh_timer_, &QTimer::timeout, this, [this]() {
        if (shutting_down_ || !title_ ||
            pending_inline_text_refresh_layer_id_.empty())
            return;
        const std::string layer_id = pending_inline_text_refresh_layer_id_;
        pending_inline_text_refresh_layer_id_.clear();
        if (active_text_edit_layer_id_ != layer_id)
            return;
        if (auto layer = title_->find_layer(layer_id))
            update_layer_panels(layer, playhead_);
    });

    inline_text_live_publish_timer_ = new QTimer(this);
    inline_text_live_publish_timer_->setSingleShot(true);
    inline_text_live_publish_timer_->setInterval(40);
    connect(inline_text_live_publish_timer_, &QTimer::timeout, this, [this]() {
        if (!shutting_down_ && live_editing_ && title_ &&
            !active_text_edit_layer_id_.empty())
            save_live_edit();
    });

    autosave_timer_ = new QTimer(this);
    autosave_timer_->setSingleShot(false);
    connect(autosave_timer_, &QTimer::timeout, this, &TitleEditor::perform_autosave);
    update_autosave_timer();

    clock_timer_ = new QTimer(this);
    // Runtime content continues while the title transport is stopped. Clocks
    // stay on the inexpensive 10 Hz cadence; independent animated assets use
    // the editor playback cadence so their motion is not coupled to Pause.
    clock_timer_->setInterval(100);
    connect(clock_timer_, &QTimer::timeout, this, [this]() {
        const bool independent_asset = title_ &&
            fxm::asset_runtime::title_has_independent_playback(*title_);
        const int target_interval = independent_asset
            ? std::max(1, editor_playback_ui_timer_interval_ms())
            : 100;
        if (clock_timer_->interval() != target_interval)
            clock_timer_->setInterval(target_interval);
        if (!playing_ && !dock_layout_transition_ && canvas_ &&
            isVisible() && !isMinimized() && isActiveWindow() &&
            title_has_dynamic_text_layer(title_))
            canvas_->refresh_runtime_dynamic_content();
    });
    clock_timer_->start();

    qApp->installEventFilter(this);
    QTimer::singleShot(0, this, &TitleEditor::update_display_refresh_pacing);
}

TitleEditor::~TitleEditor()
{
    begin_shutdown();
    save_sidebar_default_colors();
    save_new_layer_defaults();
    save_editor_layout();
}

void TitleEditor::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (initial_visible_layout_applied_)
        return;
    initial_visible_layout_applied_ = true;

    /* Home -> New/Recent restores this editor while its stacked page is still
     * hidden. Wait for the visible dock layout, restore its pixel dimensions,
     * then fit both views against their final viewport geometry. */
    QTimer::singleShot(0, this, [this]() {
        restore_editor_dock_sizes();
        QTimer::singleShot(0, this, [this]() {
            if (canvas_)
                canvas_->fit_canvas(true);
            if (timeline_)
                timeline_->fit_timeline();
        });
    });
}

void TitleEditor::begin_shutdown()
{
    if (shutting_down_)
        return;
    shutting_down_ = true;
    if (external_data_callback_id_ != 0) {
        ExternalDataManager::instance().remove_change_callback(
            external_data_callback_id_);
        external_data_callback_id_ = 0;
    }
    playing_ = false;
    release_editor_audio_preview();
    if (canvas_) canvas_->set_playback_active(false);

    if (qApp)
        qApp->removeEventFilter(this);

    const auto stop_timer = [](QTimer *timer) {
        if (timer)
            timer->stop();
    };
    stop_timer(play_timer_);
    stop_timer(gui_refresh_timer_);
    stop_timer(layout_settle_timer_);
    stop_timer(clock_timer_);
    stop_timer(cache_invalidation_timer_);
    stop_timer(autosave_timer_);
    stop_timer(status_activity_timer_);
    stop_timer(inline_text_panel_refresh_timer_);
    stop_timer(inline_text_live_publish_timer_);
    pending_inline_text_refresh_layer_id_.clear();
    for (QTimer *timer : findChildren<QTimer *>())
        timer->stop();

    /* Preserve QObject::destroyed/title_saved observers owned by TitleDock,
     * but stop every child/timer sender from entering TitleEditor slots during
     * the remainder of teardown. */
    const auto child_objects = findChildren<QObject *>();
    for (QObject *child : child_objects)
        disconnect(child, nullptr, this, nullptr);

    /* Tear down the cross-dock signal path before either endpoint begins Qt
     * child destruction.  This prevents queued/final value notifications from
     * rebuilding effect handles while the native OBS canvas display is being
     * removed. */
    if (effects_panel_ && canvas_) {
        disconnect(effects_panel_, nullptr, canvas_, nullptr);
        disconnect(canvas_, nullptr, effects_panel_, nullptr);
    }
    if (effects_panel_)
        effects_panel_->begin_shutdown();
    if (canvas_)
        canvas_->prepare_for_shutdown();

    CacheManager::instance().setInteractiveBypass(false);
    if (title_ && !title_->id.empty())
        CacheManager::instance().cancelTitleWork(
            QString::fromStdString(title_->id));
    CacheManager::instance().setEditorPrerenderFocus(QString(), false);
}




QWidget *TitleEditor::create_effects_panel()
{
    effects_panel_ = new EffectsPanel(this);
    effects_panel_->set_title(title_);
    connect(effects_panel_, &EffectsPanel::property_changed,
            this, [this](bool push_undo_snapshot) {
                if (updating_layer_panels_)
                    return;
                // Every EffectsPanel notification represents a real visual-model edit.
                // Bypass the selection-only visual hash guard so enable/disable and
                // stack changes always invalidate prerender/frame caches immediately.
                force_next_title_visual_update();
                on_title_modified(push_undo_snapshot);
                if (layers_) layers_->refresh();
            });
    connect(effects_panel_, &EffectsPanel::keyframe_navigation_requested,
            this, &TitleEditor::on_playhead_changed);

    /* Dock panels are constructed after the central canvas.  Connecting only
     * from the canvas-construction branch therefore left these signals entirely
     * disconnected in the normal editor startup order. */
    if (canvas_) {
        connect(effects_panel_, &EffectsPanel::extension_canvas_handles_changed,
                canvas_, &CanvasPreview::set_extension_canvas_handles,
                Qt::UniqueConnection);
        connect(canvas_, &CanvasPreview::extension_canvas_handle_moved,
                effects_panel_, &EffectsPanel::set_extension_canvas_handle_position,
                Qt::UniqueConnection);
        canvas_->set_extension_canvas_handles(effects_panel_->extension_canvas_handles());
    }
    return effects_panel_;
}
