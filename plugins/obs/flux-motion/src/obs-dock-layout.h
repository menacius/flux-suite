#pragma once

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <memory>

// OBS owns the docks. Observe their final Qt layout, including floating moves,
// resizes and tab/split changes, without saving transient startup/teardown state.
class ObsDockLayout final : public QObject {
public:
    explicit ObsDockLayout(QMainWindow *main, const QString &settings_file = {})
        : QObject(main), main_(main), settings_file_(settings_file)
    {
        save_timer_.setSingleShot(true);
        save_timer_.setInterval(200);
        connect(&save_timer_, &QTimer::timeout, this, [this] { save(); });
        main->installEventFilter(this);
    }

    void attach(QDockWidget *dock)
    {
        dock->installEventFilter(this);
        connect(dock, &QDockWidget::dockLocationChanged, this,
                [this](Qt::DockWidgetArea) { schedule_save(); });
        connect(dock, &QDockWidget::topLevelChanged, this,
                [this](bool) { schedule_save(); });
        connect(dock, &QDockWidget::visibilityChanged, this,
                [this](bool) { schedule_save(); });
    }

    void restore()
    {
        ready_ = false;
        save_timer_.stop();
        auto settings = open_settings();
        settings->beginGroup(QStringLiteral("ObsDockLayout"));
        const QByteArray state = settings->value(QStringLiteral("mainWindowState")).toByteArray();
        if (main_ && !state.isEmpty())
            main_->restoreState(state, 1);
        ready_ = true;
    }

    void save()
    {
        if (!ready_ || !main_) return;
        save_timer_.stop();
        auto settings = open_settings();
        settings->beginGroup(QStringLiteral("ObsDockLayout"));
        settings->setValue(QStringLiteral("mainWindowState"), main_->saveState(1));
        settings->sync();
    }

    void stop()
    {
        ready_ = false;
        save_timer_.stop();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
        case QEvent::Show:
        case QEvent::Hide:
            schedule_save();
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::unique_ptr<QSettings> open_settings() const
    {
        return settings_file_.isEmpty()
            ? std::make_unique<QSettings>(QStringLiteral("FluxMotion"), QStringLiteral("Dock"))
            : std::make_unique<QSettings>(settings_file_, QSettings::IniFormat);
    }

    void schedule_save()
    {
        if (ready_) save_timer_.start();
    }

    QPointer<QMainWindow> main_;
    QString settings_file_;
    QTimer save_timer_;
    bool ready_ = false;
};
