#pragma once

#include "frame-rate-provider.h"

#include <QDoubleSpinBox>
#include <QString>
#include <QValidator>

class TimecodeSpinBox : public QDoubleSpinBox {
public:
    explicit TimecodeSpinBox(QWidget *parent = nullptr);

    static void set_frame_rate_provider(
        const fxm::IFrameRateProvider *provider) noexcept;
    static void set_default_tooltip(const QString &tooltip);
    static QString format_seconds(double seconds);
    static bool parse_timecode(const QString &text, double *seconds_out);
    static bool parse_timecode(const QString &text, double frame_rate, double *seconds_out);

protected:
    QString textFromValue(double value) const override;
    double valueFromText(const QString &text) const override;
    QValidator::State validate(QString &text, int &pos) const override;
    void stepBy(int steps) override;

private:
    static double frame_rate();
    static double frame_duration();
    static int rounded_fps();
    static bool parse_integer_timecode_or_frames(const QString &text, double fps_d, double *seconds_out);
};
