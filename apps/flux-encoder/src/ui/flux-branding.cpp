#include "ui/flux-branding.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QSvgRenderer>
#include <QVBoxLayout>

#include <algorithm>

namespace flux::ui {
namespace {

class ClickableAboutGraphic final : public QLabel {
public:
    explicit ClickableAboutGraphic(QWidget *parent = nullptr)
        : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (QWidget *topLevel = window())
                topLevel->close();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }
};

QByteArray brandedArtworkSvg()
{
    QFile file(QStringLiteral(":/branding/flux-encoder-splash-about.svg"));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QByteArray svg = file.readAll();
    const QByteArray marker = "<text id=\"flux-encoder-version\"";
    const qsizetype elementStart = svg.indexOf(marker);
    const qsizetype contentStart = elementStart < 0 ? -1 : svg.indexOf('>', elementStart);
    const qsizetype contentEnd = contentStart < 0 ? -1 : svg.indexOf("</text>", contentStart + 1);
    if (contentStart >= 0 && contentEnd >= 0) {
        const QByteArray version = QCoreApplication::applicationVersion()
                                       .toUpper().toHtmlEscaped().toUtf8();
        svg.replace(contentStart + 1, contentEnd - contentStart - 1,
                    "<tspan x=\"0\" y=\"0\">" + version + "</tspan>");
    }
    return svg;
}

} // namespace

void applyFluxBrandFont(QApplication &application)
{
    static const QString family = [] {
        const QStringList files = {
            QStringLiteral("Satoshi-Regular.otf"),
            QStringLiteral("Satoshi-Italic.otf"),
            QStringLiteral("Satoshi-Medium.otf"),
            QStringLiteral("Satoshi-MediumItalic.otf"),
            QStringLiteral("Satoshi-Bold.otf"),
            QStringLiteral("Satoshi-BoldItalic.otf"),
            QStringLiteral("Satoshi-Black.otf"),
            QStringLiteral("Satoshi-BlackItalic.otf"),
        };
        QString result;
        for (const QString &file : files) {
            const int id = QFontDatabase::addApplicationFont(
                QStringLiteral(":/fonts/fonts/") + file);
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (result.isEmpty() && !families.isEmpty())
                result = families.constFirst();
        }
        return result;
    }();

    QFont font = application.font();
    if (!family.isEmpty())
        font.setFamily(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    application.setFont(font);
}

QIcon fluxEncoderIcon()
{
    QFile file(QStringLiteral(":/branding/flux-encoder-icon.svg"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QSvgRenderer renderer(file.readAll());
    if (!renderer.isValid())
        return QIcon(QStringLiteral(":/branding/flux-encoder-icon.svg"));

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 96, 128, 256};
    for (const int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, pixmap.rect());
        icon.addPixmap(pixmap);
    }
    return icon;
}

QPixmap fluxEncoderArtwork(const QSize &logicalSize, qreal devicePixelRatio)
{
    const QByteArray svg = brandedArtworkSvg();
    QSvgRenderer renderer(svg);
    if (!renderer.isValid() || logicalSize.isEmpty())
        return {};

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatio);
    const QSize pixelSize(qMax(1, qRound(logicalSize.width() * dpr)),
                          qMax(1, qRound(logicalSize.height() * dpr)));
    QPixmap pixmap(pixelSize);
    pixmap.fill(Qt::transparent);

    QSizeF artworkSize = renderer.defaultSize();
    if (artworkSize.isEmpty())
        artworkSize = QSizeF(948.81, 363.98);
    artworkSize.scale(QSizeF(pixelSize), Qt::KeepAspectRatio);
    const QRectF target((pixelSize.width() - artworkSize.width()) * 0.5,
                        (pixelSize.height() - artworkSize.height()) * 0.5,
                        artworkSize.width(), artworkSize.height());
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, target);
    painter.end();
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

FluxEncoderSplash::FluxEncoderSplash(const QPixmap &artwork)
    : QSplashScreen(artwork, Qt::WindowStaysOnTopHint)
{
    const int margin = 28;
    const int statusY = std::max(20, qRound(height() * 0.075));
    const int progressX = qRound(width() * 0.30);

    statusLabel_ = new QLabel(QStringLiteral("Starting Flux Encoder..."), this);
    statusLabel_->setGeometry(progressX, statusY,
                              std::max(1, width() - progressX - 110), 24);
    statusLabel_->setStyleSheet(QStringLiteral(
        "color:white;font-size:13px;background:transparent;"));

    percentLabel_ = new QLabel(QStringLiteral("0%"), this);
    percentLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percentLabel_->setGeometry(std::max(margin, width() - 100), statusY, 72, 24);
    percentLabel_->setStyleSheet(QStringLiteral(
        "color:rgba(255,255,255,190);font-size:12px;background:transparent;"));

    progressBar_ = new QProgressBar(this);
    progressBar_->setTextVisible(false);
    progressBar_->setRange(0, 100);
    progressBar_->setGeometry(progressX, statusY + 27,
                              std::max(1, width() - progressX - margin), 6);
    progressBar_->setStyleSheet(QStringLiteral(
        "QProgressBar{border:0;border-radius:3px;background:rgba(255,255,255,45);}"
        "QProgressBar::chunk{border-radius:3px;background:#42a274;}"));
}

void FluxEncoderSplash::setProgress(int percent, const QString &message)
{
    const int bounded = std::clamp(percent, 0, 100);
    statusLabel_->setText(message);
    percentLabel_->setText(QStringLiteral("%1%").arg(bounded));
    progressBar_->setValue(bounded);
    repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void showFluxEncoderAbout(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowIcon(fluxEncoderIcon());
    dialog.setWindowTitle(QObject::tr("About Flux Encoder"));
    dialog.setModal(true);
    dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setFixedSize(760, 292);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *graphic = new ClickableAboutGraphic(&dialog);
    graphic->setPixmap(fluxEncoderArtwork(QSize(760, 292), dialog.devicePixelRatioF()));
    layout->addWidget(graphic);
    dialog.exec();
}

} // namespace flux::ui
