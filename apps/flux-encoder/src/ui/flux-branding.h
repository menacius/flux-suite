#pragma once

#include <QIcon>
#include <QPixmap>
#include <QSplashScreen>

class QApplication;
class QLabel;
class QProgressBar;
class QWidget;

namespace flux::ui {

void applyFluxBrandFont(QApplication &application);
QIcon fluxEncoderIcon();
QPixmap fluxEncoderArtwork(const QSize &logicalSize,
                           qreal devicePixelRatio = 1.0);
void showFluxEncoderAbout(QWidget *parent);

class FluxEncoderSplash final : public QSplashScreen {
public:
    explicit FluxEncoderSplash(const QPixmap &artwork);
    void setProgress(int percent, const QString &message);

private:
    QLabel *statusLabel_ = nullptr;
    QLabel *percentLabel_ = nullptr;
    QProgressBar *progressBar_ = nullptr;
};

} // namespace flux::ui
