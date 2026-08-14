#include "timecode-spinbox.h"

#include <QLineEdit>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

QString &default_timecode_tooltip()
{
    static QString tooltip;
    return tooltip;
}

} // namespace

TimecodeSpinBox::TimecodeSpinBox(QWidget *parent)
    : QDoubleSpinBox(parent)
{
    setDecimals(3);
    setSingleStep(frame_duration());
    setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    setKeyboardTracking(false);
    setToolTip(default_timecode_tooltip());
    lineEdit()->setPlaceholderText(QStringLiteral("00:00:00:00"));
}

void TimecodeSpinBox::set_frame_rate_provider(
    const fxm::IFrameRateProvider *provider) noexcept
{
    fxm::set_frame_rate_provider(provider);
}

void TimecodeSpinBox::set_default_tooltip(const QString &tooltip)
{
    default_timecode_tooltip() = tooltip;
}

double TimecodeSpinBox::frame_rate()
{
    return fxm::current_frame_rate();
}

double TimecodeSpinBox::frame_duration()
{
    return 1.0 / std::max(1.0, frame_rate());
}

int TimecodeSpinBox::rounded_fps()
{
    return std::max(1, (int)std::round(frame_rate()));
}

QString TimecodeSpinBox::format_seconds(double seconds)
{
    const double fps_d = frame_rate();
    const int fps = rounded_fps();
    const int total_frames = std::max(0, (int)std::round(seconds * fps_d));
    const int frames = total_frames % fps;
    const int total_seconds = total_frames / fps;
    const int display_seconds = total_seconds % 60;
    const int minutes = (total_seconds / 60) % 60;
    const int hours = total_seconds / 3600;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(display_seconds, 2, 10, QChar('0'))
        .arg(frames, 2, 10, QChar('0'));
}

bool TimecodeSpinBox::parse_integer_timecode_or_frames(const QString &text, double fps_d, double *seconds_out)
{
    if (text.isEmpty())
        return false;

    for (const QChar ch : text) {
        if (!ch.isDigit())
            return false;
    }

    const int fps = std::max(1, (int)std::round(fps_d));
    if (text.size() <= 8) {
        int values[4] = {0, 0, 0, 0};
        int end = text.size();
        for (int field = 3; field >= 0 && end > 0; --field) {
            const int start = std::max(0, end - 2);
            bool ok = false;
            values[field] = text.mid(start, end - start).toInt(&ok);
            if (!ok)
                return false;
            end = start;
        }

        if (values[1] < 60 && values[2] < 60 && values[3] < fps) {
            const double parsed_seconds = values[0] * 3600.0 + values[1] * 60.0 +
                                          values[2] + values[3] / fps_d;
            if (seconds_out) *seconds_out = parsed_seconds;
            return std::isfinite(parsed_seconds);
        }
    }

    bool ok = false;
    const qlonglong raw_frames = text.toLongLong(&ok);
    if (!ok || raw_frames < 0)
        return false;

    const double parsed_seconds = (double)raw_frames / fps_d;
    if (seconds_out) *seconds_out = parsed_seconds;
    return std::isfinite(parsed_seconds);
}

bool TimecodeSpinBox::parse_timecode(const QString &text, double *seconds_out)
{
    return parse_timecode(text, frame_rate(), seconds_out);
}

bool TimecodeSpinBox::parse_timecode(const QString &text, double fps_d, double *seconds_out)
{
    fps_d = std::max(1.0, fps_d);

    QString clean = text.trimmed();
    const bool seconds_suffix = clean.endsWith(QLatin1Char('s'), Qt::CaseInsensitive);
    if (seconds_suffix)
        clean.chop(1);
    clean = clean.trimmed();
    if (clean.isEmpty())
        return false;

    if (!seconds_suffix && clean.indexOf(QLatin1Char(':')) < 0 &&
        clean.indexOf(QLatin1Char('.')) < 0 && clean.indexOf(QLatin1Char(',')) < 0) {
        return parse_integer_timecode_or_frames(clean, fps_d, seconds_out);
    }

    if (clean.indexOf(QLatin1Char(':')) < 0) {
        bool ok = false;
        const double decimal_seconds = QLocale::c().toDouble(clean, &ok);
        if (ok) {
            if (seconds_out) *seconds_out = decimal_seconds;
            return std::isfinite(decimal_seconds);
        }
        return false;
    }

    const QStringList parts = clean.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 4)
        return false;

    int values[4] = {0, 0, 0, 0};
    const int offset = 4 - parts.size();
    for (int i = 0; i < parts.size(); ++i) {
        bool part_ok = false;
        values[offset + i] = parts[i].toInt(&part_ok);
        if (!part_ok || values[offset + i] < 0)
            return false;
    }

    const int fps = std::max(1, (int)std::round(fps_d));
    if (values[1] >= 60 || values[2] >= 60 || values[3] >= fps)
        return false;

    const double parsed_seconds = values[0] * 3600.0 + values[1] * 60.0 +
                                  values[2] + values[3] / fps_d;
    if (seconds_out) *seconds_out = parsed_seconds;
    return std::isfinite(parsed_seconds);
}

QString TimecodeSpinBox::textFromValue(double value) const
{
    return format_seconds(value);
}

double TimecodeSpinBox::valueFromText(const QString &text) const
{
    double seconds = value();
    if (parse_timecode(text, &seconds))
        return seconds;
    return value();
}

QValidator::State TimecodeSpinBox::validate(QString &text, int &pos) const
{
    Q_UNUSED(pos);
    double seconds = 0.0;
    if (parse_timecode(text, &seconds) && seconds >= minimum() && seconds <= maximum())
        return QValidator::Acceptable;
    if (text.trimmed().isEmpty())
        return QValidator::Intermediate;
    return QValidator::Intermediate;
}

void TimecodeSpinBox::stepBy(int steps)
{
    const double stepped = value() + steps * singleStep();
    setValue(std::clamp(stepped, minimum(), maximum()));
}
