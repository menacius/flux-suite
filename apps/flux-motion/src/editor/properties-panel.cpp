#include "title-editor-internal.h"
#include "text-animator-presets.h"
#include "text-animator-preset-io.h"
#include "external-data-binding-dialog.h"
#include "external-data-settings-dialog.h"
#include "external-data.h"
#include "fxm-modern-controls.h"
#include "title-logger.h"
#include "title-serialization-schema.h"
#include "title-video-runtime.h"

#include <memory>
#include <algorithm>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QInputDialog>
#include <QDataStream>
#include <QBuffer>
#include <QToolButton>
#include <QDir>
#include <QEventLoop>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QTimer>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QSplitter>
#include <QHeaderView>



#include "open-color-palette.h"

namespace {
constexpr double kAudioMinimumDb = -96.0;
constexpr double kAudioMaximumDb = 12.0;

static double audio_gain_to_db(double gain)
{
    if (!std::isfinite(gain) || gain <= 0.0)
        return kAudioMinimumDb;
    return std::clamp(20.0 * std::log10(gain), kAudioMinimumDb,
                      kAudioMaximumDb);
}

static double audio_db_to_gain(double db)
{
    if (!std::isfinite(db) || db <= kAudioMinimumDb)
        return 0.0;
    return std::clamp(std::pow(10.0, db / 20.0), 0.0, 4.0);
}

static int audio_db_to_slider(double db)
{
    return qRound(std::clamp(db, kAudioMinimumDb, kAudioMaximumDb) * 10.0);
}

/* QDoubleSpinBox accepts an explicit leading plus sign, but normally removes
 * it when displaying the committed value. Audio controls keep the authored
 * sign visible so pan reads +100 / 0 / -100 and positive gain reads +dB. */
class SignedAudioSpinBox final : public QDoubleSpinBox {
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    QString textFromValue(double value) const override
    {
        const double zero_threshold = 0.5 * std::pow(10.0, -decimals());
        if (std::abs(value) < zero_threshold)
            value = 0.0;
        const QString number = locale().toString(value, 'f', decimals());
        return value > 0.0 ? QStringLiteral("+") + number : number;
    }
};
}

/* Ordered implementation modules. Keep this list in source order. */
#include "properties-panel/popup-state.inc"
#include "properties-panel/construction-gradient-image-signals.inc"
#include "properties-panel/construction-transform-character.inc"
#include "properties-panel/construction-type-live-shape.inc"
#include "properties-panel/color-gradient-editing.inc"
#include "properties-panel/auto-style-and-property-actions.inc"
#include "properties-panel/property-synchronization.inc"
#include "properties-panel/selection-refresh.inc"
#include "properties-panel/panel-defaults.inc"
/* Full member-function definitions must be included only after the constructor
 * and the legacy ordered implementation chain have returned to file scope. */
#include "properties-panel/text-animator-controls.inc"
