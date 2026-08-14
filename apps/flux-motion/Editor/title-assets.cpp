#include "title-assets.h"

#include "asset-path-provider.h"

#include <QApplication>
#include <QFontDatabase>
#include <QStringList>

void fxm_set_asset_path_provider(
    const fxm::IAssetPathProvider *provider) noexcept
{
    fxm::set_asset_path_provider(provider);
}

QString fxm_asset_path(const char *relative_path)
{
    if (!relative_path || !*relative_path)
        return {};
    return QString::fromStdString(fxm::resolve_asset_path(relative_path));
}

QFont fxm_satoshi_ui_font()
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

        QString resolved_family;
        for (const QString &file : files) {
            const QString path = fxm_asset_path(
                (QStringLiteral("fonts/") + file).toUtf8().constData());
            if (path.isEmpty())
                continue;
            const int id = QFontDatabase::addApplicationFont(path);
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (resolved_family.isEmpty() && !families.isEmpty())
                resolved_family = families.constFirst();
        }
        return resolved_family;
    }();

    QFont font = qApp ? qApp->font() : QFont();
    if (!family.isEmpty())
        font.setFamily(family);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}
