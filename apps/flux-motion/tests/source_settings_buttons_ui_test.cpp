#include "source-settings-buttons-layout.h"
#include <QLineEdit>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char *message)
{ if (!ok) { std::cerr << message << '\n'; std::exit(1); } }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    SourceSettingsButtonsLayout adapter;
    for (int refresh = 0; refresh < 3; ++refresh) {
        QWidget view;
        auto *form = new QFormLayout(&view);
        form->addRow(QStringLiteral("Title"), new QLineEdit(QStringLiteral("3 Windows")));
        auto *cue = new QPushButton(QStringLiteral("Cue"));
        auto *uncue = new QPushButton(QStringLiteral("Uncue"));
        cue->setToolTip(SourceSettingsButtonsLayout::cue_tooltip);
        uncue->setToolTip(SourceSettingsButtonsLayout::uncue_tooltip);
        form->addRow(nullptr, cue); form->addRow(nullptr, uncue);
        auto *following = new QLineEdit(QStringLiteral("Scene Masks"));
        form->addRow(QStringLiteral("Next setting"), following);
        int cues = 0, uncues = 0;
        QObject::connect(cue, &QPushButton::clicked, [&] { ++cues; });
        QObject::connect(uncue, &QPushButton::clicked, [&] { ++uncues; });
        view.resize(500, 180); view.show();
        QApplication::processEvents(); QApplication::processEvents();
        require(form->rowCount() == 3, "The native buttons must share one form row");
        auto *label = qobject_cast<QLabel *>(form->itemAt(1, QFormLayout::LabelRole)->widget());
        require(label && label->text() == QStringLiteral("Settings buttons"), "Left-side label");
        require(cue->parentWidget() == uncue->parentWidget(), "Both buttons share a container");
        require(cue->isVisible() && uncue->isVisible(), "Both buttons remain visible");
        require(cue->y() == uncue->y() && cue->x() < uncue->x(), "Cue and Uncue are horizontal");
        require(form->itemAt(2, QFormLayout::FieldRole)->widget() == following, "Preserve following properties");
        cue->click(); uncue->click();
        require(cues == 1 && uncues == 1, "Preserve original signal handlers");
        SourceSettingsButtonsLayout::arrange(&view);
        require(form->rowCount() == 3, "Refresh must not duplicate the controls");
    }
    // Same button text in another source/plugin must not be rearranged.
    QWidget unrelated;
    auto *form = new QFormLayout(&unrelated);
    form->addRow(nullptr, new QPushButton(QStringLiteral("Cue")));
    form->addRow(nullptr, new QPushButton(QStringLiteral("Uncue")));
    unrelated.show(); QApplication::processEvents();
    require(form->rowCount() == 2, "Do not change unrelated source properties");
    std::cout << "Horizontal settings buttons, callbacks and refresh lifetime passed\n";
}
