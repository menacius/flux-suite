        const QSize preview_size = image.size().scaled(QSize(240, 180), Qt::KeepAspectRatio);
        preview_tooltip_ = QStringLiteral(
            "<div style=\"white-space:nowrap;\"><b>%1</b><br/>%2 x %3<br/>"
            "<img src=\"%4\" width=\"%5\" height=\"%6\"/></div>")
            .arg(info.fileName().toHtmlEscaped())
            .arg(image.width())
            .arg(image.height())
            .arg(QUrl::fromLocalFile(info.absoluteFilePath()).toString().toHtmlEscaped())
            .arg(preview_size.width())
            .arg(preview_size.height());
        setToolTip(preview_tooltip_);
        if (label_) label_->setToolTip(preview_tooltip_);
    }

    QLabel *label_ = nullptr;
    QToolButton *button_ = nullptr;
    QToolButton *clear_button_ = nullptr;
    QString path_;
    QString preview_tooltip_;
    bool preview_tooltip_dirty_ = true;
    bool read_only_ = false;
};

class LiveTextCueTable : public QTableWidget {
public:
    explicit LiveTextCueTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
        setMouseTracking(false);
        viewport()->setMouseTracking(false);
    }

    std::function<QColor(int)> cue_row_border_color;
    std::function<void()> layout_requested;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTableWidget::resizeEvent(event);
        if (layout_requested)
            layout_requested();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (event && event->buttons() == Qt::NoButton)
            return;
        QTableWidget::mouseMoveEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTableWidget::paintEvent(event);

        if (!cue_row_border_color || rowCount() <= 0)
            return;

        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (int row = 0; row < rowCount(); ++row) {
            if (isRowHidden(row))
                continue;
            const QColor color = cue_row_border_color(row);
            if (!color.isValid() || color.alpha() == 0)
                continue;

            const int y = rowViewportPosition(row);
            const int h = rowHeight(row);
            if (y + h < 0 || y > viewport()->height())
                continue;

            QColor border = color;
            border.setAlpha(220);
            painter.setPen(QPen(border, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRect(1, y + 1, viewport()->width() - 3, h - 3));
        }
    }
};

class TitleListWidget : public QListWidget {
public:
    explicit TitleListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setAcceptDrops(true);
        setDragEnabled(false);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDefaultDropAction(Qt::CopyAction);
        setDropIndicatorShown(false);
        setDragDropOverwriteMode(false);

        if (viewport()) {
            viewport()->setAcceptDrops(true);
            viewport()->installEventFilter(this);
        }
    }

    std::function<void()> delete_requested;
    std::function<void(const QStringList &)> files_dropped;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event && event->key() == Qt::Key_Delete && hasFocus() && !selectedItems().empty()) {
            if (delete_requested)
                delete_requested();
            event->accept();
            return;
        }

        QListWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (handle_drag_enter_or_move(event))
            return;
        QListWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (handle_drag_enter_or_move(event))
            return;
        QListWidget::dragMoveEvent(event);
    }

    void dragLeaveEvent(QDragLeaveEvent *event) override
    {
        set_drag_import_active(false);
        QListWidget::dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (handle_drop(event))
            return;
        QListWidget::dropEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == viewport() && event) {
            switch (event->type()) {
            case QEvent::DragEnter:
                if (handle_drag_enter_or_move(static_cast<QDragEnterEvent *>(event)))
                    return true;
                break;
            case QEvent::DragMove:
                if (handle_drag_enter_or_move(static_cast<QDragMoveEvent *>(event)))
                    return true;
                break;
            case QEvent::DragLeave:
                set_drag_import_active(false);
                break;
            case QEvent::Drop:
                if (handle_drop(static_cast<QDropEvent *>(event)))
                    return true;
                break;
            default:
                break;
            }
        }
        return QListWidget::eventFilter(watched, event);
    }

private:
    template <typename DragEvent>
    bool handle_drag_enter_or_move(DragEvent *event)
    {
        const QStringList paths = accepted_template_paths(event ? event->mimeData() : nullptr);
        if (paths.isEmpty()) {
            if (event) event->ignore();
            set_drag_import_active(false);
            return false;
        }

        set_drag_import_active(true);
        if (event) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        }
        return true;
    }

    bool handle_drop(QDropEvent *event)
    {
        const QStringList paths = accepted_template_paths(event ? event->mimeData() : nullptr);
        set_drag_import_active(false);
        if (paths.isEmpty()) {
            if (event) event->ignore();
            return false;
        }

        if (files_dropped)
            files_dropped(paths);

        if (event) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        }
        return true;
    }

    static bool is_supported_template_file(const QFileInfo &info)
    {
        if (!info.exists() || !info.isFile() || !info.isReadable())
            return false;

        const QString suffix = info.suffix().toLower();
        return suffix == QStringLiteral("fxmp") ||
               suffix == QStringLiteral("fxmt") ||
               suffix == QStringLiteral("fxmproj") ||
               suffix == QStringLiteral("json");
    }

    static void append_path_if_supported(QStringList &paths, const QString &raw_path)
    {
        const QString cleaned = raw_path.trimmed();
        if (cleaned.isEmpty())
            return;

        const QFileInfo info(cleaned);
        if (is_supported_template_file(info))
            paths.push_back(info.absoluteFilePath());
    }

    static QStringList accepted_template_paths(const QMimeData *mime)
    {
        QStringList paths;
        if (!mime)
            return paths;

        if (mime->hasUrls()) {
            const auto urls = mime->urls();
            for (const QUrl &url : urls) {
                if (!url.isLocalFile())
                    continue;
                append_path_if_supported(paths, url.toLocalFile());
            }
        }

        // Some external apps expose file paths as plain text rather than text/uri-list.
        if (paths.isEmpty() && mime->hasText()) {
            const QStringList lines = mime->text().split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                QString candidate = line.trimmed();
                if (candidate.startsWith(QStringLiteral("file:///"))) {
                    const QUrl url(candidate);
                    if (url.isLocalFile())
                        candidate = url.toLocalFile();
                }
                append_path_if_supported(paths, candidate);
            }
        }

        paths.removeDuplicates();
        return paths;
    }

    void set_drag_import_active(bool active)
    {
        if (drag_import_active_ == active)
            return;
        drag_import_active_ = active;
        setProperty("obsFxmTitleImportDropActive", active);
        if (viewport())
            viewport()->setProperty("obsFxmTitleImportDropActive", active);
        if (active) {
            if (saved_style_sheet_.isNull())
                saved_style_sheet_ = styleSheet();
            setStyleSheet(saved_style_sheet_ + QStringLiteral("\nQListWidget { border: 1px solid palette(highlight); }") );
        } else if (!saved_style_sheet_.isNull()) {
            setStyleSheet(saved_style_sheet_);
        }
    }

    bool drag_import_active_ = false;
    QString saved_style_sheet_;
};

class LiveTextCueHeader : public QHeaderView {
public:
    explicit LiveTextCueHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setSectionsMovable(true);
        setSectionsClickable(true);
        setSectionResizeMode(QHeaderView::Interactive);
    }

    void set_select_all_checked(bool checked)
    {
        if (select_all_checked_ == checked) return;
        select_all_checked_ = checked;
        viewport()->update();
    }

    void set_select_all_visible(bool visible)
    {
        if (select_all_visible_ == visible) return;
        select_all_visible_ = visible;
        viewport()->update();
    }

    std::function<void(bool)> select_all_toggled;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override
    {
        QHeaderView::paintSection(painter, rect, logicalIndex);
        if (logicalIndex != kLiveSelectColumn || !select_all_visible_) return;

        const QRect indicator = checkbox_rect(rect).adjusted(1, 1, -1, -1);
        const QPalette header_palette = palette();
        const QColor fill = select_all_checked_
            ? header_palette.color(QPalette::Highlight)
            : header_palette.color(QPalette::Base);
        const QColor outline = header_palette.color(QPalette::Mid);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(outline, 1));
        painter->setBrush(fill);
        painter->drawRoundedRect(indicator, 2, 2);
        if (select_all_checked_) {
            QPen check_pen(header_palette.color(QPalette::WindowText), 1.5,
                           Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(check_pen);
            const QPointF left(indicator.left() + indicator.width() * 0.22,
                               indicator.top() + indicator.height() * 0.52);
            const QPointF middle(indicator.left() + indicator.width() * 0.43,
                                 indicator.top() + indicator.height() * 0.72);
            const QPointF right(indicator.left() + indicator.width() * 0.80,
                                indicator.top() + indicator.height() * 0.28);
            painter->drawPolyline(QPolygonF() << left << middle << right);
        }
        painter->restore();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (select_all_visible_ && event && event->button() == Qt::LeftButton &&
            logicalIndexAt(event->pos()) == kLiveSelectColumn) {
            const QRect section_rect(sectionViewportPosition(kLiveSelectColumn), 0, sectionSize(kLiveSelectColumn), height());
            if (checkbox_rect(section_rect).contains(event->pos())) {
                select_all_checked_ = !select_all_checked_;
                viewport()->update();
                if (select_all_toggled)
                    select_all_toggled(select_all_checked_);
                return;
            }
        }

        QHeaderView::mousePressEvent(event);
    }

private:
    QRect checkbox_rect(const QRect &section_rect) const
    {
        const int indicator_width = style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this);
        const int indicator_height = style()->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, this);
        return QRect(section_rect.x() + (section_rect.width() - indicator_width) / 2,
                     section_rect.y() + (section_rect.height() - indicator_height) / 2,
                     indicator_width,
                     indicator_height);
    }

    bool select_all_checked_ = false;
    bool select_all_visible_ = true;
};

static LiveTextCueHeader *live_text_cue_header(QTableWidget *table)
{
    return table ? dynamic_cast<LiveTextCueHeader *>(table->horizontalHeader()) : nullptr;
}

class TemplateListWidget : public QListWidget {
public:
    explicit TemplateListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(false);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::MoveAction);
    }

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        auto *mime = new QMimeData();
        QList<QUrl> urls;
        for (const auto *item : items) {
            if (!item) continue;
            const QString path = item->data(Qt::UserRole).toString();
            if (!path.isEmpty())
                urls << QUrl::fromLocalFile(path);
        }
        mime->setUrls(urls);
        return mime;
    }

    void startDrag(Qt::DropActions supported_actions) override
    {
        (void)supported_actions;
        const auto items = selectedItems();
        if (items.empty())
            return;

        std::unique_ptr<QMimeData> mime(mimeData(items));
        if (!mime || !mime->hasUrls())
            return;

        auto *drag = new QDrag(this);
        drag->setMimeData(mime.release());
        if (auto *item = currentItem()) {
            QPixmap pixmap = item->icon().pixmap(iconSize());
            if (!pixmap.isNull())
                drag->setPixmap(pixmap);
        }
        drag->exec(Qt::MoveAction);
    }
};

class TemplateCategoryTree : public QTreeWidget {
public:
    explicit TemplateCategoryTree(QWidget *parent = nullptr)
        : QTreeWidget(parent)
    {
        setAcceptDrops(true);
        viewport()->setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DropOnly);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
    }

    std::function<void()> templates_moved;
    std::function<void(QTreeWidgetItem *)> category_delete_requested;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (accept_template_drag(event))
            return;
        QTreeWidget::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (accept_template_drag(event))
            return;
        QTreeWidget::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (!event || !event->mimeData() || !event->mimeData()->hasUrls()) {
            QTreeWidget::dropEvent(event);
            return;
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto *target = itemAt(event->position().toPoint());
#else
        auto *target = itemAt(event->pos());
#endif
        if (!target)
            target = currentItem();
        if (!target) return;

        const QString target_dir = target->data(0, Qt::UserRole).toString();
        if (target_dir.isEmpty()) return;
        QDir().mkpath(target_dir);

        bool moved_any = false;
        for (const QUrl &url : event->mimeData()->urls()) {
            const QString source_path = url.toLocalFile();
            if (source_path.isEmpty()) continue;
            QFileInfo source_info(source_path);
            if (!source_info.exists() || !source_info.isFile()) continue;
            if (QDir::cleanPath(source_info.absolutePath()) == QDir::cleanPath(target_dir)) continue;

            QString dest_path = QDir(target_dir).filePath(source_info.fileName());
            if (QFileInfo::exists(dest_path)) {
                const QString base = source_info.completeBaseName();
                const QString suffix = source_info.suffix().isEmpty() ? QString() : QStringLiteral(".") + source_info.suffix();
                int copy = 2;
                do {
                    dest_path = QDir(target_dir).filePath(QStringLiteral("%1 %2%3").arg(base).arg(copy++).arg(suffix));
                } while (QFileInfo::exists(dest_path));
            }

            if (move_template_file(source_path, dest_path))
                moved_any = true;
        }

        if (moved_any && templates_moved)
            templates_moved();
        accept_template_drag(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (category_delete_requested) {
            if (auto *item = delete_item_at(event ? event->pos() : QPoint())) {
            setCurrentItem(item);
            if (category_delete_requested)
                category_delete_requested(item);
                return;
            }
        }
        QTreeWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (category_delete_requested && delete_item_at(event ? event->pos() : QPoint()))
            return;
        QTreeWidget::mouseDoubleClickEvent(event);
    }

private:
    QTreeWidgetItem *delete_item_at(const QPoint &pos) const
    {
        auto *item = itemAt(pos);
        if (!item)
            return nullptr;

        const QRect rect = visualRect(indexFromItem(item, 0));
        const int delete_width = fontMetrics().horizontalAdvance(QStringLiteral("×")) + 12;
        return QRect(rect.left(), rect.top(), delete_width, rect.height()).contains(pos) ? item : nullptr;
    }

    template <typename DragEvent>
    bool accept_template_drag(DragEvent *event)
    {
        if (!event || !event->mimeData() || !event->mimeData()->hasUrls())
            return false;

        if (event->possibleActions() & Qt::MoveAction) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else {
            event->acceptProposedAction();
        }
        return true;
    }

    static bool move_template_file(const QString &source_path, const QString &dest_path)
    {
        if (QFile::rename(source_path, dest_path))
            return true;

        if (!QFile::copy(source_path, dest_path))
            return false;

        if (QFile::remove(source_path))
            return true;

        QFile::remove(dest_path);
        return false;
    }
};

static double title_export_screenshot_time(const Title &title)
{
    const double source_time = title.playback_mode == 2 ? title.pause_time : title.duration * 0.5;
    return std::clamp(source_time, 0.0, std::max(0.0, title.duration));
}

static QImage title_screenshot_image(const Title &title)
{
    return render_title_to_image(title, title_export_screenshot_time(title));
}

static QIcon title_cached_screenshot_icon(const Title &title, const QSize &size)
{
    if (title.preview_screenshot_png_base64.empty())
        return QIcon();

    const QByteArray png = QByteArray::fromBase64(
        QByteArray(title.preview_screenshot_png_base64.data(),
                   (int)title.preview_screenshot_png_base64.size()));
    QPixmap pixmap;
    if (!pixmap.loadFromData(png, "PNG"))
        return QIcon();

    return QIcon(pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

static bool title_has_exposed_text(const std::shared_ptr<Title> &title)
{
    return !exposed_text_layers(title).empty();
}

static bool title_has_scene_mask(const std::shared_ptr<Title> &title)
{
    if (!title) return false;
    for (const auto &layer : title->layers) {
        if (layer && layer->use_as_scene_mask && layer_type_can_be_scene_mask(layer->type))
            return true;
    }
    return false;
}

static bool title_has_external_data_connection(const Title &title)
{
    if (!title.external_data_enabled)
        return false;
    for (const auto &source : title.external_data_sources) {
        if (source.provider.enabled)
            return true;
    }
    return !title.external_data_sources.empty();
}

static QIcon title_list_status_icon(const Title &title, bool, bool)
{
    const char *icon_name = "graphic.svg";
    switch (title.graphic_type) {
    case TitleGraphicType::Title:   icon_name = "text.svg"; break;
    case TitleGraphicType::Graphic: icon_name = "graphic.svg"; break;
    case TitleGraphicType::Mask:    icon_name = "mask.svg"; break;
    case TitleGraphicType::Stinger: icon_name = "stinger.svg"; break;
    }
    /* Graphic-type badges are part of Flux Motion's document language, not
     * generic host actions. Keep the authored Title/Graphic/Mask/Stinger
     * artwork even when an OBS resource happens to share a filename. */
    return fxm_icon(icon_name);
}

static void paint_playlist_badge(QPainter &painter, const QRect &rect,
                                 bool draw_background = true,
                                 const QColor &icon_color = QColor(255, 255, 255))
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (draw_background) {
        painter.setBrush(QColor(39, 186, 103));
        painter.setPen(QPen(QColor(0, 0, 0, 120), 1));
        painter.drawRoundedRect(rect, 4, 4);
    }
    const QPixmap play = obs_icon("play.svg", icon_color).pixmap(QSize(12, 12));
    if (!play.isNull())
        painter.drawPixmap(rect.center() - QPoint(play.width() / 2, play.height() / 2), play);
    painter.restore();
}

static QPixmap title_preview_pixmap(const Title &title, const QSize &size)
{
    QPixmap pixmap;

    if (!title.preview_screenshot_png_base64.empty()) {
        const QByteArray png = QByteArray::fromBase64(
            QByteArray(title.preview_screenshot_png_base64.data(),
                       (int)title.preview_screenshot_png_base64.size()));
        pixmap.loadFromData(png, "PNG");
    }

    if (pixmap.isNull()) {
        const QImage screenshot = title_screenshot_image(title);
        if (!screenshot.isNull())
            pixmap = QPixmap::fromImage(screenshot);
    }

    if (pixmap.isNull())
        return QPixmap();

    return pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

static void paint_transparency_grid(QPainter &painter, const QRect &rect, int cell = 8)
{
    const bool dark_theme = QApplication::palette().color(QPalette::Window).lightness() < 128;
    const QColor light = dark_theme ? QColor(74, 74, 74) : QColor(224, 224, 224);
    const QColor dark = dark_theme ? QColor(48, 48, 48) : QColor(184, 184, 184);
    for (int y = rect.top(); y <= rect.bottom(); y += cell) {
        for (int x = rect.left(); x <= rect.right(); x += cell) {
            const bool alternate = (((x - rect.left()) / cell) + ((y - rect.top()) / cell)) % 2;
            painter.fillRect(QRect(x, y, cell, cell).intersected(rect), alternate ? dark : light);
        }
    }
}

static QString elide_to_two_lines(const QString &text, const QFontMetrics &fm, int width)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty())
        return simplified;

    const QString first_try = fm.elidedText(simplified, Qt::ElideRight, width);
    if (first_try == simplified)
        return simplified;

    QString first;
    QString rest = simplified;
    const QStringList words = simplified.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() > 1) {
        for (const QString &word : words) {
            const QString candidate = first.isEmpty() ? word : first + QLatin1Char(' ') + word;
            if (fm.horizontalAdvance(candidate) > width)
                break;
            first = candidate;
        }
        if (!first.isEmpty())
            rest = simplified.mid(first.length()).trimmed();
    }

    if (first.isEmpty()) {
        for (int i = 1; i <= simplified.size(); ++i) {
            const QString candidate = simplified.left(i);
            if (fm.horizontalAdvance(candidate) > width)
                break;
            first = candidate;
        }
        rest = simplified.mid(first.length()).trimmed();
    }

    if (rest.isEmpty())
        return first;

    return first + QLatin1Char(' ') + fm.elidedText(rest, Qt::ElideRight, width);
}

static QIcon title_icon_view_icon(const Title &title, bool has_exposed_text, bool has_scene_mask,
                                  const LiveCueAggregateCacheStatus &cache_status,
                                  bool playlist_active, bool show_cache)
{
    const QSize thumb_size(kTitleIconViewThumbWidth, kTitleIconViewThumbHeight);
    QPixmap composed(thumb_size);
    composed.fill(Qt::transparent);

    QPainter painter(&composed);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect full_rect(QPoint(0, 0), thumb_size);
    paint_transparency_grid(painter, full_rect, 8);

    const QPixmap preview = title_preview_pixmap(title, thumb_size);
    if (!preview.isNull()) {
        const QPoint preview_pos((thumb_size.width() - preview.width()) / 2,
                                 (thumb_size.height() - preview.height()) / 2);
        painter.drawPixmap(preview_pos, preview);
    }

    painter.setPen(QPen(QColor(0, 0, 0, 90), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(full_rect.adjusted(0, 0, -1, -1));

    const QPixmap type_pixmap = title_list_status_icon(title, has_exposed_text, has_scene_mask).pixmap(QSize(18, 18));
    QRect badge_rect(5, 5, 24, 24);
    if (!type_pixmap.isNull())
        painter.drawPixmap(badge_rect.center() - QPoint(type_pixmap.width() / 2, type_pixmap.height() / 2), type_pixmap);

    const QPixmap cache_pixmap = obs_icon(live_cue_cache_icon_name(cache_status.state, cache_status.progress_percent),
                                          live_cue_cache_color(cache_status.state)).pixmap(QSize(18, 18));
    QRect cache_badge_rect(thumb_size.width() - 29, 5, 24, 24);
    if (show_cache && !cache_pixmap.isNull())
        painter.drawPixmap(cache_badge_rect.center() - QPoint(cache_pixmap.width() / 2, cache_pixmap.height() / 2), cache_pixmap);
    if (title_has_external_data_connection(title)) {
        const QPixmap globe = obs_icon("globe.svg").pixmap(QSize(18, 18));
        if (!globe.isNull())
            painter.drawPixmap(7, thumb_size.height() - globe.height() - 7, globe);
    }
    if (playlist_active)
        paint_playlist_badge(painter, QRect(thumb_size.width() - 29, thumb_size.height() - 29, 24, 24), false);
    painter.end();

    return QIcon(composed);
}

class TitleIconViewDelegate : public QStyledItemDelegate {
public:
    explicit TitleIconViewDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto *list_view = qobject_cast<const QListView *>(parent());
        if (list_view && list_view->viewMode() == QListView::ListMode) {
            int available_width = list_view->viewport()
                ? list_view->viewport()->contentsRect().width()
                : option.rect.width();
            if (available_width <= 0)
                available_width = option.rect.width();
            return QSize(std::max(0, available_width), 44);
        }
        return QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!painter)
            return;

        const auto *list_view = qobject_cast<const QListView *>(option.widget);
        if (list_view && list_view->viewMode() == QListView::ListMode) {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            opt.text.clear();
            opt.icon = QIcon();
            const QWidget *widget = option.widget;
            QStyle *style = widget ? widget->style() : QApplication::style();
            style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

            const QRect row = option.rect.adjusted(4, 3, -4, -3);
            const QSize thumb_size(64, 36);
            const QRect thumb_rect(row.left(), row.center().y() - thumb_size.height() / 2,
                                   thumb_size.width(), thumb_size.height());
            paint_transparency_grid(*painter, thumb_rect, 6);
            const QPixmap thumb = index.data(kTitleListThumbnailRole).value<QPixmap>();
            if (!thumb.isNull()) {
                const QPixmap scaled = thumb.scaled(thumb_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter->drawPixmap(thumb_rect.center() - QPoint(scaled.width() / 2, scaled.height() / 2), scaled);
            }
            painter->setPen(QPen(QColor(0, 0, 0, 90), 1));
            painter->drawRect(thumb_rect.adjusted(0, 0, -1, -1));

            const QRect text_rect(thumb_rect.right() + 10, row.top(),
                                  std::max(0, row.right() - thumb_rect.right() - 52), row.height());
            painter->setPen(option.palette.color((option.state & QStyle::State_Selected)
                                                     ? QPalette::HighlightedText : QPalette::Text));
            painter->drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                              option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(),
                                                            Qt::ElideRight, text_rect.width()));

            if (index.data(kTitleShowStatusIconRole).toBool()) {
                const QIcon status_icon = index.data(Qt::DecorationRole).value<QIcon>();
                const QPixmap status = status_icon.pixmap(QSize(kTitleListIconExtent * 3 + 6, kTitleListIconExtent));
                if (!status.isNull())
                    painter->drawPixmap(row.right() - status.width() - 4,
                                        row.center().y() - status.height() / 2, status);
            }
            const auto cue_state = static_cast<TitleCueVisualState>(
                index.data(kTitleCueVisualStateRole).toInt());
            const QColor cue_color = title_cue_visual_color(cue_state);
            if (cue_color.isValid())
                paint_cue_state_border(painter, thumb_rect.adjusted(1, 1, -1, -1), 3, cue_color);
            if (index.data(kTitlePlaylistActiveRole).toBool())
                paint_playlist_badge(*painter, QRect(thumb_rect.right() - 22, thumb_rect.bottom() - 22, 20, 20), false);
            return;
        }

        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        opt.text.clear();
        opt.icon = QIcon();

        const QWidget *widget = option.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, widget);

        const QRect item_rect = option.rect.adjusted(6, 6, -6, -6);
        const int thumb_x = item_rect.x() + (item_rect.width() - kTitleIconViewThumbWidth) / 2;
        const QRect thumb_rect(thumb_x, item_rect.y(), kTitleIconViewThumbWidth, kTitleIconViewThumbHeight);
        QPixmap pixmap;
        if (index.data(kTitleShowStatusIconRole).toBool()) {
            const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
            pixmap = icon.pixmap(thumb_rect.size());
        } else {
            paint_transparency_grid(*painter, thumb_rect, 8);
            const QPixmap thumbnail = index.data(kTitleListThumbnailRole).value<QPixmap>();
            if (!thumbnail.isNull())
                pixmap = thumbnail.scaled(thumb_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (!pixmap.isNull())
            painter->drawPixmap(thumb_rect.center() - QPoint(pixmap.width() / 2, pixmap.height() / 2), pixmap);
        if (!index.data(kTitleShowStatusIconRole).toBool()) {
            painter->setPen(QPen(QColor(0, 0, 0, 90), 1));
            painter->drawRect(thumb_rect.adjusted(0, 0, -1, -1));
        }
        const auto cue_state = static_cast<TitleCueVisualState>(
            index.data(kTitleCueVisualStateRole).toInt());
        const QColor cue_color = title_cue_visual_color(cue_state);
        if (cue_color.isValid())
            paint_cue_state_border(painter, thumb_rect.adjusted(1, 1, -1, -1), 4, cue_color);

        QRect text_rect(item_rect.x(), thumb_rect.bottom() + 6, item_rect.width(),
                        option.fontMetrics.height() * kTitleIconViewTextLines + 4);
        painter->setFont(option.font);
        painter->setPen(option.palette.color((option.state & QStyle::State_Selected)
                                                 ? QPalette::HighlightedText
                                                 : QPalette::Text));
        const QString display = elide_to_two_lines(index.data(Qt::DisplayRole).toString(), option.fontMetrics, text_rect.width());
        painter->drawText(text_rect, Qt::AlignHCenter | Qt::AlignTop, display);
    }

private:
    static void paint_cue_state_border(QPainter *painter, const QRect &rect, int radius,
                                       const QColor &color)
    {
        if (!painter || rect.isEmpty() || !color.isValid())
            return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(rect).adjusted(1.0, 1.0, -1.0, -1.0), radius, radius);
        painter->restore();
    }
};
