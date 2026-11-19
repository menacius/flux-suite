#include "scene-mask-panel.h"
#include "obs-dock-layout.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <cstdlib>
#include <iostream>

static QString settings_file;

static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static void settle(int milliseconds = 300)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}

static QByteArray saved_layout()
{
    QSettings settings(settings_file, QSettings::IniFormat);
    return settings.value(QStringLiteral("ObsDockLayout/mainWindowState")).toByteArray();
}

static void panels_survive_recreation()
{
    {
        SceneMaskPanel preview(QStringLiteral("PREVIEW"), QStringLiteral("Sections/Preview"), nullptr, settings_file);
        auto *mask = new SceneMaskPanel(QStringLiteral("First mask"), QStringLiteral("Masks/first"), nullptr, settings_file);
        auto *other = new SceneMaskPanel(QStringLiteral("Second mask"), QStringLiteral("Masks/second"), nullptr, settings_file);
        preview.content_layout()->addWidget(mask);
        preview.content_layout()->addWidget(other);
        preview.resize(230, 250);
        preview.show();
        settle(30);
        require(preview.toggle_button()->x() > preview.width() / 2, "Caret must be on the right");
        mask->toggle_button()->click();
        require(mask->content_widget()->isHidden(), "Mask controls must collapse");
        require(mask->toggle_button()->arrowType() == Qt::RightArrow, "Collapsed caret direction");
        preview.toggle_button()->click();
        preview.toggle_button()->click();
        require(mask->content_widget()->isHidden(), "Section toggle must preserve child fold state");
        require(!other->content_widget()->isHidden(), "Other masks keep their own fold state");
        preview.toggle_button()->click();
        SceneMaskPanel monitor(QStringLiteral("MONITOR"), QStringLiteral("Sections/Monitor"), nullptr, settings_file);
        require(!monitor.content_widget()->isHidden(), "Preview and Monitor states are independent");
    }
    {
        SceneMaskPanel preview(QStringLiteral("PREVIEW"), QStringLiteral("Sections/Preview"), nullptr, settings_file);
        require(preview.content_widget()->isHidden(), "Section collapse must survive a new session");
        SceneMaskPanel mask(QStringLiteral("Renamed mask in Monitor"), QStringLiteral("Masks/first"), nullptr, settings_file);
        require(mask.content_widget()->isHidden(), "Logical mask fold must survive rebuilds and section changes");
        SceneMaskPanel other(QStringLiteral("Second mask"), QStringLiteral("Masks/second"), nullptr, settings_file);
        require(!other.content_widget()->isHidden(), "Uncollapsed mask must remain expanded");
        mask.toggle_button()->click();
        require(!mask.content_widget()->isHidden(), "Restored mask must expand normally");
    }
}

struct Window {
    QMainWindow main;
    QDockWidget *titles = new QDockWidget(QStringLiteral("Titles"), &main);
    QDockWidget *masks = new QDockWidget(QStringLiteral("Masks"), &main);
    ObsDockLayout layout{&main, settings_file};

    Window()
    {
        titles->setObjectName(QStringLiteral("flux-motion-dock"));
        masks->setObjectName(QStringLiteral("flux-motion-scene-masks-dock"));
        titles->setWidget(new QLabel(QStringLiteral("Titles")));
        masks->setWidget(new QLabel(QStringLiteral("Controls")));
        main.addDockWidget(Qt::LeftDockWidgetArea, titles);
        main.addDockWidget(Qt::RightDockWidgetArea, masks);
        layout.attach(titles);
        layout.attach(masks);
        main.resize(230, 280);
        main.show();
        settle(20);
    }
};

static void dock_layout_survives_recreation()
{
    QByteArray tabbed_state;
    QSize floating_size;
    QPoint floating_position;
    {
        Window window;
        require(saved_layout().isEmpty(), "Startup events must not overwrite saved layout");
        window.layout.restore();
        window.main.tabifyDockWidget(window.titles, window.masks);
        settle();
        window.layout.save();
        tabbed_state = saved_layout();
        require(!tabbed_state.isEmpty(), "Dock state must be saved");
        window.layout.stop();
        window.main.removeDockWidget(window.masks);
        settle();
        require(saved_layout() == tabbed_state, "Teardown must not save a partial layout");
    }
    {
        Window window;
        require(saved_layout() == tabbed_state, "New dock construction must preserve the old session");
        window.layout.restore();
        require(window.main.tabifiedDockWidgets(window.titles).contains(window.masks),
                "Tab placement must survive restart");
        window.masks->setFloating(true);
        settle();
        const QByteArray before_move = saved_layout();
        window.masks->resize(180, 160);
        window.masks->move(20, 30);
        settle();
        floating_size = window.masks->size();
        floating_position = window.masks->pos();
        require(saved_layout() != tabbed_state, "Floating move/resize events must save without a close");
        require(saved_layout() != before_move, "Moving/resizing an already-floating dock must update its saved layout");
        window.layout.stop();
    }
    {
        Window window;
        window.layout.restore();
        settle(30);
        require(window.masks->isFloating(), "Floating dock state must survive restart");
        require(window.masks->size() == floating_size, "Floating dock size must survive restart");
        require((window.masks->pos() - floating_position).manhattanLength() <= 10,
                "Floating dock position must survive restart");
        window.masks->setFloating(false);
        window.main.addDockWidget(Qt::RightDockWidgetArea, window.masks);
        window.layout.save();
        window.layout.stop();
    }
    {
        Window window;
        window.layout.restore();
        require(!window.masks->isFloating() &&
                window.main.dockWidgetArea(window.masks) == Qt::RightDockWidgetArea,
                "Docked area must survive restart");
        window.layout.stop();
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings_root;
    require(settings_root.isValid(), "Temporary settings directory");
    settings_file = settings_root.filePath(QStringLiteral("dock.ini"));
    panels_survive_recreation();
    dock_layout_survives_recreation();
    std::cout << "Scene-mask panel and dock persistence round trips passed\n";
}
