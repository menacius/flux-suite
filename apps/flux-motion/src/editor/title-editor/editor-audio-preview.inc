namespace {

bool editor_title_contains_audio_media_impl(const std::shared_ptr<Title> &title,
                                            std::set<std::string> &visited)
{
    if (!title || !visited.insert(title->id).second)
        return false;
    for (const auto &layer : title->layers) {
        if (!layer)
            continue;
        if (layer->type == LayerType::Audio)
            return true;
        if (layer->type == LayerType::Asset && !layer->asset_title_id.empty()) {
            auto asset = TitleDataStore::instance().get_title(layer->asset_title_id);
            if (editor_title_contains_audio_media_impl(asset, visited))
                return true;
        }
    }
    return false;
}

bool editor_title_contains_audio_media(const std::shared_ptr<Title> &title)
{
    std::set<std::string> visited;
    return editor_title_contains_audio_media_impl(title, visited);
}

}

class FxmEditorAudioMeter : public QWidget {
public:
    explicit FxmEditorAudioMeter(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("FluxMotionEditorAudioMeter"));
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        setToolTip(QStringLiteral("Editor audio level"));

        resetLevels();
        refreshObsConfiguration(true);
        refreshObsTheme();
    }

    ~FxmEditorAudioMeter() override
    {
        setSession(nullptr);
    }

    void setSession(fxm::editor_host::IEditorAudioPreviewSession *session)
    {
        if (session_ == session)
            return;

        if (session_)
            session_->set_meter_handler({});
        session_ = nullptr;
        {
            QMutexLocker locker(&data_mutex_);
            resetLevelsLocked();
            last_callback_ns_ = 0;
        }
        last_polled_update_ns_ = 0;

        if (session) {
            session_ = session;
            display_channels_ = std::clamp(session->channel_count(), 1, 2);
            session_->set_meter_handler(
                [this](const fxm::editor_host::AudioMeterLevels &levels) {
                    levelsChanged(levels, true);
                });
        } else {
            display_channels_ = 2;
        }
        updateGeometry();
        update();
    }

    void tick()
    {
        refreshObsConfiguration(false);
        pollSessionLevelsIfNeeded();
        update();
    }

    QSize minimumSizeHint() const override
    {
        return sizeHint();
    }

    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        const QSize token = metrics.size(Qt::TextSingleLine,
                                         QStringLiteral("-88"));
        const int meter_width =
            display_channels_ * (meter_thickness_ + 1) - 1;
        const int label_count =
            std::abs(static_cast<int>(minimum_level_) / kTickDbInterval) + 1;
        return QSize(meter_width + token.width() + kTickSize + 10,
                     static_cast<int>(label_count * token.height() *
                                      kTickLabelHeightScale) +
                         kIndicatorThickness);
    }

protected:
    bool event(QEvent *event) override
    {
        const bool result = QWidget::event(event);
        switch (event->type()) {
        case QEvent::ApplicationFontChange:
        case QEvent::FontChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
        case QEvent::Show:
            refreshObsTheme();
            break;
        default:
            break;
        }
        return result;
    }

    void paintEvent(QPaintEvent *) override
    {
        const uint64_t now = monotonicNs();
        const double elapsed =
            last_redraw_ns_ == 0
                ? 0.0
                : static_cast<double>(now - last_redraw_ns_) / 1000000000.0;

        std::array<float, 2> magnitude{};
        std::array<float, 2> peak{};
        std::array<float, 2> peak_hold{};
        std::array<float, 2> input_peak_hold{};
        bool idle = true;
        {
            QMutexLocker locker(&data_mutex_);
            calculateBallisticsLocked(now, elapsed);
            idle = current_last_update_ns_ == 0 ||
                   static_cast<double>(now - current_last_update_ns_) /
                           1000000000.0 >
                       0.5;
            if (idle)
                resetLevelsLocked();
            for (int channel = 0; channel < 2; ++channel) {
                magnitude[channel] = display_magnitude_[channel];
                peak[channel] = display_peak_[channel];
                peak_hold[channel] = display_peak_hold_[channel];
                input_peak_hold[channel] = display_input_peak_hold_[channel];
            }
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), meter_background_color_);

        const int meter_length =
            std::max(1, height() - (kIndicatorThickness + 2));
        const double scale = meter_length / minimum_level_;
        const int warning_position =
            meter_length - static_cast<int>(warning_level_ * scale);
        const int error_position =
            meter_length - static_cast<int>(error_level_ * scale);
        const int clip_position =
            meter_length - static_cast<int>(clip_level_ * scale);
        const int nominal_length = warning_position;
        const int warning_length =
            nominal_length + (error_position - warning_position);

        painter.setFont(font());
        painter.setPen(major_tick_color_);
        const QFontMetrics metrics(font());
        const int tick_x =
            display_channels_ * (meter_thickness_ + 1) - 1;
        const int tick_height = height() - (kIndicatorThickness + 3);
        const double tick_scale = tick_height / minimum_level_;
        for (int db = 0; db >= static_cast<int>(minimum_level_);
             db -= kTickDbInterval) {
            const int position =
                static_cast<int>(db * tick_scale);
            const QString label = QString::number(db);
            if (db == 0) {
                painter.drawText(tick_x + 10,
                                 position + metrics.capHeight(), label);
            } else {
                painter.drawText(tick_x + 8,
                                 position + metrics.capHeight() / 2, label);
            }
            painter.drawLine(tick_x, position, tick_x + kTickSize, position);
        }

        for (int channel = 0; channel < display_channels_; ++channel) {
            const int offset = channel * (meter_thickness_ + 1);
            painter.fillRect(offset, meter_length, meter_thickness_,
                             -meter_length, background_error_color_);
            painter.fillRect(offset, meter_length, meter_thickness_,
                             -warning_length, background_warning_color_);
            painter.fillRect(offset, meter_length, meter_thickness_,
                             -nominal_length, background_nominal_color_);
        }

        bool clipping = now < clip_until_ns_;
        for (int channel = 0; channel < display_channels_; ++channel) {
            const int offset = channel * (meter_thickness_ + 1);
            const int peak_position =
                clipping
                    ? meter_length
                    : meter_length -
                          dbPosition(peak[channel], scale);
            const int peak_hold_position =
                meter_length - dbPosition(peak_hold[channel], scale);
            const int magnitude_position =
                meter_length - dbPosition(magnitude[channel], scale);

            auto fill = [&](int length, const QColor &color) {
                painter.fillRect(offset, meter_length, meter_thickness_,
                                 -length, color);
            };

            if (peak_position >= clip_position) {
                clip_until_ns_ =
                    std::max(clip_until_ns_,
                             now + kClipFlashDurationNs);
                clipping = true;
                fill(meter_length, foreground_error_color_);
            } else {
                if (peak_position > error_position)
                    fill(std::min(peak_position, meter_length),
                         foreground_error_color_);
                if (peak_position > warning_position)
                    fill(std::min(peak_position, warning_length),
                         foreground_warning_color_);
                if (peak_position > kMeterStart)
                    fill(std::min(peak_position, nominal_length),
                         foreground_nominal_color_);
            }

            QColor peak_hold_color = foreground_nominal_color_;
            if (peak_hold_position >= error_position)
                peak_hold_color = foreground_error_color_;
            else if (peak_hold_position >= warning_position)
                peak_hold_color = foreground_warning_color_;

            if (peak_hold_position - 3 > 0) {
                painter.fillRect(offset,
                                 meter_length - peak_hold_position - 3,
                                 meter_thickness_, 3, peak_hold_color);
            }
            if (magnitude_position - 3 >= 0) {
                painter.fillRect(offset,
                                 meter_length - magnitude_position - 3,
                                 meter_thickness_, 3, magnitude_color_);
            }
            if (!idle) {
                painter.fillRect(
                    offset, height(), meter_thickness_,
                    -kIndicatorThickness,
                    inputPeakColor(input_peak_hold[channel]));
            }
        }
        last_redraw_ns_ = now;
    }

private:
    static constexpr int kIndicatorThickness = 3;
    static constexpr int kMeterStart = kIndicatorThickness + 2;
    static constexpr int kTickSize = 2;
    static constexpr int kTickDbInterval = 6;
    static constexpr float kTickLabelHeightScale = 0.8f;
    static constexpr uint64_t kClipFlashDurationNs = 1000000000ULL;

    static uint64_t monotonicNs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    static QColor colorFromObsConfig(int64_t value)
    {
        return QColor(static_cast<int>(value & 0xff),
                      static_cast<int>((value >> 8) & 0xff),
                      static_cast<int>((value >> 16) & 0xff));
    }

    static QColor widgetColor(QWidget *widget, const char *property_name,
                              const QColor &fallback)
    {
        if (!widget)
            return fallback;
        const QVariant value = widget->property(property_name);
        return value.canConvert<QColor>() ? value.value<QColor>() : fallback;
    }

    void refreshObsTheme()
    {
        if (refreshing_theme_)
            return;
        refreshing_theme_ = true;

        QWidget *native_meter = nullptr;
        for (QWidget *candidate : QApplication::allWidgets()) {
            if (candidate && candidate != this &&
                candidate->inherits("VolumeMeter")) {
                native_meter = candidate;
                break;
            }
        }

        QColor background_nominal(0x26, 0x7f, 0x26);
        QColor background_warning(0x7f, 0x7f, 0x26);
        QColor background_error(0x7f, 0x26, 0x26);
        QColor foreground_nominal(0x4c, 0xff, 0x4c);
        QColor foreground_warning(0xff, 0xff, 0x4c);
        QColor foreground_error(0xff, 0x4c, 0x4c);
        QColor magnitude(0, 0, 0);
        QColor major_tick = palette().color(QPalette::Text);
        QColor meter_background = palette().color(QPalette::Window);

        QFont meter_font = font();
        meter_font.setPointSizeF(6.0);
        meter_font.setBold(true);
        if (native_meter) {
            background_nominal = widgetColor(
                native_meter, "backgroundNominalColor", background_nominal);
            background_warning = widgetColor(
                native_meter, "backgroundWarningColor", background_warning);
            background_error = widgetColor(
                native_meter, "backgroundErrorColor", background_error);
            foreground_nominal = widgetColor(
                native_meter, "foregroundNominalColor", foreground_nominal);
            foreground_warning = widgetColor(
                native_meter, "foregroundWarningColor", foreground_warning);
            foreground_error = widgetColor(
                native_meter, "foregroundErrorColor", foreground_error);
            magnitude = widgetColor(native_meter, "magnitudeColor", magnitude);
            major_tick = widgetColor(native_meter, "majorTickColor", major_tick);
            meter_background =
                native_meter->palette().color(QPalette::Window);
            meter_font = native_meter->font();
        }

        const auto *host = fxm::editor_host::editor_host();
        const auto configuration = host
            ? host->audio_meter_configuration()
            : fxm::editor_host::AudioMeterConfiguration{};
        if (configuration.override_colors) {
            background_nominal = colorFromObsConfig(
                configuration.background_nominal);
            background_warning = colorFromObsConfig(
                configuration.background_warning);
            background_error = colorFromObsConfig(
                configuration.background_error);
            foreground_nominal = colorFromObsConfig(
                configuration.foreground_nominal);
            foreground_warning = colorFromObsConfig(
                configuration.foreground_warning);
            foreground_error = colorFromObsConfig(
                configuration.foreground_error);
        }

        background_nominal_color_ = background_nominal;
        background_warning_color_ = background_warning;
        background_error_color_ = background_error;
        foreground_nominal_color_ = foreground_nominal;
        foreground_warning_color_ = foreground_warning;
        foreground_error_color_ = foreground_error;
        magnitude_color_ = magnitude;
        major_tick_color_ = major_tick;
        meter_background_color_ = meter_background;
        if (font() != meter_font)
            setFont(meter_font);
        updateGeometry();
        update();
        refreshing_theme_ = false;
    }

    void refreshObsConfiguration(bool force)
    {
        const uint64_t now = monotonicNs();
        if (!force && now - last_configuration_refresh_ns_ < 1000000000ULL)
            return;
        last_configuration_refresh_ns_ = now;

        const auto *host = fxm::editor_host::editor_host();
        const bool true_peak = host &&
            host->audio_meter_configuration().true_peak;
        if (force || true_peak_ != true_peak) {
            true_peak_ = true_peak;
            warning_level_ = true_peak ? -13.0 : -20.0;
            error_level_ = true_peak ? -2.0 : -9.0;
        }
        refreshObsTheme();
    }

    void pollSessionLevelsIfNeeded()
    {
        if (!session_)
            return;

        const uint64_t now = monotonicNs();
        {
            QMutexLocker locker(&data_mutex_);
            /* OBS supplies native dB meter callbacks. The standalone runtime
             * exposes linear peaks through audio_levels(), so polling is the
             * fallback whenever a native callback is not active. */
            if (last_callback_ns_ != 0 &&
                now - last_callback_ns_ < 150000000ULL)
                return;
        }

        float left = 0.0f;
        float right = 0.0f;
        uint64_t source_update_ns = 0;
        if (!session_->audio_levels(&left, &right, &source_update_ns) ||
            source_update_ns == 0 ||
            source_update_ns == last_polled_update_ns_)
            return;
        last_polled_update_ns_ = source_update_ns;

        auto linearToDb = [](float level) {
            if (!std::isfinite(level) || level <= 0.000001f)
                return -std::numeric_limits<float>::infinity();
            return 20.0f * std::log10(std::clamp(level, 0.000001f, 1.0f));
        };
        const float left_db = linearToDb(left);
        const float right_db = linearToDb(right);
        fxm::editor_host::AudioMeterLevels levels;
        levels.magnitude.fill(-std::numeric_limits<float>::infinity());
        levels.peak.fill(-std::numeric_limits<float>::infinity());
        levels.input_peak.fill(-std::numeric_limits<float>::infinity());
        levels.magnitude[0] = levels.peak[0] = levels.input_peak[0] = left_db;
        levels.magnitude[1] = levels.peak[1] = levels.input_peak[1] = right_db;
        levelsChanged(levels, false);
    }

    void levelsChanged(const fxm::editor_host::AudioMeterLevels &levels,
                       bool native_callback)
    {
        const uint64_t now = monotonicNs();
        QMutexLocker locker(&data_mutex_);
        if (native_callback)
            last_callback_ns_ = now;
        current_last_update_ns_ = now;
        for (std::size_t channel = 0;
             channel < fxm::editor_host::kAudioMeterChannelCapacity;
             ++channel) {
            current_magnitude_[channel] = levels.magnitude[channel];
            current_peak_[channel] = levels.peak[channel];
            current_input_peak_[channel] = levels.input_peak[channel];
        }
        calculateBallisticsLocked(now, 0.0);
    }

    void calculateBallisticsLocked(uint64_t now, double elapsed)
    {
        for (std::size_t channel = 0;
             channel < fxm::editor_host::kAudioMeterChannelCapacity;
             ++channel) {
            const float current_peak = current_peak_[channel];
            if (current_peak >= display_peak_[channel] ||
                !std::isfinite(display_peak_[channel])) {
                display_peak_[channel] = current_peak;
            } else {
                const float decay =
                    static_cast<float>(peak_decay_rate_ * elapsed);
                display_peak_[channel] = std::clamp(
                    display_peak_[channel] - decay,
                    std::min(current_peak, 0.0f), 0.0f);
            }

            if (current_peak >= display_peak_hold_[channel] ||
                !std::isfinite(display_peak_hold_[channel])) {
                display_peak_hold_[channel] = current_peak;
                display_peak_hold_update_ns_[channel] = now;
            } else if (static_cast<double>(
                           now - display_peak_hold_update_ns_[channel]) /
                           1000000000.0 >
                       peak_hold_duration_) {
                display_peak_hold_[channel] = current_peak;
                display_peak_hold_update_ns_[channel] = now;
            }

            const float current_input = current_input_peak_[channel];
            if (current_input >= display_input_peak_hold_[channel] ||
                !std::isfinite(display_input_peak_hold_[channel])) {
                display_input_peak_hold_[channel] = current_input;
                display_input_peak_hold_update_ns_[channel] = now;
            } else if (static_cast<double>(
                           now - display_input_peak_hold_update_ns_[channel]) /
                           1000000000.0 >
                       input_peak_hold_duration_) {
                display_input_peak_hold_[channel] = current_input;
                display_input_peak_hold_update_ns_[channel] = now;
            }

            const float current_magnitude = current_magnitude_[channel];
            if (!std::isfinite(display_magnitude_[channel])) {
                display_magnitude_[channel] = current_magnitude;
            } else {
                const float attack = static_cast<float>(
                    (current_magnitude - display_magnitude_[channel]) *
                    (elapsed / magnitude_integration_time_) * 0.99);
                display_magnitude_[channel] = std::clamp(
                    display_magnitude_[channel] + attack,
                    static_cast<float>(minimum_level_), 0.0f);
            }
        }
    }

    void resetLevels()
    {
        QMutexLocker locker(&data_mutex_);
        resetLevelsLocked();
    }

    void resetLevelsLocked()
    {
        current_last_update_ns_ = 0;
        const float silence = -std::numeric_limits<float>::infinity();
        current_magnitude_.fill(silence);
        current_peak_.fill(silence);
        current_input_peak_.fill(silence);
        display_magnitude_.fill(silence);
        display_peak_.fill(silence);
        display_peak_hold_.fill(silence);
        display_input_peak_hold_.fill(silence);
        display_peak_hold_update_ns_.fill(0);
        display_input_peak_hold_update_ns_.fill(0);
    }

    static int dbPosition(float db, double scale)
    {
        if (!std::isfinite(db))
            return std::numeric_limits<int>::max();
        return static_cast<int>(
            std::clamp(static_cast<double>(db), -60.0, 0.0) * scale);
    }

    QColor inputPeakColor(float peak_hold) const
    {
        if (peak_hold < minimum_input_level_)
            return background_nominal_color_;
        if (peak_hold < warning_level_)
            return foreground_nominal_color_;
        if (peak_hold < error_level_)
            return foreground_warning_color_;
        if (peak_hold < clip_level_)
            return foreground_error_color_;
        return QColor(255, 255, 255);
    }

    QMutex data_mutex_;
    fxm::editor_host::IEditorAudioPreviewSession *session_ = nullptr;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> current_magnitude_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> current_peak_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> current_input_peak_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> display_magnitude_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> display_peak_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> display_peak_hold_;
    std::array<float, fxm::editor_host::kAudioMeterChannelCapacity> display_input_peak_hold_;
    std::array<uint64_t, fxm::editor_host::kAudioMeterChannelCapacity> display_peak_hold_update_ns_;
    std::array<uint64_t, fxm::editor_host::kAudioMeterChannelCapacity>
        display_input_peak_hold_update_ns_;
    uint64_t current_last_update_ns_ = 0;
    uint64_t last_callback_ns_ = 0;
    uint64_t last_polled_update_ns_ = 0;
    uint64_t last_redraw_ns_ = 0;
    uint64_t clip_until_ns_ = 0;
    uint64_t last_configuration_refresh_ns_ = 0;

    QColor background_nominal_color_;
    QColor background_warning_color_;
    QColor background_error_color_;
    QColor foreground_nominal_color_;
    QColor foreground_warning_color_;
    QColor foreground_error_color_;
    QColor magnitude_color_;
    QColor major_tick_color_;
    QColor meter_background_color_;

    bool true_peak_ = false;
    int display_channels_ = 2;
    int meter_thickness_ = 6;
    double minimum_level_ = -60.0;
    double warning_level_ = -20.0;
    double error_level_ = -9.0;
    double clip_level_ = 0.0;
    double minimum_input_level_ = -50.0;
    double peak_decay_rate_ = 11.76;
    double magnitude_integration_time_ = 0.3;
    double peak_hold_duration_ = 20.0;
    double input_peak_hold_duration_ = 1.0;
    bool refreshing_theme_ = false;
};

class FxmEditorAudioMonitorButton : public QToolButton {
public:
    explicit FxmEditorAudioMonitorButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QToolButton::paintEvent(event);
        if (isChecked())
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF mark = QRectF(rect()).adjusted(9.0, 9.0, -9.0, -9.0);
        painter.setPen(QPen(QColor(220, 45, 45), 3.0,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(mark.topLeft(), mark.bottomRight());
        painter.drawLine(mark.topRight(), mark.bottomLeft());
    }
};

QWidget *TitleEditor::create_editor_audio_panel()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("FluxMotionEditorAudioPanel"));
    panel->setMinimumWidth(78);
    panel->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 6, 4, 6);
    layout->setSpacing(4);

    editor_audio_meter_ = new FxmEditorAudioMeter(panel);
    layout->addWidget(editor_audio_meter_, 1, Qt::AlignHCenter);

    editor_audio_monitor_button_ = new FxmEditorAudioMonitorButton(panel);
    editor_audio_monitor_button_->setObjectName(
        QStringLiteral("FluxMotionEditorAudioMonitorButton"));
    editor_audio_monitor_button_->setIcon(obs_icon("headphones.svg"));
    editor_audio_monitor_button_->setText(QStringLiteral("🎧"));
    editor_audio_monitor_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    editor_audio_monitor_button_->setCheckable(true);
    editor_audio_monitor_button_->setAutoRaise(true);
    editor_audio_monitor_button_->setFixedSize(36, 36);
    editor_audio_monitor_button_->setIconSize(QSize(22, 22));
    editor_audio_monitor_button_->setStyleSheet(QStringLiteral(
        "QToolButton{border:none;background:transparent;padding:4px;}"
        "QToolButton:hover{background:palette(light);}"
        "QToolButton:checked{background:palette(highlight);"
        "color:palette(highlighted-text);}"
        "QToolButton:disabled{background:transparent;color:palette(mid);}"));
    connect(editor_audio_monitor_button_, &QToolButton::toggled, this,
            [this](bool enabled) {
                editor_audio_monitor_enabled_ = enabled;
                apply_editor_audio_monitoring();
                update_editor_audio_monitor_button();
            });

    layout->addWidget(editor_audio_monitor_button_, 0,
                      Qt::AlignHCenter | Qt::AlignBottom);

    editor_audio_meter_timer_ = new QTimer(panel);
    editor_audio_meter_timer_->setInterval(16);
    editor_audio_meter_timer_->setTimerType(Qt::PreciseTimer);
    connect(editor_audio_meter_timer_, &QTimer::timeout, this,
            &TitleEditor::update_editor_audio_meter);
    editor_audio_meter_timer_->start();
    connect(&CacheManager::instance(), &CacheManager::playbackSettingsChanged, this,
            [this]() {
                apply_editor_audio_monitoring();
                update_editor_audio_monitor_button();
                update_editor_audio_meter();
                if (editor_audio_preview_session_)
                    sync_editor_audio_preview(false);
            });

    update_editor_audio_monitor_button();
    update_editor_audio_meter();
    return panel;
}

void TitleEditor::apply_editor_audio_monitoring()
{
    if (!editor_audio_preview_session_)
        return;
    editor_audio_preview_session_->set_monitoring(
        editor_audio_monitor_enabled_);
}

void TitleEditor::update_editor_audio_monitor_button()
{
    if (!editor_audio_monitor_button_)
        return;

    QSignalBlocker blocker(editor_audio_monitor_button_);
    const bool play_every_frame =
        CacheManager::instance().playbackSettings().play_every_frame;
    editor_audio_monitor_button_->setChecked(editor_audio_monitor_enabled_);
    editor_audio_monitor_button_->setEnabled(
        editor_audio_preview_session_ != nullptr ||
        editor_title_contains_audio_media(title_));
    editor_audio_monitor_button_->setToolTip(
        editor_audio_monitor_enabled_
            ? (play_every_frame
                   ? QStringLiteral("Editor audio monitoring is on; Play Every Frame varispeeds audio to visual cadence")
                   : QStringLiteral("Editor audio monitoring is on"))
            : QStringLiteral("Editor audio monitoring is muted"));
}

void TitleEditor::update_editor_audio_meter()
{
    if (!editor_audio_meter_)
        return;
    editor_audio_meter_->setSession(editor_audio_preview_session_.get());
    if (editor_audio_meter_->isVisibleTo(this))
        editor_audio_meter_->tick();
}

/* Editor-only monitored preview source. It uses the same FXM source/audio
 * runtime as program output, but is private and monitor-only, preventing a
 * second scene/program output while still following the editor transport. */
void TitleEditor::ensure_editor_audio_preview()
{
    if (editor_audio_preview_session_ || !title_ ||
        !editor_title_contains_audio_media(title_))
        return;

    FXM_LOG_INFO(
        "Audio",
        QStringLiteral("[editor-preview-create-request] title=%1 playhead=%2")
            .arg(QString::fromStdString(title_->id))
            .arg(playhead_, 0, 'f', 3));

    auto *host = fxm::editor_host::editor_host();
    if (host) {
        editor_audio_preview_session_ =
            host->create_editor_audio_preview_session(title_->id);
    }

    if (!editor_audio_preview_session_) {
        FXM_LOG_ERROR(
            "Audio",
            QStringLiteral("[editor-preview-create-failed] title=%1")
                .arg(QString::fromStdString(title_->id)));
        return;
    }

    if (editor_audio_meter_)
        editor_audio_meter_->setSession(editor_audio_preview_session_.get());
    apply_editor_audio_monitoring();
    update_editor_audio_monitor_button();

    FXM_LOG_INFO(
        "Audio",
        QStringLiteral(
            "[editor-preview-created] title=%1 source=%2 active=%3 "
            "showing=%4 monitoring_type=%5")
            .arg(QString::fromStdString(title_->id))
            .arg(QString::fromStdString(editor_audio_preview_session_->name()))
            .arg(editor_audio_preview_session_->active() ? 1 : 0)
            .arg(editor_audio_preview_session_->showing() ? 1 : 0)
            .arg(editor_audio_preview_session_->monitoring_mode()));

    editor_audio_preview_session_->seek(playhead_);
    editor_audio_preview_session_->set_playing(false);
    editor_audio_snapshot_dirty_ = true;
    editor_audio_play_state_known_ = true;
    editor_audio_last_playing_ = false;
}

void TitleEditor::release_editor_audio_preview()
{
    if (!editor_audio_preview_session_)
        return;

    FXM_LOG_INFO(
        "Audio",
        QStringLiteral(
            "[editor-preview-release] source=%1 playhead=%2 state=%3")
            .arg(QString::fromStdString(editor_audio_preview_session_->name()))
            .arg(playhead_, 0, 'f', 3)
            .arg(static_cast<int>(editor_audio_preview_session_->media_state())));

    if (editor_audio_meter_)
        editor_audio_meter_->setSession(nullptr);
    editor_audio_preview_session_->stop();
    editor_audio_preview_session_.reset();
    editor_audio_snapshot_dirty_ = true;
    editor_audio_play_state_known_ = false;
    editor_audio_last_playing_ = false;
    update_editor_audio_monitor_button();
}

void TitleEditor::sync_editor_audio_preview(bool discontinuity)
{
    if (!title_)
        return;

    if (!editor_title_contains_audio_media(title_)) {
        release_editor_audio_preview();
        return;
    }

    ensure_editor_audio_preview();
    if (!editor_audio_preview_session_)
        return;

    if (discontinuity || editor_audio_snapshot_dirty_) {
        editor_audio_preview_session_->set_title_snapshot(title_);
        editor_audio_snapshot_dirty_ = false;
    }
    const CachePlaybackSettings cache_settings =
        CacheManager::instance().playbackSettings();
    double editor_audio_play_every_frame_speed_factor = 1.0;
    if (cache_settings.play_every_frame && playing_ && !discontinuity) {
        const qint64 elapsed_ns = editor_audio_speed_clock_.isValid()
            ? editor_audio_speed_clock_.nsecsElapsed()
            : 0;
        const double wall_seconds = static_cast<double>(elapsed_ns) / 1.0e9;
        const double timeline_seconds = std::abs(
            playhead_ - editor_audio_speed_last_playhead_);
        if (wall_seconds > 0.001 && timeline_seconds > 0.0) {
            const double measured = std::clamp(timeline_seconds / wall_seconds,
                                               0.02, 4.0);
            editor_audio_speed_factor_ =
                std::clamp(editor_audio_speed_factor_ * 0.65 +
                               measured * 0.35,
                           0.02, 4.0);
        }
        editor_audio_play_every_frame_speed_factor = editor_audio_speed_factor_;
    } else {
        editor_audio_speed_factor_ = 1.0;
    }
    editor_audio_speed_last_playhead_ = playhead_;
    editor_audio_speed_clock_.restart();
    editor_audio_preview_session_->set_transport(
        playhead_, playback_reverse_,
        editor_audio_play_every_frame_speed_factor);

    if (discontinuity) {
        FXM_LOG_INFO(
            "Audio",
            QStringLiteral(
                "[editor-preview-seek] source=%1 playhead=%2 playing=%3 reverse=%4")
                .arg(QString::fromStdString(editor_audio_preview_session_->name()))
                .arg(playhead_, 0, 'f', 3)
                .arg(playing_ ? 1 : 0)
                .arg(playback_reverse_ ? 1 : 0));
        editor_audio_preview_seeking_ = true;
        editor_audio_preview_session_->seek(playhead_);
        editor_audio_preview_seeking_ = false;
    }

    const bool play_state_changed = !editor_audio_play_state_known_ ||
        editor_audio_last_playing_ != playing_;
    const bool playback_recovery_needed = playing_ &&
        editor_audio_preview_session_->media_state() !=
            fxm::editor_host::MediaState::Playing;
    if (play_state_changed || discontinuity || playback_recovery_needed) {
        editor_audio_preview_session_->set_playing(playing_);
        editor_audio_play_state_known_ = true;
        editor_audio_last_playing_ = playing_;
    }

    if (discontinuity || play_state_changed) {
        FXM_LOG_DEBUG(
            "Audio",
            QStringLiteral(
                "[editor-preview-sync] source=%1 playhead=%2 playing=%3 "
                "media_state=%4 discontinuity=%5 reverse=%6 audioSpeed=%7")
                .arg(QString::fromStdString(editor_audio_preview_session_->name()))
                .arg(playhead_, 0, 'f', 3)
                .arg(playing_ ? 1 : 0)
                .arg(static_cast<int>(editor_audio_preview_session_->media_state()))
                .arg(discontinuity ? 1 : 0)
                .arg(playback_reverse_ ? 1 : 0)
                .arg(editor_audio_play_every_frame_speed_factor, 0, 'f', 3));
    }
}


void TitleEditor::publish_editor_audio_runtime_state()
{
    if (!title_) return;
    editor_audio_snapshot_dirty_ = true;
    auto stored = TitleDataStore::instance().get_title(title_->id);
    if (!stored) return;
    for (const auto &src : title_->layers) {
        if (!src) continue;
        auto dst = stored->find_layer(src->id);
        if (!dst) continue;
        if (src->type == LayerType::Audio || src->type == LayerType::Video || layer_type_is_container(src->type)) {
            dst->audio_volume = src->audio_volume;
            dst->audio_pan = src->audio_pan;
            dst->audio_volume_prop = src->audio_volume_prop;
            dst->audio_pan_prop = src->audio_pan_prop;
            dst->audio_muted = src->audio_muted;
            dst->audio_solo = src->audio_solo;
            dst->audio_fade_in = src->audio_fade_in;
            dst->audio_fade_out = src->audio_fade_out;
            dst->audio_fade_curve = src->audio_fade_curve;
            dst->audio_loop = src->audio_loop;
            dst->audio_independent = src->audio_independent;
            dst->audio_playback_mode = src->audio_playback_mode;
            dst->video_playback_mode = src->video_playback_mode;
            dst->audio_in_point = src->audio_in_point;
            dst->audio_out_point = src->audio_out_point;
            dst->audio_effects = src->audio_effects;
        }
    }
    TitleDataStore::instance().touch_runtime_change();
}

void TitleEditor::update_footer_diagnostics()
{
    if (!status_diagnostics_label_ ||
        !status_diagnostics_label_->isVisibleTo(this))
        return;
    int total = 0, ready = 0;
    int active_waveform_percent = 100;
    QString active_waveform_label;
    if (title_) {
        auto stored = TitleDataStore::instance().get_title(title_->id);
        for (const auto &layer : title_->layers) {
            if (!layer || layer->type != LayerType::Audio) continue;
            ++total;
            bool has = !layer->audio_waveform.empty();
            if (stored) {
                auto runtime_layer = stored->find_layer(layer->id);
                if (runtime_layer) {
                    if (!runtime_layer->audio_waveform.empty()) {
                        layer->audio_waveform = runtime_layer->audio_waveform;
                        layer->audio_waveform_duration = runtime_layer->audio_waveform_duration;
                        has = true;
                    }
                    layer->audio_waveform_progress_percent =
                        runtime_layer->audio_waveform_progress_percent;
                    layer->audio_waveform_generating =
                        runtime_layer->audio_waveform_generating;
                    layer->audio_waveform_progress_label =
                        runtime_layer->audio_waveform_progress_label;
                }
            }
            if (has) {
                ++ready;
            } else if (layer->audio_waveform_generating) {
                active_waveform_percent = std::clamp(
                    layer->audio_waveform_progress_percent, 0, 99);
                active_waveform_label = layer->audio_waveform_progress_label.empty()
                    ? QString::fromStdString(layer->name)
                    : QString::fromStdString(layer->audio_waveform_progress_label);
            }
        }
    }
    const int waveform_percent = total == 0 ? 100 :
        std::max(0, std::min(100, (ready * 100 + active_waveform_percent) /
                                  std::max(1, total)));
    const CanvasPreview::DiagnosticsSample diagnostics = canvas_
        ? canvas_->take_diagnostics_sample()
        : CanvasPreview::DiagnosticsSample{};
    const double prepare_ms = diagnostics.average_prepare_ms;
    const double render_ms = diagnostics.average_render_ms;
    const double total_frame_ms = diagnostics.average_total_frame_ms;
    const double live_fps = playing_ ? diagnostics.playback_fps : 0.0;
    const double target_fps = std::max(1.0, obs_frame_rate());
    const double frame_budget_ms = 1000.0 / target_fps;
    const QString normal_color = status_diagnostics_label_->palette().color(QPalette::Text).name();
    QString render_color = normal_color;
    if (render_ms > frame_budget_ms) render_color = QStringLiteral("#e53935");
    else if (render_ms >= frame_budget_ms * 0.80) render_color = QStringLiteral("#f9a825");
    QString total_color = normal_color;
    if (total_frame_ms > frame_budget_ms) total_color = QStringLiteral("#e53935");
    else if (total_frame_ms >= frame_budget_ms * 0.80) total_color = QStringLiteral("#f9a825");
    const double fps_measurement_tolerance = std::max(0.5, target_fps * 0.01);
    const QString fps_color =
        (playing_ && live_fps + fps_measurement_tolerance < target_fps)
        ? QStringLiteral("#e53935") : normal_color;

    QStringList parts;
    if (waveform_percent < 100) {
        if (!active_waveform_label.isEmpty())
            parts << QStringLiteral("Waveform %1% — %2")
                         .arg(waveform_percent)
                         .arg(active_waveform_label.toHtmlEscaped());
        else
            parts << QStringLiteral("Waveform %1%").arg(waveform_percent);
    }
    parts << QStringLiteral("Render: <span style=\"color:%1\">%2 ms</span>")
                 .arg(render_color).arg(render_ms, 0, 'f', 1);
    parts << QStringLiteral("Total frame: <span style=\"color:%1\">%2 ms</span>")
                 .arg(total_color).arg(total_frame_ms, 0, 'f', 1);
    parts << QStringLiteral("FPS: <span style=\"color:%1\">%2</span>")
                 .arg(fps_color).arg(live_fps, 0, 'f', 1);
    if (diagnostics.render_target_width > 0 &&
        diagnostics.render_target_height > 0) {
        parts << QStringLiteral("Preview: %1×%2 (%3%)")
                     .arg(diagnostics.render_target_width)
                     .arg(diagnostics.render_target_height)
                     .arg(qRound(diagnostics.preview_scale * 100.0));
    }
    const QString diagnostics_text = parts.join(QStringLiteral("   |   "));
    if (status_diagnostics_label_->text() != diagnostics_text)
        status_diagnostics_label_->setText(diagnostics_text);
    status_diagnostics_label_->setToolTip(QStringLiteral(
        "Frame pipeline average — prepare: %1 ms; render/submit: %2 ms; "
        "graphics wait: %3 ms; preview upload: %4 ms; present/vsync: %5 ms; "
        "total: %6 ms; requested: %7×%8; actual target: %9×%10. "
        "Normal editor playback performs no GPU-to-CPU readback.")
        .arg(prepare_ms, 0, 'f', 2)
        .arg(render_ms, 0, 'f', 2)
        .arg(diagnostics.average_graphics_wait_ms, 0, 'f', 2)
        .arg(diagnostics.average_preview_upload_ms, 0, 'f', 2)
        .arg(diagnostics.average_present_ms, 0, 'f', 2)
        .arg(total_frame_ms, 0, 'f', 2)
        .arg(diagnostics.requested_width)
        .arg(diagnostics.requested_height)
        .arg(diagnostics.render_target_width)
        .arg(diagnostics.render_target_height));
    if (waveform_percent < 100 && timeline_)
        timeline_->update();
}
