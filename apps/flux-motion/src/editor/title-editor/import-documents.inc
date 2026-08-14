/* Native document importers.
 *
 * SVG is converted to editable FXM Shape layers. PSD and XCF are decoded in
 * process so the editor never depends on Photoshop, GIMP or ImageMagick being
 * installed on the production machine. Native layer types are retained when
 * FXM has a corresponding model; pixel layers keep their lossless channel
 * data, while the merged option uses the document composite when available. */
namespace fxm_document_import {

constexpr double kPi = 3.1415926535897932384626433832795;

enum class Kind { Pixel, Text, Solid, Shape, Adjustment, Group, GroupEnd };

struct ImportedLayer {
    QString name;
    Kind kind = Kind::Pixel;
    QRect rect;
    QImage pixels;
    QString text;
    QString font_family;
    double font_size = 48.0;
    RichTextDocument rich_text;
    QColor solid_color = Qt::transparent;
    QPainterPath vector_path;
    bool visible = true;
    double opacity = 1.0;
    EffectBlendMode blend = EffectBlendMode::Normal;
    std::vector<LayerEffect> effects;
    int parent = -1;
};

struct ImportedDocument {
    QSize size;
    QVector<ImportedLayer> layers; /* source order: topmost first */
    QImage merged_preview;
    QString error;
};

static quint32 argb(const QColor &color, double extra_alpha = 1.0)
{
    const int a = qBound(0, qRound(color.alpha() * extra_alpha), 255);
    return (quint32(a) << 24) | (quint32(color.red()) << 16) |
           (quint32(color.green()) << 8) | quint32(color.blue());
}

static QColor svg_color(QString value, const QColor &fallback, bool *none = nullptr)
{
    value = value.trimmed();
    if (none) *none = value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0;
    if (value.isEmpty() || (none && *none)) return fallback;
    const QString lower = value.toLower();
    if (lower == QStringLiteral("transparent")) return QColor(0, 0, 0, 0);
    if (lower.startsWith(QStringLiteral("rgb(")) ||
        lower.startsWith(QStringLiteral("rgba("))) {
        const int begin = value.indexOf('(');
        QStringList c = value.mid(begin + 1, value.size() - begin - 2)
                            .split(QRegularExpression(QStringLiteral("\\s*[,/]\\s*")),
                                   Qt::SkipEmptyParts);
        if (c.size() >= 3) {
            auto component = [](QString s) {
                s = s.trimmed();
                return qBound(0, s.endsWith('%')
                    ? qRound(s.chopped(1).toDouble() * 2.55) : qRound(s.toDouble()), 255);
            };
            QColor color(component(c[0]), component(c[1]), component(c[2]));
            if (c.size() >= 4) {
                const QString alpha = c[3].trimmed();
                color.setAlphaF(qBound(0.0, alpha.endsWith('%')
                    ? alpha.chopped(1).toDouble() / 100.0 : alpha.toDouble(), 1.0));
            }
            return color;
        }
    }
    if (lower.startsWith(QStringLiteral("hsl(")) ||
        lower.startsWith(QStringLiteral("hsla("))) {
        const int begin = value.indexOf('(');
        const QStringList c = value.mid(begin + 1, value.size() - begin - 2)
                                  .split(QRegularExpression(QStringLiteral("\\s*[,/]\\s*")),
                                         Qt::SkipEmptyParts);
        if (c.size() >= 3) {
            const double hue = std::fmod(c[0].toDouble(), 360.0) / 360.0;
            QString saturation_text = c[1];
            QString lightness_text = c[2];
            const double saturation = saturation_text.remove('%').toDouble() / 100.0;
            const double lightness = lightness_text.remove('%').toDouble() / 100.0;
            QColor color = QColor::fromHslF(hue < 0.0 ? hue + 1.0 : hue,
                                             qBound(0.0, saturation, 1.0),
                                             qBound(0.0, lightness, 1.0));
            if (c.size() >= 4) {
                QString alpha = c[3].trimmed();
                color.setAlphaF(qBound(0.0, alpha.endsWith('%')
                    ? alpha.chopped(1).toDouble() / 100.0 : alpha.toDouble(), 1.0));
            }
            return color;
        }
    }
    if (value.startsWith('#') && (value.size() == 4 || value.size() == 5)) {
        QString expanded = QStringLiteral("#");
        for (int i = 1; i < value.size(); ++i) expanded += QString(2, value[i]);
        value = expanded;
    }
    if (value.startsWith('#') && value.size() == 9) {
        bool ok = false;
        const quint32 rgba = value.mid(1).toUInt(&ok, 16);
        if (ok) return QColor((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF,
                              (rgba >> 8) & 0xFF, rgba & 0xFF);
    }
    QColor color(value);
    return color.isValid() ? color : fallback;
}

struct SvgStyle {
    QColor fill = Qt::black;
    QColor stroke = Qt::transparent;
    QColor current_color = Qt::black;
    bool fill_none = false;
    bool stroke_none = true;
    double opacity = 1.0;
    double fill_opacity = 1.0;
    double stroke_opacity = 1.0;
    double stroke_width = 1.0;
    QString fill_gradient_id;
    QString stroke_gradient_id;
    int fill_gradient_type = 0;
    int fill_gradient_spread = 0;
    double fill_gradient_angle = 0.0;
    double fill_gradient_center_x = 0.5;
    double fill_gradient_center_y = 0.5;
    double fill_gradient_scale = 1.0;
    double fill_gradient_focal_x = 0.5;
    double fill_gradient_focal_y = 0.5;
    QVector<QPair<double, QColor>> fill_gradient_stops;
    int stroke_gradient_type = 0;
    int stroke_gradient_spread = 0;
    double stroke_gradient_angle = 0.0;
    double stroke_gradient_center_x = 0.5;
    double stroke_gradient_center_y = 0.5;
    double stroke_gradient_scale = 1.0;
    double stroke_gradient_focal_x = 0.5;
    double stroke_gradient_focal_y = 0.5;
    QVector<QPair<double, QColor>> stroke_gradient_stops;
    QString font_family = QStringLiteral("Sans Serif");
    double font_size = 16.0;
    int font_weight = QFont::Normal;
    bool font_italic = false;
    bool font_underline = false;
    bool font_strikethrough = false;
    bool font_kerning = true;
    bool font_ligatures = true;
    bool font_opentype_features = false;
    int text_style = 0;
    double font_scale_x = 1.0;
    double letter_spacing = 0.0;
    double baseline_shift = 0.0;
    QString language = QStringLiteral("English");
    QString text_anchor = QStringLiteral("start");
    int stroke_join_style = 0;
    Qt::FillRule fill_rule = Qt::WindingFill;
};

static QMap<QString, QString> svg_style_map(const QXmlStreamAttributes &attrs)
{
    QMap<QString, QString> out;
    for (const auto &attr : attrs) out.insert(attr.name().toString(), attr.value().toString());
    const QString inline_style = out.value(QStringLiteral("style"));
    for (const QString &entry : inline_style.split(';', Qt::SkipEmptyParts)) {
        const int colon = entry.indexOf(':');
        if (colon > 0) out.insert(entry.left(colon).trimmed(), entry.mid(colon + 1).trimmed());
    }
    return out;
}

static double svg_fraction(QString value, double fallback = 1.0)
{
    value = value.trimmed();
    bool ok = false;
    const double number = value.endsWith('%')
        ? value.chopped(1).toDouble(&ok) / 100.0 : value.toDouble(&ok);
    return ok ? number : fallback;
}

static double svg_length(QString value, double fallback, double em = 16.0)
{
    value = value.trimmed().toLower();
    static const QRegularExpression number_re(
        QStringLiteral("^[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
    const auto match = number_re.match(value);
    if (!match.hasMatch()) return fallback;
    const double number = match.captured().toDouble();
    const QString unit = value.mid(match.capturedLength()).trimmed();
    if (unit == QStringLiteral("pt")) return number * 96.0 / 72.0;
    if (unit == QStringLiteral("pc")) return number * 16.0;
    if (unit == QStringLiteral("mm")) return number * 96.0 / 25.4;
    if (unit == QStringLiteral("cm")) return number * 96.0 / 2.54;
    if (unit == QStringLiteral("in")) return number * 96.0;
    if (unit == QStringLiteral("em")) return number * em;
    if (unit == QStringLiteral("rem")) return number * 16.0;
    if (unit == QStringLiteral("%")) return number * em / 100.0;
    return number;
}

static SvgStyle svg_style(const SvgStyle &parent, const QMap<QString, QString> &values)
{
    SvgStyle out = parent;
    if (values.contains(QStringLiteral("color")))
        out.current_color = svg_color(values.value(QStringLiteral("color")), out.current_color);
    if (values.contains(QStringLiteral("fill"))) {
        const QString fill=values.value(QStringLiteral("fill")).trimmed();
        const QRegularExpressionMatch reference=QRegularExpression(QStringLiteral("^url\\(\\s*#([^\\s)]+)\\s*\\)$")).match(fill);
        if(reference.hasMatch()){out.fill_gradient_id=reference.captured(1);out.fill_none=false;}
        else{out.fill_gradient_id.clear();out.fill_gradient_stops.clear();out.fill=fill.compare(QStringLiteral("currentColor"),Qt::CaseInsensitive)==0?out.current_color:svg_color(fill,out.fill,&out.fill_none);}
    }
    if (values.contains(QStringLiteral("stroke"))) {
        const QString stroke=values.value(QStringLiteral("stroke")).trimmed();
        const auto reference=QRegularExpression(QStringLiteral("^url\\(\\s*#([^\\s)]+)\\s*\\)$")).match(stroke);
        if(reference.hasMatch()){out.stroke_gradient_id=reference.captured(1);out.stroke_none=false;}
        else{out.stroke_gradient_id.clear();out.stroke_gradient_stops.clear();out.stroke=stroke.compare(QStringLiteral("currentColor"),Qt::CaseInsensitive)==0?out.current_color:svg_color(stroke,out.stroke,&out.stroke_none);}
    }
    if (values.contains(QStringLiteral("opacity"))) out.opacity *= svg_fraction(values.value(QStringLiteral("opacity")));
    if (values.contains(QStringLiteral("fill-opacity"))) out.fill_opacity *= svg_fraction(values.value(QStringLiteral("fill-opacity")));
    if (values.contains(QStringLiteral("stroke-opacity"))) out.stroke_opacity *= svg_fraction(values.value(QStringLiteral("stroke-opacity")));
    if (values.contains(QStringLiteral("stroke-width"))) out.stroke_width = svg_length(values.value(QStringLiteral("stroke-width")),out.stroke_width,out.font_size);
    if (values.contains(QStringLiteral("font-family"))) out.font_family=values.value(QStringLiteral("font-family")).split(',').front().trimmed().remove('"').remove('\'');
    if (values.contains(QStringLiteral("font-size"))) out.font_size=qMax(1.0,svg_length(values.value(QStringLiteral("font-size")),out.font_size,out.font_size));
    if (values.contains(QStringLiteral("font-style"))) out.font_italic=values.value(QStringLiteral("font-style")).contains(QStringLiteral("italic"),Qt::CaseInsensitive)||values.value(QStringLiteral("font-style")).contains(QStringLiteral("oblique"),Qt::CaseInsensitive);
    if (values.contains(QStringLiteral("font-weight"))){const QString weight=values.value(QStringLiteral("font-weight"));bool ok=false;const int numeric=weight.toInt(&ok);out.font_weight=weight.compare(QStringLiteral("bold"),Qt::CaseInsensitive)==0?QFont::Bold:(ok?qBound(1,numeric,1000):QFont::Normal);}
    if (values.contains(QStringLiteral("text-decoration"))){const QString decoration=values.value(QStringLiteral("text-decoration")).toLower();out.font_underline=decoration.contains(QStringLiteral("underline"));out.font_strikethrough=decoration.contains(QStringLiteral("line-through"));}
    if (values.contains(QStringLiteral("font-kerning"))) out.font_kerning=values.value(QStringLiteral("font-kerning")).compare(QStringLiteral("none"),Qt::CaseInsensitive)!=0;
    if (values.contains(QStringLiteral("font-variant-ligatures"))) out.font_ligatures=values.value(QStringLiteral("font-variant-ligatures")).compare(QStringLiteral("none"),Qt::CaseInsensitive)!=0;
    if (values.contains(QStringLiteral("font-feature-settings"))) out.font_opentype_features=values.value(QStringLiteral("font-feature-settings")).compare(QStringLiteral("normal"),Qt::CaseInsensitive)!=0;
    if (values.contains(QStringLiteral("font-variant"))) out.text_style=values.value(QStringLiteral("font-variant")).contains(QStringLiteral("small-caps"),Qt::CaseInsensitive)?2:0;
    if (values.contains(QStringLiteral("font-stretch"))){const QString stretch=values.value(QStringLiteral("font-stretch")).toLower();out.font_scale_x=stretch.endsWith('%')?qMax(0.01,stretch.chopped(1).toDouble()/100.0):(stretch.contains(QStringLiteral("condensed"))?0.75:(stretch.contains(QStringLiteral("expanded"))?1.25:1.0));}
    if (values.contains(QStringLiteral("letter-spacing"))) out.letter_spacing=svg_length(values.value(QStringLiteral("letter-spacing")),out.letter_spacing,out.font_size);
    if (values.contains(QStringLiteral("baseline-shift"))){const QString shift=values.value(QStringLiteral("baseline-shift")).trimmed().toLower();out.baseline_shift=shift==QStringLiteral("super")?out.font_size*0.35:(shift==QStringLiteral("sub")?-out.font_size*0.2:svg_length(shift,out.baseline_shift,out.font_size));}
    if (values.contains(QStringLiteral("text-anchor"))) out.text_anchor=values.value(QStringLiteral("text-anchor")).trimmed().toLower();
    if (values.contains(QStringLiteral("lang"))) out.language=values.value(QStringLiteral("lang")).trimmed();
    if (values.contains(QStringLiteral("stroke-linejoin"))){const QString join=values.value(QStringLiteral("stroke-linejoin")).toLower();out.stroke_join_style=join==QStringLiteral("round")?1:(join==QStringLiteral("bevel")?2:0);}
    if (values.contains(QStringLiteral("fill-rule"))) out.fill_rule=values.value(QStringLiteral("fill-rule")).compare(QStringLiteral("evenodd"),Qt::CaseInsensitive)==0?Qt::OddEvenFill:Qt::WindingFill;
    return out;
}

static SvgStyle svg_style(const SvgStyle &parent, const QXmlStreamAttributes &attrs)
{
    return svg_style(parent, svg_style_map(attrs));
}

struct SvgCssRule {
    QString selector;
    QMap<QString, QString> declarations;
    int specificity = 0;
};

static QMap<QString, QString> svg_declarations(const QString &source)
{
    QMap<QString, QString> values;
    for (const QString &entry : source.split(';', Qt::SkipEmptyParts)) {
        const int colon = entry.indexOf(':');
        if (colon > 0)
            values.insert(entry.left(colon).trimmed().toLower(),
                          entry.mid(colon + 1).trimmed());
    }
    return values;
}

static QVector<SvgCssRule> svg_css_rules(const QByteArray &data)
{
    QVector<SvgCssRule> rules;
    QString source = QString::fromUtf8(data);
    source.remove(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")));
    const QRegularExpression style_re(
        QStringLiteral("<style(?:\\s[^>]*)?>([\\s\\S]*?)</style>"),
        QRegularExpression::CaseInsensitiveOption);
    auto style_matches = style_re.globalMatch(source);
    while (style_matches.hasNext()) {
        const QString sheet = style_matches.next().captured(1);
        const QRegularExpression rule_re(QStringLiteral("([^{}]+)\\{([^{}]*)\\}"));
        auto matches = rule_re.globalMatch(sheet);
        while (matches.hasNext()) {
            const auto match = matches.next();
            const auto declarations = svg_declarations(match.captured(2));
            for (QString selector : match.captured(1).split(',', Qt::SkipEmptyParts)) {
                selector = selector.trimmed();
                if (selector.contains(' ') || selector.contains('>') ||
                    selector.contains('+') || selector.contains('~'))
                    continue;
                SvgCssRule rule;
                rule.selector = selector;
                rule.declarations = declarations;
                rule.specificity = selector.count('#') * 100 + selector.count('.') * 10 +
                    (!selector.startsWith('.') && !selector.startsWith('#') ? 1 : 0);
                rules.push_back(rule);
            }
        }
    }
    return rules;
}

static void svg_register_embedded_fonts(const QByteArray &data,
                                        const QString &svg_path)
{
    const QString source = QString::fromUtf8(data);
    const QRegularExpression face_re(
        QStringLiteral("@font-face\\s*\\{([\\s\\S]*?)\\}"),
        QRegularExpression::CaseInsensitiveOption);
    auto faces = face_re.globalMatch(source);
    while (faces.hasNext()) {
        const QString body = faces.next().captured(1);
        const auto src_match = QRegularExpression(
            QStringLiteral("src\\s*:[^;]*url\\(\\s*['\"]?([^'\")]+)"),
            QRegularExpression::CaseInsensitiveOption).match(body);
        if (!src_match.hasMatch()) continue;
        const QString url = src_match.captured(1).trimmed();
        QByteArray font_data;
        if (url.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)) {
            const int comma = url.indexOf(',');
            if (comma < 0 || !url.left(comma).contains(
                    QStringLiteral(";base64"), Qt::CaseInsensitive)) continue;
            const QByteArray encoded = url.mid(comma + 1).toLatin1();
            if (encoded.size() > 32 * 1024 * 1024) continue;
            font_data = QByteArray::fromBase64(encoded);
        } else if (!url.contains(QStringLiteral("://"))) {
            QFile font_file(QFileInfo(svg_path).dir().filePath(url));
            if (font_file.open(QIODevice::ReadOnly) &&
                font_file.size() <= 32 * 1024 * 1024)
                font_data = font_file.readAll();
        }
        if (!font_data.isEmpty()) QFontDatabase::addApplicationFontFromData(font_data);
    }
}

static bool svg_selector_matches(const QString &selector, const QString &tag,
                                 const QString &id, const QSet<QString> &classes)
{
    static const QRegularExpression selector_re(
        QStringLiteral("^([A-Za-z][A-Za-z0-9_-]*)?(?:#([A-Za-z0-9_-]+))?(?:\\.([A-Za-z0-9_.-]+))?$"));
    const auto match = selector_re.match(selector);
    if (!match.hasMatch()) return false;
    if (!match.captured(1).isEmpty() &&
        match.captured(1).compare(tag, Qt::CaseInsensitive) != 0) return false;
    if (!match.captured(2).isEmpty() && match.captured(2) != id) return false;
    const QString class_text = match.captured(3);
    if (!class_text.isEmpty())
        for (const QString &name : class_text.split('.'))
            if (!classes.contains(name)) return false;
    return true;
}

static QMap<QString, QString> svg_style_values(
    const QXmlStreamAttributes &attrs, const QString &tag,
    const QVector<SvgCssRule> &rules)
{
    QMap<QString, QString> values;
    QString inline_style;
    QString id;
    QSet<QString> classes;
    for (const auto &attr : attrs) {
        const QString name = attr.name().toString();
        if (name == QStringLiteral("style")) inline_style = attr.value().toString();
        else values.insert(name, attr.value().toString());
        if (name == QStringLiteral("id")) id = attr.value().toString();
        if (name == QStringLiteral("class")) {
            const QStringList names = attr.value().toString().split(
                QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            for (const QString &class_name : names) classes.insert(class_name);
        }
    }
    QMap<QString, int> specificity;
    for (const SvgCssRule &rule : rules) {
        if (!svg_selector_matches(rule.selector, tag, id, classes)) continue;
        for (auto it = rule.declarations.constBegin(); it != rule.declarations.constEnd(); ++it) {
            if (!specificity.contains(it.key()) || rule.specificity >= specificity.value(it.key())) {
                values.insert(it.key(), it.value());
                specificity.insert(it.key(), rule.specificity);
            }
        }
    }
    const auto inline_values = svg_declarations(inline_style);
    for (auto it = inline_values.constBegin(); it != inline_values.constEnd(); ++it)
        values.insert(it.key(), it.value());
    return values;
}

static QList<double> svg_numbers(const QString &text)
{
    QList<double> out;
    static const QRegularExpression re(QStringLiteral("[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
    auto match = re.globalMatch(text);
    while (match.hasNext()) out.push_back(match.next().captured().toDouble());
    return out;
}

static QTransform svg_transform(const QString &source)
{
    QTransform result;
    static const QRegularExpression re(QStringLiteral("([A-Za-z]+)\\s*\\(([^)]*)\\)"));
    auto matches = re.globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString op = match.captured(1).toLower();
        const QList<double> n = svg_numbers(match.captured(2));
        QTransform local;
        if (op == QStringLiteral("matrix") && n.size() >= 6)
            local = QTransform(n[0], n[1], n[2], n[3], n[4], n[5]);
        else if (op == QStringLiteral("translate") && !n.isEmpty())
            local.translate(n[0], n.size() > 1 ? n[1] : 0.0);
        else if (op == QStringLiteral("scale") && !n.isEmpty())
            local.scale(n[0], n.size() > 1 ? n[1] : n[0]);
        else if (op == QStringLiteral("rotate") && !n.isEmpty()) {
            if (n.size() >= 3) { local.translate(n[1], n[2]); local.rotate(n[0]); local.translate(-n[1], -n[2]); }
            else local.rotate(n[0]);
        } else if (op == QStringLiteral("skewx") && !n.isEmpty())
            local.shear(std::tan(n[0] * kPi / 180.0), 0.0);
        else if (op == QStringLiteral("skewy") && !n.isEmpty())
            local.shear(0.0, std::tan(n[0] * kPi / 180.0));
        result = result * local;
    }
    return result;
}

class SvgPathReader {
public:
    explicit SvgPathReader(QString source) : s_(std::move(source)) {}
    QPainterPath parse()
    {
        QChar command;
        while (skip_separators(), i_ < s_.size()) {
            if (s_[i_].isLetter()) command = s_[i_++];
            if (command.isNull()) break;
            const bool relative = command.isLower();
            const QChar op = command.toUpper();
            if (op == 'Z') { path_.closeSubpath(); current_ = start_; previous_control_ = current_; last_op_=op; command = QChar(); continue; }
            if (op == 'M') {
                QPointF p; if (!point(p, relative)) break;
                path_.moveTo(p); current_ = start_ = p; previous_control_ = p;
                command = relative ? QChar('l') : QChar('L');
            } else if (op == 'L') {
                QPointF p; if (!point(p, relative)) break; path_.lineTo(p); current_ = previous_control_ = p;
            } else if (op == 'H') {
                double x; if (!number(x)) break; if (relative) x += current_.x(); current_.setX(x); path_.lineTo(current_); previous_control_ = current_;
            } else if (op == 'V') {
                double y; if (!number(y)) break; if (relative) y += current_.y(); current_.setY(y); path_.lineTo(current_); previous_control_ = current_;
            } else if (op == 'C') {
                QPointF c1, c2, p; if (!point(c1, relative) || !point(c2, relative) || !point(p, relative)) break;
                path_.cubicTo(c1, c2, p); previous_control_ = c2; current_ = p;
            } else if (op == 'S') {
                QPointF c2, p; if (!point(c2, relative) || !point(p, relative)) break;
                const QPointF c1 = (last_op_=='C'||last_op_=='S')?current_ * 2.0 - previous_control_:current_; path_.cubicTo(c1, c2, p); previous_control_ = c2; current_ = p;
            } else if (op == 'Q') {
                QPointF c, p; if (!point(c, relative) || !point(p, relative)) break;
                path_.quadTo(c, p); previous_control_ = c; current_ = p;
            } else if (op == 'T') {
                QPointF p; if (!point(p, relative)) break;
                const QPointF c = (last_op_=='Q'||last_op_=='T')?current_ * 2.0 - previous_control_:current_; path_.quadTo(c, p); previous_control_ = c; current_ = p;
            } else if (op == 'A') {
                double rx, ry, rotation, large, sweep; QPointF p;
                if (!number(rx) || !number(ry) || !number(rotation) || !number(large) || !number(sweep) || !point(p, relative)) break;
                arc_to(std::abs(rx), std::abs(ry), rotation, large != 0.0, sweep != 0.0, p); previous_control_ = current_ = p;
            } else break;
            last_op_=op;
        }
        return path_;
    }
private:
    void skip_separators() { while (i_ < s_.size() && (s_[i_].isSpace() || s_[i_] == ',')) ++i_; }
    bool number(double &out)
    {
        skip_separators(); if (i_ >= s_.size() || s_[i_].isLetter()) return false;
        static const QRegularExpression re(QStringLiteral("^[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
        const auto m = re.match(s_.mid(i_)); if (!m.hasMatch()) return false;
        out = m.captured().toDouble(); i_ += m.capturedLength(); return true;
    }
    bool point(QPointF &out, bool relative)
    {
        double x, y; if (!number(x) || !number(y)) return false;
        out = QPointF(x, y); if (relative) out += current_; return true;
    }
    void arc_to(double rx, double ry, double angle, bool large, bool sweep, const QPointF &end)
    {
        if (rx <= 0.0 || ry <= 0.0 || end == current_) { path_.lineTo(end); return; }
        const double phi = std::fmod(angle, 360.0) * kPi / 180.0;
        const double cp = std::cos(phi), sp = std::sin(phi);
        const double dx = (current_.x() - end.x()) / 2.0, dy = (current_.y() - end.y()) / 2.0;
        const double xp = cp * dx + sp * dy, yp = -sp * dx + cp * dy;
        const double lambda = xp*xp/(rx*rx) + yp*yp/(ry*ry);
        if (lambda > 1.0) { const double k = std::sqrt(lambda); rx *= k; ry *= k; }
        const double numerator = std::max(0.0, rx*rx*ry*ry - rx*rx*yp*yp - ry*ry*xp*xp);
        const double denominator = rx*rx*yp*yp + ry*ry*xp*xp;
        const double sign = (large == sweep) ? -1.0 : 1.0;
        const double factor = denominator > 0.0 ? sign * std::sqrt(numerator / denominator) : 0.0;
        const double cxp = factor * rx * yp / ry, cyp = factor * -ry * xp / rx;
        const QPointF center(cp*cxp - sp*cyp + (current_.x()+end.x())/2.0,
                             sp*cxp + cp*cyp + (current_.y()+end.y())/2.0);
        auto vector_angle = [](double ux, double uy, double vx, double vy) {
            const double dot = ux*vx + uy*vy;
            const double len = std::sqrt((ux*ux+uy*uy)*(vx*vx+vy*vy));
            double a = std::acos(qBound(-1.0, len > 0.0 ? dot/len : 1.0, 1.0));
            if (ux*vy - uy*vx < 0.0) a = -a; return a;
        };
        const double ux = (xp-cxp)/rx, uy = (yp-cyp)/ry;
        const double vx = (-xp-cxp)/rx, vy = (-yp-cyp)/ry;
        double theta = vector_angle(1, 0, ux, uy), delta = vector_angle(ux, uy, vx, vy);
        if (!sweep && delta > 0) delta -= 2*kPi; else if (sweep && delta < 0) delta += 2*kPi;
        const int segments = qMax(1, int(std::ceil(std::abs(delta)/(kPi/2.0))));
        const double step = delta / segments;
        for (int n = 0; n < segments; ++n) {
            const double a0 = theta+n*step, a1 = a0+step, k = 4.0/3.0*std::tan((a1-a0)/4.0);
            auto map = [&](double x, double y) { return QPointF(center.x()+cp*rx*x-sp*ry*y, center.y()+sp*rx*x+cp*ry*y); };
            const QPointF c1 = map(std::cos(a0)-k*std::sin(a0), std::sin(a0)+k*std::cos(a0));
            const QPointF c2 = map(std::cos(a1)+k*std::sin(a1), std::sin(a1)-k*std::cos(a1));
            path_.cubicTo(c1, c2, map(std::cos(a1), std::sin(a1)));
        }
    }
    QString s_; int i_ = 0; QPainterPath path_; QPointF current_, start_, previous_control_; QChar last_op_;
};

struct SvgTextRun {
    size_t start = 0;
    size_t length = 0;
    SvgStyle style;
};

struct SvgShape {
    QString name;
    QPainterPath path;
    SvgStyle style;
    bool closed = true;
    bool native_text = false;
    QString text;
    QVector<SvgTextRun> text_runs;
    QRectF text_local_bounds;
    QTransform text_transform;
};

struct SvgGradient {
    int type = 0;
    int spread = 0;
    QString href;
    QString x1 = QStringLiteral("0%");
    QString y1 = QStringLiteral("0%");
    QString x2 = QStringLiteral("100%");
    QString y2 = QStringLiteral("0%");
    QString cx = QStringLiteral("50%");
    QString cy = QStringLiteral("50%");
    QString radius = QStringLiteral("50%");
    QString fx;
    QString fy;
    QTransform transform;
    QVector<QPair<double, QColor>> stops;
};

static double svg_gradient_coordinate(const QString &text, double fallback,
                                      double origin, double extent)
{
    const QString value = text.trimmed();
    if (value.isEmpty()) return fallback;
    if (value.endsWith('%')) return origin + value.chopped(1).toDouble() * extent / 100.0;
    return value.toDouble();
}

static void apply_svg_gradient(SvgStyle &style, const SvgGradient &gradient,
                               const QRectF &bounds, bool stroke)
{
    const double x1 = svg_gradient_coordinate(gradient.x1, bounds.left(), bounds.left(), bounds.width());
    const double y1 = svg_gradient_coordinate(gradient.y1, bounds.top(), bounds.top(), bounds.height());
    const double x2 = svg_gradient_coordinate(gradient.x2, bounds.right(), bounds.left(), bounds.width());
    const double y2 = svg_gradient_coordinate(gradient.y2, bounds.top(), bounds.top(), bounds.height());
    const QPointF start = gradient.transform.map(QPointF(x1, y1));
    const QPointF end = gradient.transform.map(QPointF(x2, y2));
    const double angle = std::atan2(end.y() - start.y(), end.x() - start.x()) *
        57.295779513082320876798154814105;
    const double cx = svg_gradient_coordinate(gradient.cx, bounds.center().x(), bounds.left(), bounds.width());
    const double cy = svg_gradient_coordinate(gradient.cy, bounds.center().y(), bounds.top(), bounds.height());
    const double fx = svg_gradient_coordinate(gradient.fx, cx, bounds.left(), bounds.width());
    const double fy = svg_gradient_coordinate(gradient.fy, cy, bounds.top(), bounds.height());
    const double radius = qMax(1e-6, svg_gradient_coordinate(
        gradient.radius, qMax(bounds.width(), bounds.height()) * 0.5, 0.0,
        qMax(bounds.width(), bounds.height())));
    const QPointF center = gradient.transform.map(QPointF(cx, cy));
    const QPointF focal = gradient.transform.map(QPointF(fx, fy));
    const double center_x = bounds.width() > 0.0 ? (center.x() - bounds.left()) / bounds.width() : 0.5;
    const double center_y = bounds.height() > 0.0 ? (center.y() - bounds.top()) / bounds.height() : 0.5;
    const double focal_x = bounds.width() > 0.0 ? (focal.x() - bounds.left()) / bounds.width() : 0.5;
    const double focal_y = bounds.height() > 0.0 ? (focal.y() - bounds.top()) / bounds.height() : 0.5;
    const double scale = radius / qMax(1e-6, qMax(bounds.width(), bounds.height()) * 0.5);
    if (stroke) {
        style.stroke_gradient_type = gradient.type;
        style.stroke_gradient_spread = gradient.spread;
        style.stroke_gradient_angle = angle;
        style.stroke_gradient_center_x = center_x;
        style.stroke_gradient_center_y = center_y;
        style.stroke_gradient_focal_x = focal_x;
        style.stroke_gradient_focal_y = focal_y;
        style.stroke_gradient_scale = scale;
        style.stroke_gradient_stops = gradient.stops;
    } else {
        style.fill_gradient_type = gradient.type;
        style.fill_gradient_spread = gradient.spread;
        style.fill_gradient_angle = angle;
        style.fill_gradient_center_x = center_x;
        style.fill_gradient_center_y = center_y;
        style.fill_gradient_focal_x = focal_x;
        style.fill_gradient_focal_y = focal_y;
        style.fill_gradient_scale = scale;
        style.fill_gradient_stops = gradient.stops;
    }
}

static QPainterPath svg_element_path(const QString &tag, const QMap<QString, QString> &a, bool &closed)
{
    QPainterPath path; closed = true;
    auto d = [&](const char *name, double fallback = 0.0) {
        bool ok = false;
        const double value = a.value(QString::fromLatin1(name)).toDouble(&ok);
        return ok ? value : fallback;
    };
    if (tag == QStringLiteral("path")) return SvgPathReader(a.value(QStringLiteral("d"))).parse();
    if (tag == QStringLiteral("rect")) {
        const QRectF r(d("x"), d("y"), d("width"), d("height"));
        const double rx = d("rx");
        const double ry = d("ry", rx);
        path.addRoundedRect(r, rx, ry);
    } else if (tag == QStringLiteral("circle")) {
        const double r = d("r"); path.addEllipse(QPointF(d("cx"), d("cy")), r, r);
    } else if (tag == QStringLiteral("ellipse")) {
        path.addEllipse(QPointF(d("cx"), d("cy")), d("rx"), d("ry"));
    } else if (tag == QStringLiteral("line")) {
        path.moveTo(d("x1"), d("y1")); path.lineTo(d("x2"), d("y2")); closed = false;
    } else if (tag == QStringLiteral("polyline") || tag == QStringLiteral("polygon")) {
        const QList<double> n = svg_numbers(a.value(QStringLiteral("points")));
        if (n.size() >= 2) { path.moveTo(n[0], n[1]); for (int i=2; i+1<n.size(); i+=2) path.lineTo(n[i], n[i+1]); }
        closed = tag == QStringLiteral("polygon"); if (closed) path.closeSubpath();
    }
    return path;
}

static QFont svg_font(const SvgStyle &style)
{
    QFont font(style.font_family);
    font.setPixelSize(qMax(1, qRound(style.font_size)));
    font.setWeight(static_cast<QFont::Weight>(qBound(1, style.font_weight, 1000)));
    font.setItalic(style.font_italic);
    font.setUnderline(style.font_underline);
    font.setStrikeOut(style.font_strikethrough);
    font.setKerning(style.font_kerning);
    font.setStretch(qBound(1, qRound(style.font_scale_x * 100.0), 4000));
    if (std::abs(style.letter_spacing) > 1e-9)
        font.setLetterSpacing(QFont::AbsoluteSpacing, style.letter_spacing);
    return font;
}

static void read_svg_text_node(QXmlStreamReader &xml, const SvgStyle &node_style,
                               const QVector<SvgCssRule> &css_rules,
                               SvgShape &shape, QPainterPath &outline,
                               QPointF &cursor, double line_origin_x)
{
    auto append_text = [&](QString text, const SvgStyle &style) {
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        if (text.trimmed().isEmpty() && text.contains(QLatin1Char('\n')))
            return;
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
        for (int line_index = 0; line_index < lines.size(); ++line_index) {
            const QString &line = lines[line_index];
            if (!line.isEmpty()) {
                const QByteArray existing = shape.text.toUtf8();
                const QByteArray encoded = line.toUtf8();
                SvgTextRun run;
                run.start = size_t(existing.size());
                run.length = size_t(encoded.size());
                run.style = style;
                shape.text += line;
                shape.text_runs.push_back(run);

                const QFont font = svg_font(style);
                QPainterPath glyphs;
                glyphs.addText(QPointF(cursor.x(), cursor.y() - style.baseline_shift),
                               font, line);
                glyphs.setFillRule(style.fill_rule);
                outline.addPath(glyphs);
                cursor.rx() += QFontMetricsF(font).horizontalAdvance(line);
            }
            if (line_index + 1 < lines.size()) {
                shape.text += QLatin1Char('\n');
                cursor.setX(line_origin_x);
                cursor.ry() += QFontMetricsF(svg_font(style)).height();
            }
        }
    };

    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::Characters ||
            token == QXmlStreamReader::EntityReference) {
            append_text(xml.text().toString(), node_style);
        } else if (token == QXmlStreamReader::StartElement) {
            const QString tag = xml.name().toString().toLower();
            const auto values = svg_style_values(xml.attributes(), tag, css_rules);
            const SvgStyle child_style = svg_style(node_style, values);
            QPointF child_cursor = cursor;
            if (values.contains(QStringLiteral("x")))
                child_cursor.setX(svg_length(values.value(QStringLiteral("x")),
                                             child_cursor.x(), child_style.font_size));
            if (values.contains(QStringLiteral("y")))
                child_cursor.setY(svg_length(values.value(QStringLiteral("y")),
                                             child_cursor.y(), child_style.font_size));
            child_cursor.rx() += svg_length(values.value(QStringLiteral("dx")),
                                             0.0, child_style.font_size);
            child_cursor.ry() += svg_length(values.value(QStringLiteral("dy")),
                                             0.0, child_style.font_size);
            read_svg_text_node(xml, child_style, css_rules, shape, outline,
                               child_cursor, child_cursor.x());
            cursor = child_cursor;
        } else if (token == QXmlStreamReader::EndElement) {
            return;
        }
    }
}

static bool read_svg(const QString &path, QVector<SvgShape> &shapes, QRectF &view_box, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > 128*1024*1024) { error = fxm_tr("OBSTitles.ImportDocumentTooLarge"); return false; }
    const QByteArray data = file.readAll();
    svg_register_embedded_fonts(data, path);
    const QVector<SvgCssRule> css_rules = svg_css_rules(data);
    QXmlStreamReader xml(data);
    struct Context { SvgStyle style; QTransform transform; bool renderable = true; };
    QVector<Context> stack; stack.push_back({SvgStyle(), QTransform(), true});
    QMap<QString, SvgShape> definitions;
    QMap<QString, SvgGradient> gradients;
    QString active_gradient;
    int unnamed = 1;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = xml.name().toString().toLower();
            const auto attrs = xml.attributes();
            const auto map = svg_style_values(attrs, tag, css_rules);
            if(tag==QStringLiteral("lineargradient")||tag==QStringLiteral("radialgradient")){
                active_gradient=map.value(QStringLiteral("id"));
                if(!active_gradient.isEmpty()){
                    SvgGradient gradient;gradient.type=tag==QStringLiteral("radialgradient")?1:0;
                    gradient.x1=map.value(QStringLiteral("x1"),gradient.x1);gradient.y1=map.value(QStringLiteral("y1"),gradient.y1);gradient.x2=map.value(QStringLiteral("x2"),gradient.x2);gradient.y2=map.value(QStringLiteral("y2"),gradient.y2);
                    gradient.cx=map.value(QStringLiteral("cx"),gradient.cx);gradient.cy=map.value(QStringLiteral("cy"),gradient.cy);gradient.radius=map.value(QStringLiteral("r"),gradient.radius);gradient.fx=map.value(QStringLiteral("fx"));gradient.fy=map.value(QStringLiteral("fy"));
                    gradient.href=map.value(QStringLiteral("href"));if(gradient.href.isEmpty())gradient.href=map.value(QStringLiteral("xlink:href"));if(gradient.href.startsWith('#'))gradient.href.remove(0,1);
                    const QString spread=map.value(QStringLiteral("spreadMethod")).toLower();gradient.spread=spread==QStringLiteral("reflect")?1:(spread==QStringLiteral("repeat")?2:0);
                    gradient.transform=svg_transform(map.value(QStringLiteral("gradientTransform")));gradients.insert(active_gradient,gradient);
                }
            }
            else if(tag==QStringLiteral("stop")&&!active_gradient.isEmpty()&&gradients.contains(active_gradient)){QString offset_text=map.value(QStringLiteral("offset"));double offset=offset_text.endsWith('%')?offset_text.chopped(1).toDouble()/100.0:offset_text.toDouble();QColor color=svg_color(map.value(QStringLiteral("stop-color"),QStringLiteral("black")),Qt::black);color.setAlphaF(qBound(0.0,svg_fraction(map.value(QStringLiteral("stop-opacity"),QStringLiteral("1"))),1.0));gradients[active_gradient].stops.push_back({qBound(0.0,offset,1.0),color});}
            if (tag == QStringLiteral("svg")) {
                const QList<double> vb = svg_numbers(map.value(QStringLiteral("viewBox")));
                if (vb.size() >= 4) view_box = QRectF(vb[0], vb[1], vb[2], vb[3]);
                if (!view_box.isValid()) {const QList<double> sw=svg_numbers(map.value(QStringLiteral("width"))),sh=svg_numbers(map.value(QStringLiteral("height")));view_box=QRectF(0,0,sw.isEmpty()?0.0:sw.front(),sh.isEmpty()?0.0:sh.front());}
            }
            const bool definition_container = tag==QStringLiteral("defs")||tag==QStringLiteral("clippath")||tag==QStringLiteral("mask")||tag==QStringLiteral("symbol")||tag==QStringLiteral("pattern");
            Context context;
            context.style=svg_style(stack.back().style,map);
            context.transform=stack.back().transform*svg_transform(map.value(QStringLiteral("transform")));
            context.renderable=stack.back().renderable&&!definition_container;
            stack.push_back(context);
            static const QSet<QString> shape_tags = {QStringLiteral("path"), QStringLiteral("rect"), QStringLiteral("circle"), QStringLiteral("ellipse"), QStringLiteral("line"), QStringLiteral("polyline"), QStringLiteral("polygon")};
            if (shape_tags.contains(tag)) {
                bool closed = true; QPainterPath p = svg_element_path(tag, map, closed);
                p.setFillRule(context.style.fill_rule);
                p = context.transform.map(p);
                if (!p.isEmpty()) {
                    QString name = map.value(QStringLiteral("id"));
                    if (name.isEmpty()) name = QStringLiteral("Shape %1").arg(unnamed++);
                    SvgShape shape;shape.name=name;shape.path=p;shape.style=context.style;shape.closed=closed;
                    if (context.renderable) shapes.push_back(shape);
                    else if (!map.value(QStringLiteral("id")).isEmpty()) definitions.insert(map.value(QStringLiteral("id")), shape);
                }
            } else if (context.renderable && tag==QStringLiteral("use")) {
                QString reference=map.value(QStringLiteral("href"));if(reference.isEmpty())reference=map.value(QStringLiteral("xlink:href"));if(reference.startsWith('#'))reference.remove(0,1);
                if(definitions.contains(reference)){SvgShape shape=definitions.value(reference);QTransform placement=context.transform;placement.translate(map.value(QStringLiteral("x")).toDouble(),map.value(QStringLiteral("y")).toDouble());shape.path=placement.map(shape.path);const QString id=map.value(QStringLiteral("id"));if(!id.isEmpty())shape.name=id;shapes.push_back(shape);}
            } else if (tag==QStringLiteral("text")) {
                SvgShape shape;shape.style=context.style;shape.closed=true;
                QPointF cursor(svg_length(map.value(QStringLiteral("x")),0.0,context.style.font_size),svg_length(map.value(QStringLiteral("y")),0.0,context.style.font_size));
                QPainterPath untransformed;read_svg_text_node(xml,context.style,css_rules,shape,untransformed,cursor,cursor.x());
                if(context.style.text_anchor!=QStringLiteral("start")&&!untransformed.isEmpty()){const double shift=context.style.text_anchor==QStringLiteral("middle")?untransformed.boundingRect().width()/2.0:untransformed.boundingRect().width();QTransform anchor_shift;anchor_shift.translate(-shift,0.0);untransformed=anchor_shift.map(untransformed);}
                shape.text_local_bounds=untransformed.boundingRect();shape.text_transform=context.transform;shape.path=context.transform.map(untransformed);const double axis_dot=context.transform.m11()*context.transform.m21()+context.transform.m12()*context.transform.m22();const double axis_length=std::hypot(context.transform.m11(),context.transform.m12())*std::hypot(context.transform.m21(),context.transform.m22());shape.native_text=axis_length>1e-9&&std::abs(axis_dot)<=axis_length*1e-6&&!shape.text.isEmpty();
                QString name=map.value(QStringLiteral("id"));if(name.isEmpty())name=shape.text.simplified().left(40);if(name.isEmpty())name=QStringLiteral("Text %1").arg(unnamed++);shape.name=name;
                if(!shape.path.isEmpty()){if(context.renderable)shapes.push_back(shape);else if(!map.value(QStringLiteral("id")).isEmpty())definitions.insert(map.value(QStringLiteral("id")),shape);}
                stack.pop_back();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString tag=xml.name().toString().toLower();if((tag==QStringLiteral("lineargradient")||tag==QStringLiteral("radialgradient")))active_gradient.clear();if(stack.size()>1)stack.pop_back();
        }
    }
    if (xml.hasError()) { error = xml.errorString(); return false; }
    if (shapes.size() > 100000) { error = fxm_tr("OBSTitles.ImportDocumentTooLarge"); return false; }
    std::function<SvgGradient(const QString &,QSet<QString>)> resolve_gradient;
    resolve_gradient=[&](const QString &id,QSet<QString> visiting){SvgGradient gradient=gradients.value(id);if(gradient.href.isEmpty()||visiting.contains(id)||!gradients.contains(gradient.href))return gradient;visiting.insert(id);const SvgGradient parent=resolve_gradient(gradient.href,visiting);if(gradient.stops.isEmpty())gradient.stops=parent.stops;if(gradient.transform.isIdentity())gradient.transform=parent.transform;if(gradient.spread==0)gradient.spread=parent.spread;return gradient;};
    for(auto &shape:shapes){const QRectF bounds=shape.path.boundingRect();if(!shape.style.fill_gradient_id.isEmpty()&&gradients.contains(shape.style.fill_gradient_id))apply_svg_gradient(shape.style,resolve_gradient(shape.style.fill_gradient_id,{}),bounds,false);if(!shape.style.stroke_gradient_id.isEmpty()&&gradients.contains(shape.style.stroke_gradient_id))apply_svg_gradient(shape.style,resolve_gradient(shape.style.stroke_gradient_id,{}),bounds,true);for(auto &run:shape.text_runs){if(!run.style.fill_gradient_id.isEmpty()&&gradients.contains(run.style.fill_gradient_id))apply_svg_gradient(run.style,resolve_gradient(run.style.fill_gradient_id,{}),bounds,false);if(!run.style.stroke_gradient_id.isEmpty()&&gradients.contains(run.style.stroke_gradient_id))apply_svg_gradient(run.style,resolve_gradient(run.style.stroke_gradient_id,{}),bounds,true);}}
    if (shapes.isEmpty()) { error = fxm_tr("OBSTitles.ImportSvgNoShapes"); return false; }
    if (!view_box.isValid()) { view_box = shapes.front().path.boundingRect(); for (const auto &s : shapes) view_box |= s.path.boundingRect(); }
    return true;
}

static std::vector<BezierPathPoint> painter_path_points(const QPainterPath &path, const QRectF &bounds)
{
    std::vector<BezierPathPoint> out;
    const double w = qMax(1e-9, bounds.width()), h = qMax(1e-9, bounds.height());
    auto norm = [&](const QPointF &p) { return QPointF((p.x()-bounds.left())/w, (p.y()-bounds.top())/h); };
    int last_anchor = -1;
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto e = path.elementAt(i);
        if (e.type == QPainterPath::MoveToElement || e.type == QPainterPath::LineToElement) {
            BezierPathPoint point; const QPointF n = norm(QPointF(e.x,e.y)); point.x=n.x(); point.y=n.y();
            point.in_x=point.out_x=point.x; point.in_y=point.out_y=point.y;
            point.starts_subpath = e.type == QPainterPath::MoveToElement && !out.empty(); out.push_back(point); last_anchor = int(out.size())-1;
        } else if (e.type == QPainterPath::CurveToElement && i+2 < path.elementCount()) {
            const auto c2 = path.elementAt(i+1), end = path.elementAt(i+2);
            if (last_anchor >= 0) { const QPointF n = norm(QPointF(e.x,e.y)); out[last_anchor].out_x=n.x(); out[last_anchor].out_y=n.y(); out[last_anchor].has_out=true; }
            BezierPathPoint point; const QPointF p=norm(QPointF(end.x,end.y)), in=norm(QPointF(c2.x,c2.y));
            point.x=p.x(); point.y=p.y(); point.in_x=in.x(); point.in_y=in.y(); point.has_in=true; point.out_x=point.x; point.out_y=point.y;
            out.push_back(point); last_anchor=int(out.size())-1; i+=2;
        }
    }
    return out;
}

static RichTextFill svg_rich_text_fill(const SvgStyle &style, bool stroke)
{
    RichTextFill fill;
    const auto &stops = stroke ? style.stroke_gradient_stops
                               : style.fill_gradient_stops;
    const double opacity = qBound(0.0, style.opacity *
        (stroke ? style.stroke_opacity : style.fill_opacity), 1.0);
    const QColor color = stroke ? style.stroke : style.fill;
    fill.type = stops.size() >= 2 ? 1 : 0;
    fill.color = argb(color, opacity);
    if (fill.type == 1) {
        fill.gradient_type = stroke ? style.stroke_gradient_type
                                    : style.fill_gradient_type;
        fill.gradient_spread = stroke ? style.stroke_gradient_spread
                                      : style.fill_gradient_spread;
        fill.gradient_angle = float(stroke ? style.stroke_gradient_angle
                                           : style.fill_gradient_angle);
        fill.gradient_center_x = float(stroke ? style.stroke_gradient_center_x
                                              : style.fill_gradient_center_x);
        fill.gradient_center_y = float(stroke ? style.stroke_gradient_center_y
                                              : style.fill_gradient_center_y);
        fill.gradient_scale = float(stroke ? style.stroke_gradient_scale
                                           : style.fill_gradient_scale);
        fill.gradient_focal_x = float(stroke ? style.stroke_gradient_focal_x
                                             : style.fill_gradient_focal_x);
        fill.gradient_focal_y = float(stroke ? style.stroke_gradient_focal_y
                                             : style.fill_gradient_focal_y);
        fill.gradient_start_color = argb(stops.front().second, opacity);
        fill.gradient_end_color = argb(stops.back().second, opacity);
        fill.gradient_start_pos = float(stops.front().first);
        fill.gradient_end_pos = float(stops.back().first);
        fill.gradient_start_opacity = 1.0f;
        fill.gradient_end_opacity = 1.0f;
        fill.gradient_opacity = 1.0f;
    }
    return fill;
}

static RichTextCharFormat svg_rich_text_format(const SvgStyle &style)
{
    RichTextCharFormat format;
    format.font_family = style.font_family.toStdString();
    format.font_style = style.font_italic
        ? (style.font_weight >= QFont::DemiBold ? "Bold Italic" : "Italic")
        : (style.font_weight >= QFont::DemiBold ? "Bold" : "Regular");
    format.font_size = qMax(1, qRound(style.font_size));
    format.bold = style.font_weight >= QFont::DemiBold;
    format.italic = style.font_italic;
    format.underline = style.font_underline;
    format.strikethrough = style.font_strikethrough;
    format.kerning = style.font_kerning;
    format.ligatures = style.font_ligatures;
    format.opentype_features = style.font_opentype_features;
    format.text_style = style.text_style;
    format.tracking = float(style.letter_spacing);
    format.scale_x = float(style.font_scale_x);
    format.baseline_shift = float(style.baseline_shift);
    format.language = style.language.toStdString();
    format.fill = svg_rich_text_fill(style, false);
    if (style.fill_none) format.fill.color = 0;
    format.stroke.enabled = !style.stroke_none && style.stroke_width > 0.0;
    format.stroke.width = float(style.stroke_width);
    format.stroke.opacity = 1.0f;
    format.stroke.join_style = style.stroke_join_style;
    format.stroke.fill = svg_rich_text_fill(style, true);
    return format;
}

class BeReader {
public:
    explicit BeReader(const QByteArray &data, qsizetype start = 0, qsizetype limit = -1)
        : data_(data), pos_(start), end_(limit < 0 ? data.size() : qMin(data.size(), start + limit)) {}
    bool ok(qsizetype count = 0) const { return pos_ >= 0 && pos_ + count <= end_; }
    qsizetype pos() const { return pos_; }
    qsizetype end() const { return end_; }
    void seek(qsizetype value) { pos_ = value; }
    void skip(qsizetype count) { pos_ = qMin(end_, pos_ + count); }
    quint8 u8() { if (!ok(1)) return 0; return quint8(data_[pos_++]); }
    quint16 u16() { quint16 v=0; for(int i=0;i<2;++i) v=(v<<8)|u8(); return v; }
    qint16 s16() { return qint16(u16()); }
    quint32 u32() { quint32 v=0; for(int i=0;i<4;++i) v=(v<<8)|u8(); return v; }
    qint32 s32() { return qint32(u32()); }
    quint64 u64() { quint64 v=0; for(int i=0;i<8;++i) v=(v<<8)|u8(); return v; }
    QByteArray bytes(qsizetype count) { if (!ok(count)) { pos_=end_; return {}; } const QByteArray v=data_.mid(pos_,count); pos_+=count; return v; }
    QByteArray at(qsizetype offset, qsizetype count) const { return offset >= 0 && offset+count <= data_.size() ? data_.mid(offset,count) : QByteArray(); }
private:
    const QByteArray &data_;
    qsizetype pos_=0;
    qsizetype end_=0;
};

static QString utf16_be(const QByteArray &bytes)
{
    QString out; out.reserve(bytes.size()/2);
    for (int i=0; i+1<bytes.size(); i+=2) out.append(QChar((quint8(bytes[i])<<8)|quint8(bytes[i+1])));
    return out;
}

static EffectBlendMode imported_blend(const QByteArray &key)
{
    /* FXM currently exposes six compositor modes. Preserve the visual family
     * of Photoshop's extended modes instead of silently collapsing all of
     * them to Normal. */
    const QByteArray mode = key.toLower();
    if (mode == "mul " || mode == "mltp" || mode == "dark" || mode == "drkn" ||
        mode == "dkcl" || mode == "idiv" || mode == "lbrn" ||
        mode == "cbrn" || mode == "linearburn" || mode == "darkercolor")
        return EffectBlendMode::Multiply;
    if (mode == "scrn" || mode == "lite" || mode == "lghn" || mode == "lgcl" ||
        mode == "div " || mode == "lddg" || mode == "cddg" ||
        mode == "lightercolor")
        return EffectBlendMode::Screen;
    if (mode == "over" || mode == "ovrl" || mode == "slit" || mode == "sftl" ||
        mode == "hlit" || mode == "hrdl" || mode == "vlit" ||
        mode == "llit" || mode == "plit" || mode == "hmix" ||
        mode == "vividlight" || mode == "linearlight" || mode == "pinlight" ||
        mode == "hardmix" || mode == "diff" || mode == "dfrn" ||
        mode == "xclu" || mode == "smud")
        return EffectBlendMode::Overlay;
    if (mode == "add " || mode == "lineardodge")
        return EffectBlendMode::Additive;
    if (mode == "hue " || mode == "h   " || mode == "sat " || mode == "strt" ||
        mode == "colr" || mode == "clr " || mode == "lum " || mode == "lmns")
        return EffectBlendMode::Color;
    return EffectBlendMode::Normal;
}

static double psd_double(BeReader &reader)
{
    const quint64 bits = reader.u64();
    double value = 0.0;
    static_assert(sizeof(value) == sizeof(bits));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static QString psd_descriptor_id(BeReader &reader)
{
    const quint32 length = reader.u32();
    const QByteArray bytes = reader.bytes(length == 0 ? 4 : length);
    return QString::fromLatin1(bytes);
}

static QJsonObject psd_descriptor(BeReader &reader, int depth = 0);

static QJsonValue psd_descriptor_value(BeReader &reader, const QByteArray &type,
                                       int depth)
{
    if (depth > 24) return {};
    if (type == "Objc" || type == "GlbO")
        return psd_descriptor(reader, depth + 1);
    if (type == "VlLs") {
        QJsonArray list;
        const quint32 count = reader.u32();
        if (count > 100000) return list;
        for (quint32 i = 0; i < count && reader.ok(4); ++i) {
            const QByteArray item_type = reader.bytes(4);
            list.push_back(psd_descriptor_value(reader, item_type, depth + 1));
        }
        return list;
    }
    if (type == "doub") return psd_double(reader);
    if (type == "UntF") { reader.skip(4); return psd_double(reader); }
    if (type == "long") return reader.s32();
    if (type == "bool") return reader.u8() != 0;
    if (type == "enum") {
        psd_descriptor_id(reader);
        return psd_descriptor_id(reader);
    }
    if (type == "TEXT") {
        const quint32 chars = reader.u32();
        return chars < 1000000 && reader.ok(qsizetype(chars) * 2)
            ? utf16_be(reader.bytes(qsizetype(chars) * 2)) : QString();
    }
    if (type == "comp") { reader.skip(8); return {}; }
    if (type == "tdta" || type == "alis" || type == "Pth ") {
        const quint32 length = reader.u32();
        reader.skip(length);
        return {};
    }
    if (type == "type" || type == "GlbC" || type == "Clss") {
        const quint32 chars = reader.u32();
        reader.skip(qsizetype(chars) * 2);
        return psd_descriptor_id(reader);
    }
    reader.seek(reader.end());
    return {};
}

static QJsonObject psd_descriptor(BeReader &reader, int depth)
{
    QJsonObject object;
    if (depth > 24 || !reader.ok(4)) return object;
    const quint32 name_chars = reader.u32();
    if (name_chars > 1000000 || !reader.ok(qsizetype(name_chars) * 2)) return object;
    reader.skip(qsizetype(name_chars) * 2);
    psd_descriptor_id(reader); /* class ID */
    const quint32 count = reader.u32();
    if (count > 100000) return object;
    for (quint32 i = 0; i < count && reader.ok(8); ++i) {
        const QString key = psd_descriptor_id(reader);
        const QByteArray type = reader.bytes(4);
        if (key.isEmpty() || type.size() != 4) break;
        const QJsonValue value = psd_descriptor_value(reader, type, depth + 1);
        if (!value.isUndefined() && !value.isNull()) object.insert(key, value);
    }
    return object;
}

static QJsonValue psd_value(const QJsonObject &object,
                            std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        const auto it = object.constFind(QString::fromLatin1(key));
        if (it != object.constEnd()) return *it;
    }
    return {};
}

static double psd_number(const QJsonObject &object,
                         std::initializer_list<const char *> keys,
                         double fallback)
{
    const QJsonValue value = psd_value(object, keys);
    return value.isDouble() ? value.toDouble() : fallback;
}

static bool psd_bool(const QJsonObject &object,
                     std::initializer_list<const char *> keys, bool fallback)
{
    const QJsonValue value = psd_value(object, keys);
    return value.isBool() ? value.toBool() : fallback;
}

static QColor psd_descriptor_color(const QJsonObject &object,
                                   const QColor &fallback)
{
    const QJsonValue nested = psd_value(object, {"Clr ", "color"});
    const QJsonObject color = nested.isObject() ? nested.toObject() : object;
    const int red = qBound(0, qRound(psd_number(color, {"Rd  ", "red"}, fallback.red())), 255);
    const int green = qBound(0, qRound(psd_number(color, {"Grn ", "green"}, fallback.green())), 255);
    const int blue = qBound(0, qRound(psd_number(color, {"Bl  ", "blue"}, fallback.blue())), 255);
    return QColor(red, green, blue);
}

static void sync_imported_effect(LayerEffect &effect)
{
    effect.enabled_prop.static_value = effect.enabled ? 1.0 : 0.0;
    effect.opacity_prop.static_value = effect.effect_opacity;
    effect.size_prop.static_value = effect.effect_size;
    effect.distance_prop.static_value = effect.effect_distance;
    effect.angle_prop.static_value = effect.effect_angle;
    effect.spread_prop.static_value = effect.effect_spread;
    effect.stroke_width_prop.static_value = effect.effect_stroke_width;
    effect.stroke_opacity_prop.static_value = effect.effect_stroke_opacity;
    effect.color_a.static_value = (effect.effect_color >> 24) & 0xFF;
    effect.color_r.static_value = (effect.effect_color >> 16) & 0xFF;
    effect.color_g.static_value = (effect.effect_color >> 8) & 0xFF;
    effect.color_b.static_value = effect.effect_color & 0xFF;
    effect.stroke_color_a.static_value = (effect.effect_stroke_color >> 24) & 0xFF;
    effect.stroke_color_r.static_value = (effect.effect_stroke_color >> 16) & 0xFF;
    effect.stroke_color_g.static_value = (effect.effect_stroke_color >> 8) & 0xFF;
    effect.stroke_color_b.static_value = effect.effect_stroke_color & 0xFF;
}

static LayerEffect psd_layer_effect(LayerEffectType type,
                                    const QJsonObject &descriptor)
{
    LayerEffect effect = fxm::effects::make_default_layer_effect(type);
    effect.enabled = psd_bool(descriptor, {"enab", "enabled"}, true);
    effect.effect_opacity = float(qBound(0.0,
        psd_number(descriptor, {"Opct", "opacity"}, effect.effect_opacity * 100.0) / 100.0,
        1.0));
    effect.effect_size = float(qMax(0.0,
        psd_number(descriptor, {"blur", "Sz  ", "size"}, effect.effect_size)));
    effect.effect_distance = float(qMax(0.0,
        psd_number(descriptor, {"Dstn", "distance"}, effect.effect_distance)));
    effect.effect_angle = float(psd_number(
        descriptor, {"lagl", "localLightingAngle", "Angl", "angle"},
        effect.effect_angle));
    effect.effect_spread = float(qBound(0.0,
        psd_number(descriptor, {"Ckmt", "chokeMatte", "spread"},
                   effect.effect_spread * 100.0) / 100.0, 1.0));
    effect.effect_color = argb(psd_descriptor_color(
        descriptor, QColor::fromRgba(effect.effect_color)));
    const QJsonValue mode = psd_value(descriptor, {"Md  ", "mode"});
    if (mode.isString()) effect.blend_mode = imported_blend(mode.toString().toLatin1());
    if (type == LayerEffectType::Outline) {
        effect.effect_owned_style_loaded = true;
        effect.effect_fill_type = 1;
        effect.effect_stroke_width = effect.effect_size;
        effect.effect_stroke_color = effect.effect_color;
        effect.effect_stroke_opacity = effect.effect_opacity;
        const QJsonValue position = psd_value(descriptor, {"Styl", "style"});
        effect.effect_on_front = !position.isString() || position.toString() != QStringLiteral("InsF");
    }
    sync_imported_effect(effect);
    return effect;
}

static void append_psd_effect(std::vector<LayerEffect> &effects,
                              const QJsonValue &value, LayerEffectType type)
{
    if (value.isObject()) {
        LayerEffect effect = psd_layer_effect(type, value.toObject());
        if (effect.enabled) effects.push_back(std::move(effect));
    }
    else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray())
            if (item.isObject()) {
                LayerEffect effect = psd_layer_effect(type, item.toObject());
                if (effect.enabled) effects.push_back(std::move(effect));
            }
    }
}

static std::vector<LayerEffect> psd_layer_effects(const QByteArray &block)
{
    std::vector<LayerEffect> effects;
    if (block.size() < 12) return effects;
    BeReader reader(block);
    reader.u32(); /* object-effects version */
    if (reader.u32() != 16) return effects;
    const QJsonObject descriptor = psd_descriptor(reader);
    if (!psd_bool(descriptor, {"masterFXSwitch", "masterFXSwitch"}, true))
        return effects;
    append_psd_effect(effects, psd_value(descriptor, {"DrSh", "dropShadow", "dropShadowMulti"}), LayerEffectType::DropShadow);
    append_psd_effect(effects, psd_value(descriptor, {"IrSh", "innerShadow", "innerShadowMulti"}), LayerEffectType::InnerShadow);
    append_psd_effect(effects, psd_value(descriptor, {"OrGl", "outerGlow"}), LayerEffectType::Glow);
    append_psd_effect(effects, psd_value(descriptor, {"IrGl", "innerGlow"}), LayerEffectType::InnerGlow);
    append_psd_effect(effects, psd_value(descriptor, {"SoFi", "solidFill", "solidFillMulti"}), LayerEffectType::ColorOverlay);
    append_psd_effect(effects, psd_value(descriptor, {"FrFX", "frameFX", "frameFXMulti"}), LayerEffectType::Outline);
    append_psd_effect(effects, psd_value(descriptor, {"ebbl", "bevelEmboss"}), LayerEffectType::Emboss);
    return effects;
}

static QColor psd_legacy_color(BeReader &reader)
{
    const quint16 color_space = reader.u16();
    const quint16 c0 = reader.u16(), c1 = reader.u16(), c2 = reader.u16();
    reader.u16();
    if (color_space == 0)
        return QColor(c0 >> 8, c1 >> 8, c2 >> 8);
    if (color_space == 8) {
        const int gray = c0 >> 8;
        return QColor(gray, gray, gray);
    }
    return Qt::black;
}

static std::vector<LayerEffect> psd_legacy_layer_effects(const QByteArray &block)
{
    std::vector<LayerEffect> effects;
    BeReader reader(block);
    if (reader.u16() != 0) return effects;
    const quint16 count = reader.u16();
    for (quint16 i = 0; i < count && reader.ok(12); ++i) {
        if (reader.bytes(4) != "8BIM") break;
        const QByteArray key = reader.bytes(4);
        const quint32 length = reader.u32();
        const QByteArray payload = reader.bytes(length);
        BeReader effect_reader(payload);
        if ((key == "dsdw" || key == "isdw") && effect_reader.ok(41)) {
            effect_reader.u32();
            LayerEffect effect = fxm::effects::make_default_layer_effect(
                key == "isdw" ? LayerEffectType::InnerShadow : LayerEffectType::DropShadow);
            effect.effect_size = float(effect_reader.u32());
            effect_reader.u32();
            effect.effect_angle = float(effect_reader.s32());
            effect.effect_distance = float(effect_reader.u32());
            effect.effect_color = argb(psd_legacy_color(effect_reader));
            effect_reader.skip(4);
            effect.blend_mode = imported_blend(effect_reader.bytes(4));
            effect.enabled = effect_reader.u8() != 0;
            effect_reader.u8();
            effect.effect_opacity = effect_reader.u8() / 255.0f;
            if (effect.enabled) {
                sync_imported_effect(effect);
                effects.push_back(std::move(effect));
            }
        } else if ((key == "oglw" || key == "iglw") && effect_reader.ok(32)) {
            effect_reader.u32();
            LayerEffect effect = fxm::effects::make_default_layer_effect(
                key == "iglw" ? LayerEffectType::InnerGlow : LayerEffectType::Glow);
            effect.effect_size = float(effect_reader.u32());
            effect_reader.u32();
            effect.effect_color = argb(psd_legacy_color(effect_reader));
            effect_reader.skip(4);
            effect.blend_mode = imported_blend(effect_reader.bytes(4));
            effect.enabled = effect_reader.u8() != 0;
            effect.effect_opacity = effect_reader.u8() / 255.0f;
            if (effect.enabled) {
                sync_imported_effect(effect);
                effects.push_back(std::move(effect));
            }
        }
    }
    return effects;
}

static bool image_is_solid(const QImage &image, QColor &color)
{
    bool found=false; QRgb candidate=0;
    for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
        const QRgb p=image.pixel(x,y); if (qAlpha(p)==0) continue;
        if (!found) { candidate=p; found=true; }
        else if (qRed(p)!=qRed(candidate)||qGreen(p)!=qGreen(candidate)||qBlue(p)!=qBlue(candidate)||qAlpha(p)!=qAlpha(candidate)) return false;
    }
    if (found) color=QColor::fromRgba(candidate); return found;
}

static QByteArray decode_packbits(BeReader &reader, int expected)
{
    QByteArray out; out.reserve(expected);
    while (out.size()<expected && reader.ok(1)) {
        const qint8 control=qint8(reader.u8());
        if (control>=0) { const int count=int(control)+1; out += reader.bytes(qMin(count, expected-out.size())); }
        else if (control!=-128) { const int count=1-int(control); const char value=char(reader.u8()); out.append(qMin(count,expected-out.size()),value); }
    }
    if (out.size()<expected) out.append(expected-out.size(),char(0)); return out.left(expected);
}

static QByteArray decode_zlib(const QByteArray &compressed, int expected)
{
    QByteArray framed;
    framed.reserve(compressed.size()+4);
    framed.append(char((quint32(expected)>>24)&0xFF));framed.append(char((quint32(expected)>>16)&0xFF));
    framed.append(char((quint32(expected)>>8)&0xFF));framed.append(char(quint32(expected)&0xFF));framed+=compressed;
    return qUncompress(framed);
}

static QByteArray decode_psd_channel(const QByteArray &block, int width, int height)
{
    BeReader r(block); const quint16 compression=r.u16(); const int expected=width*height;
    if (compression==0) { QByteArray raw=r.bytes(expected); if(raw.size()<expected)raw.append(expected-raw.size(),char(0)); return raw; }
    if (compression==2||compression==3) {
        QByteArray out=decode_zlib(r.bytes(r.end()-r.pos()),expected);if(out.size()!=expected)return{};
        if(compression==3)for(int y=0;y<height;++y)for(int x=1;x<width;++x){const int p=y*width+x;out[p]=char(quint8(out[p])+quint8(out[p-1]));}
        return out;
    }
    if (compression!=1) return {};
    QVector<int> lengths; lengths.reserve(height); for(int y=0;y<height;++y) lengths.push_back(r.u16());
    QByteArray out; out.reserve(expected);
    for (int y=0;y<height;++y) { const QByteArray row=r.bytes(lengths[y]); BeReader rr(row); out += decode_packbits(rr,width); }
    return out;
}

static QString psd_text_value(const QByteArray &block)
{
    for (int i=0;i+12<block.size();++i) {
        if (block.mid(i,4)!="Txt ") continue;
        int type=block.indexOf("TEXT",i+4); if(type<0||type>i+32||type+8>block.size()) continue;
        BeReader r(block,type+4); const quint32 count=r.u32();
        if (count>0 && count<100000 && r.ok(qsizetype(count)*2)) return utf16_be(r.bytes(qsizetype(count)*2)).replace('\r','\n');
    }
    return {};
}

static QByteArray psd_text_engine_data(const QByteArray &block)
{
    const int key = block.indexOf("EngineData");
    if (key < 0) return {};
    const int type = block.indexOf("tdta", key);
    if (type < 0 || type - key > 128 || type + 8 > block.size()) return {};
    BeReader reader(block, type + 4);
    const quint32 length = reader.u32();
    return length <= quint32(reader.end() - reader.pos())
        ? reader.bytes(length) : QByteArray();
}

static QVector<double> psd_engine_numbers(const QString &source)
{
    QVector<double> values;
    auto cursor = QRegularExpression(
        QStringLiteral("[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"))
        .globalMatch(source);
    while (cursor.hasNext()) values.push_back(cursor.next().captured().toDouble());
    return values;
}

static uint32_t psd_engine_color(const QString &style, const QString &key,
                                 uint32_t fallback)
{
    const QRegularExpression expression(
        QStringLiteral("/%1\\s*<<[\\s\\S]*?/Values\\s*\\[([^\\]]+)\\]")
            .arg(QRegularExpression::escape(key)));
    const auto match = expression.match(style);
    if (!match.hasMatch()) return fallback;
    const QVector<double> values = psd_engine_numbers(match.captured(1));
    if (values.size() < 4) return fallback;
    const int alpha = qBound(0, qRound(values[0] * 255.0), 255);
    const int red = qBound(0, qRound(values[1] * 255.0), 255);
    const int green = qBound(0, qRound(values[2] * 255.0), 255);
    const int blue = qBound(0, qRound(values[3] * 255.0), 255);
    return (quint32(alpha) << 24) | (quint32(red) << 16) |
           (quint32(green) << 8) | quint32(blue);
}

static float psd_engine_scale(double value, float fallback)
{
    if (!std::isfinite(value) || value <= 0.0)
        return fallback;
    /* EngineData normally stores 1.0 as 100%. Some producers serialize 100. */
    const double normalized = value > 10.0 ? value / 100.0 : value;
    return float(qBound(0.01, normalized, 100.0));
}

static RichTextCharFormat psd_engine_style(
    const QString &style, const QStringList &fonts,
    const RichTextCharFormat &fallback)
{
    RichTextCharFormat format = fallback;
    auto number = [&](const QString &key, double current) {
        const auto match = QRegularExpression(
            QStringLiteral("/%1\\s+([-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?)")
                .arg(QRegularExpression::escape(key))).match(style);
        return match.hasMatch() ? match.captured(1).toDouble() : current;
    };
    auto boolean = [&](const QString &key, bool current) {
        const auto match = QRegularExpression(
            QStringLiteral("/%1\\s+(true|false|[-+]?(?:\\d*\\.\\d+|\\d+\\.?))")
                .arg(QRegularExpression::escape(key)),
            QRegularExpression::CaseInsensitiveOption).match(style);
        if (!match.hasMatch()) return current;
        const QString value = match.captured(1);
        if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0)
            return true;
        if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0)
            return false;
        return std::abs(value.toDouble()) > 1e-9;
    };
    const int font_index = qRound(number(QStringLiteral("Font"), -1.0));
    if (font_index >= 0 && font_index < fonts.size())
        format.font_family = fonts[font_index].toStdString();
    format.font_size = qMax(1, qRound(number(QStringLiteral("FontSize"), format.font_size)));
    format.bold = boolean(QStringLiteral("FauxBold"), format.bold);
    format.italic = boolean(QStringLiteral("FauxItalic"), format.italic);
    format.underline = boolean(QStringLiteral("Underline"), format.underline);
    format.strikethrough = boolean(QStringLiteral("Strikethrough"), format.strikethrough);
    format.kerning = boolean(QStringLiteral("AutoKerning"), format.kerning);
    format.tracking = float(number(QStringLiteral("Tracking"), format.tracking));
    format.baseline_shift = float(number(QStringLiteral("BaselineShift"), format.baseline_shift));
    format.scale_x = psd_engine_scale(
        number(QStringLiteral("HorizontalScale"), format.scale_x),
        format.scale_x);
    format.scale_y = psd_engine_scale(
        number(QStringLiteral("VerticalScale"), format.scale_y),
        format.scale_y);
    const int font_caps = qRound(number(QStringLiteral("FontCaps"), -1.0));
    /* Photoshop: 1=small caps, 2=all caps. FXM stores those as 2 and 1. */
    if (font_caps >= 0)
        format.text_style = font_caps == 1 ? 2 : (font_caps == 2 ? 1 : 0);
    format.fill.color = psd_engine_color(style, QStringLiteral("FillColor"),
                                         format.fill.color);
    if (!boolean(QStringLiteral("FillFlag"), true))
        format.fill.color &= 0x00FFFFFFu;
    format.stroke.fill.color = psd_engine_color(
        style, QStringLiteral("StrokeColor"), format.stroke.fill.color);
    format.stroke.width = float(number(QStringLiteral("LineWidth"),
        number(QStringLiteral("StrokeWidth"), format.stroke.width)));
    format.stroke.enabled = boolean(QStringLiteral("StrokeFlag"),
                                    format.stroke.enabled) &&
        format.stroke.width > 0.0f &&
        ((format.stroke.fill.color >> 24) & 0xFF) != 0;
    format.font_style = format.italic
        ? (format.bold ? "Bold Italic" : "Italic")
        : (format.bold ? "Bold" : "Regular");
    return format;
}

static RichTextParagraphFormat psd_engine_paragraph_style(
    const QString &style, const RichTextParagraphFormat &fallback)
{
    RichTextParagraphFormat format = fallback;
    auto number = [&](const QString &key, double current) {
        const auto match = QRegularExpression(
            QStringLiteral("/%1\\s+([-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?)")
                .arg(QRegularExpression::escape(key))).match(style);
        return match.hasMatch() ? match.captured(1).toDouble() : current;
    };
    const int justification = qRound(number(QStringLiteral("Justification"), 0.0));
    switch (justification) {
    case 1: format.align_h = 2; break; /* right */
    case 2: format.align_h = 1; break; /* center */
    case 3: format.align_h = 3; break;
    case 4: format.align_h = 5; break; /* justify, last line right */
    case 5: format.align_h = 4; break; /* justify, last line centered */
    case 6: format.align_h = 6; break;
    default: format.align_h = 0; break;
    }
    format.indent_left = float(number(QStringLiteral("StartIndent"), format.indent_left));
    format.indent_right = float(number(QStringLiteral("EndIndent"), format.indent_right));
    format.indent_first_line = float(number(QStringLiteral("FirstLineIndent"), format.indent_first_line));
    format.line_spacing = float(number(QStringLiteral("Leading"), format.line_spacing));
    format.space_before = float(number(QStringLiteral("SpaceBefore"), format.space_before));
    format.space_after = float(number(QStringLiteral("SpaceAfter"), format.space_after));
    return format;
}

static QVector<QString> psd_engine_dictionaries(const QString &engine,
                                                const QString &key)
{
    QVector<QString> dictionaries;
    int search_from = 0;
    const QString marker = QStringLiteral("/") + key;
    while (search_from < engine.size()) {
        const int key_pos = engine.indexOf(marker, search_from);
        if (key_pos < 0) break;
        const int begin = engine.indexOf(QStringLiteral("<<"),
                                         key_pos + marker.size());
        if (begin < 0) break;
        int depth = 1;
        int cursor = begin + 2;
        while (cursor + 1 < engine.size() && depth > 0) {
            if (engine.mid(cursor, 2) == QStringLiteral("<<")) {
                ++depth;
                cursor += 2;
            } else if (engine.mid(cursor, 2) == QStringLiteral(">>")) {
                --depth;
                cursor += 2;
            } else {
                ++cursor;
            }
        }
        if (depth != 0) break;
        dictionaries.push_back(engine.mid(begin + 2, cursor - begin - 4));
        search_from = cursor;
    }
    return dictionaries;
}

static QVector<double> psd_engine_run_lengths(const QString &section)
{
    const auto match = QRegularExpression(
        QStringLiteral("/RunLengthArray\\s*\\[([^\\]]*)\\]"))
        .match(section);
    return match.hasMatch() ? psd_engine_numbers(match.captured(1))
                            : QVector<double>();
}

static QPair<size_t, size_t> psd_utf8_range(const QString &text,
                                            int utf16_start, int utf16_length)
{
    const int text_size = int(std::min<qsizetype>(text.size(), 1000000));
    const int safe_start = qBound(0, utf16_start, text_size);
    const int safe_end = qBound(safe_start, utf16_start + qMax(0, utf16_length),
                                text_size);
    const size_t byte_start = size_t(text.left(safe_start).toUtf8().size());
    const size_t byte_length = size_t(text.mid(
        safe_start, safe_end - safe_start).toUtf8().size());
    return {byte_start, byte_length};
}

static RichTextDocument psd_rich_text(const QByteArray &block,
                                      const QString &plain_text)
{
    RichTextDocument document;
    document.plain_text = plain_text.toUtf8().toStdString();
    document.default_format.font_family = "Sans Serif";
    document.default_format.font_size = 48;
    const QByteArray engine_bytes = psd_text_engine_data(block);
    if (engine_bytes.isEmpty()) {
        document.normalize();
        return document;
    }
    const QString engine = QString::fromLatin1(engine_bytes);
    QStringList fonts;
    auto font_matches = QRegularExpression(
        QStringLiteral("/Name\\s*\\(([^)]*)\\)"))
        .globalMatch(engine);
    while (font_matches.hasNext()) {
        QString name = font_matches.next().captured(1);
        name.replace(QStringLiteral("\\("), QStringLiteral("("));
        name.replace(QStringLiteral("\\)"), QStringLiteral(")"));
        fonts.push_back(name);
    }

    const QVector<QString> style_sections = psd_engine_dictionaries(
        engine, QStringLiteral("StyleRun"));
    const QString style_section = style_sections.isEmpty()
        ? engine : style_sections.front();
    const QVector<QString> styles = psd_engine_dictionaries(
        style_section, QStringLiteral("StyleSheetData"));
    const QVector<double> run_numbers = psd_engine_run_lengths(style_section);
    const int style_count = int(styles.size());
    const int run_number_count = int(run_numbers.size());
    const int style_offset = style_count > run_number_count
        ? style_count - run_number_count : 0;
    if (!styles.isEmpty())
        document.default_format = psd_engine_style(
            styles.front(), fonts, document.default_format);
    int utf16_start = 0;
    const int available_styles = qMax(0, style_count - style_offset);
    const int run_count = qMin(run_number_count, available_styles);
    for (int index = 0; index < run_count; ++index) {
        const int utf16_length = qMax(0, qRound(run_numbers[index]));
        const auto byte_range = psd_utf8_range(
            plain_text, utf16_start, utf16_length);
        if (byte_range.second > 0) {
            RichTextRange range;
            range.start = byte_range.first;
            range.length = byte_range.second;
            range.format = psd_engine_style(
                styles[style_offset + index], fonts, document.default_format);
            range.mask = RichTextCharAll;
            document.ranges.push_back(range);
        }
        utf16_start += utf16_length;
    }

    const QVector<QString> paragraph_sections = psd_engine_dictionaries(
        engine, QStringLiteral("ParagraphRun"));
    const QString paragraph_section = paragraph_sections.isEmpty()
        ? engine : paragraph_sections.front();
    const QVector<QString> paragraph_styles = psd_engine_dictionaries(
        paragraph_section, QStringLiteral("Properties"));
    const QVector<double> paragraph_lengths = psd_engine_run_lengths(
        paragraph_section);
    const int paragraph_style_count = int(paragraph_styles.size());
    const int paragraph_length_count = int(paragraph_lengths.size());
    const int paragraph_offset = paragraph_style_count > paragraph_length_count
        ? paragraph_style_count - paragraph_length_count : 0;
    if (!paragraph_styles.isEmpty())
        document.default_paragraph_format = psd_engine_paragraph_style(
            paragraph_styles.front(), document.default_paragraph_format);
    document.normalize();
    utf16_start = 0;
    const int available_paragraph_styles = qMax(
        0, paragraph_style_count - paragraph_offset);
    const int paragraph_count = qMin(
        paragraph_length_count, available_paragraph_styles);
    for (int index = 0; index < paragraph_count; ++index) {
        const int utf16_length = qMax(0, qRound(paragraph_lengths[index]));
        const auto byte_range = psd_utf8_range(
            plain_text, utf16_start, utf16_length);
        if (byte_range.second > 0) {
            const RichTextParagraphFormat format = psd_engine_paragraph_style(
                paragraph_styles[paragraph_offset + index],
                document.default_paragraph_format);
            rich_text_document_apply_paragraph_format(
                document, byte_range.first,
                byte_range.first + byte_range.second,
                format, RichTextParagraphAll);
        }
        utf16_start += utf16_length;
    }
    document.normalize();
    return document;
}

static QPainterPath psd_vector_mask(const QByteArray &block, int document_width, int document_height)
{
    QPainterPath path;
    if (block.size() < 8) return path;
    BeReader r(block); r.u32(); r.u32();
    struct Knot { QPointF before, anchor, after; };
    QVector<Knot> knots; int wanted = 0; bool closed = true;
    auto flush = [&]() {
        if (knots.isEmpty()) return;
        path.moveTo(knots.front().anchor);
        for (int i=1;i<knots.size();++i) path.cubicTo(knots[i-1].after, knots[i].before, knots[i].anchor);
        if (closed && knots.size()>1) { path.cubicTo(knots.back().after,knots.front().before,knots.front().anchor); path.closeSubpath(); }
        knots.clear(); wanted=0;
    };
    auto fixed = [&](qint32 value, int extent) { return double(value) / 16777216.0 * extent; };
    while (r.ok(26)) {
        const quint16 selector=r.u16();
        if (selector==0||selector==3) { flush(); wanted=int(r.u16()); closed=selector==0; r.skip(22); }
        else if (selector>=1&&selector<=5) {
            Knot k;
            const qint32 before_y=r.s32(),before_x=r.s32(),anchor_y=r.s32(),anchor_x=r.s32(),after_y=r.s32(),after_x=r.s32();
            k.before=QPointF(fixed(before_x,document_width),fixed(before_y,document_height));
            k.anchor=QPointF(fixed(anchor_x,document_width),fixed(anchor_y,document_height));
            k.after=QPointF(fixed(after_x,document_width),fixed(after_y,document_height));knots.push_back(k);
            if(wanted>0&&knots.size()>=wanted)flush();
        } else r.skip(24);
    }
    flush(); return path;
}

static bool image_has_flat_rgb(const QImage &image, QColor &color)
{
    bool found=false;int red=0,green=0,blue=0,max_alpha=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const QRgb p=image.pixel(x,y);if(qAlpha(p)==0)continue;if(!found){red=qRed(p);green=qGreen(p);blue=qBlue(p);found=true;}else if(qRed(p)!=red||qGreen(p)!=green||qBlue(p)!=blue)return false;max_alpha=qMax(max_alpha,qAlpha(p));}
    if(found)color=QColor(red,green,blue,max_alpha);return found;
}

struct PsdRecord {
    ImportedLayer layer; QVector<QPair<qint16,quint32>> channels; QByteArray text_block; bool has_solid=false; int section=0;
};

static ImportedDocument read_psd(const QString &path)
{
    ImportedDocument doc; QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){doc.error=file.errorString();return doc;} const QByteArray data=file.readAll(); BeReader r(data);
    if(r.bytes(4)!="8BPS"||r.u16()!=1){doc.error=fxm_tr("OBSTitles.ImportInvalidPsd");return doc;}
    r.skip(6); const int channel_count=r.u16(); const int height=int(r.u32()), width=int(r.u32()); const int depth=r.u16(), color_mode=r.u16();
    if(width<=0||height<=0||width>30000||height>30000||quint64(width)*quint64(height)>100000000ULL){doc.error=fxm_tr("OBSTitles.ImportDocumentTooLarge");return doc;}
    if(depth!=8||(color_mode!=1&&color_mode!=3&&color_mode!=4)){doc.error=fxm_tr("OBSTitles.ImportUnsupportedPsd");return doc;}
    doc.size=QSize(width,height);
    for(int section=0;section<3;++section){const quint32 length=r.u32(); if(!r.ok(length)){doc.error=fxm_tr("OBSTitles.ImportCorruptDocument");return doc;} if(section<2)r.skip(length); else {
        const qsizetype mask_end=r.pos()+length; if(length<4){r.seek(mask_end);break;} const quint32 layer_info_length=r.u32(); const qsizetype layer_end=qMin(mask_end,r.pos()+layer_info_length);
        if(layer_info_length>0&&r.pos()+2<=layer_end){int count=std::abs(int(r.s16())); QVector<PsdRecord> records; records.reserve(count);
            for(int n=0;n<count&&r.pos()<layer_end;++n){PsdRecord rec; const int top=r.s32(),left=r.s32(),bottom=r.s32(),right=r.s32(); rec.layer.rect=QRect(left,top,qMax(0,right-left),qMax(0,bottom-top));
                const int cc=r.u16(); for(int c=0;c<cc;++c)rec.channels.push_back({r.s16(),r.u32()});
                r.bytes(4); rec.layer.blend=imported_blend(r.bytes(4)); rec.layer.opacity=r.u8()/255.0; r.u8(); const quint8 flags=r.u8(); rec.layer.visible=(flags&2)==0; r.u8();
                const quint32 extra_len=r.u32(); const qsizetype extra_start=r.pos(),extra_end=qMin(layer_end,extra_start+extra_len);
                quint32 len=r.u32();r.skip(len);len=r.u32();r.skip(len);const qsizetype pascal_start=r.pos();const int name_len=r.u8();rec.layer.name=QString::fromLatin1(r.bytes(name_len));r.seek(qMin(extra_end,pascal_start+((name_len+1+3)&~3)));
                while(r.pos()+12<=extra_end){const QByteArray sig=r.bytes(4),key=r.bytes(4);Q_UNUSED(sig);const quint32 block_len=r.u32();const QByteArray block=r.bytes(qMin<qsizetype>(block_len,extra_end-r.pos()));if(block_len&1)r.skip(1);
                    if(key=="luni"&&block.size()>=4){BeReader br(block);const quint32 chars=br.u32();if(chars<100000&&br.ok(qsizetype(chars)*2))rec.layer.name=utf16_be(br.bytes(qsizetype(chars)*2));}
                    else if(key=="TySh"){rec.text_block=block;rec.layer.kind=Kind::Text;}
                    else if(key=="SoCo"){rec.has_solid=true;rec.layer.kind=Kind::Solid;}
                    else if(key=="vmsk"||key=="vsms"){rec.layer.vector_path=psd_vector_mask(block,width,height);if(!rec.layer.vector_path.isEmpty())rec.layer.kind=Kind::Shape;}
                    else if(key=="lfx2"||key=="lmfx"){const auto parsed=psd_layer_effects(block);rec.layer.effects.insert(rec.layer.effects.end(),parsed.begin(),parsed.end());}
                    else if(key=="lrFX"){const auto parsed=psd_legacy_layer_effects(block);rec.layer.effects.insert(rec.layer.effects.end(),parsed.begin(),parsed.end());}
                    else if(key=="brit"||key=="levl"||key=="curv"||key=="expA"||key=="vibA"||key=="hue2"||key=="blnc"||key=="blwh"||key=="phfl"||key=="mixr"||key=="clrL"||key=="nvrt"||key=="post"||key=="thrs"||key=="grdm"||key=="selc")rec.layer.kind=Kind::Adjustment;
                    else if(key=="lsct"&&block.size()>=4){BeReader br(block);rec.section=int(br.u32());rec.layer.kind=rec.section==3?Kind::GroupEnd:Kind::Group;}
                }
                r.seek(extra_end); if(rec.layer.name.isEmpty())rec.layer.name=QStringLiteral("Layer %1").arg(n+1); records.push_back(rec);
            }
            for(PsdRecord &rec:records){QMap<int,QByteArray> planes;const int w=rec.layer.rect.width(),h=rec.layer.rect.height();for(const auto &channel:rec.channels){const QByteArray block=r.bytes(channel.second);const QByteArray decoded=decode_psd_channel(block,w,h);if(!decoded.isEmpty())planes.insert(channel.first,decoded);}
                if(w>0&&h>0&&!planes.isEmpty()){rec.layer.pixels=QImage(w,h,QImage::Format_ARGB32);for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int p=y*w+x;const int a=planes.contains(-1)?quint8(planes[-1][p]):255;int red=0,green=0,blue=0;if(color_mode==1){red=green=blue=planes.contains(0)?quint8(planes[0][p]):0;}else if(color_mode==4){const int c=planes.contains(0)?quint8(planes[0][p]):255,m=planes.contains(1)?quint8(planes[1][p]):255,yellow=planes.contains(2)?quint8(planes[2][p]):255,k=planes.contains(3)?quint8(planes[3][p]):255;red=c*k/255;green=m*k/255;blue=yellow*k/255;}else{red=planes.contains(0)?quint8(planes[0][p]):0;green=planes.contains(1)?quint8(planes[1][p]):0;blue=planes.contains(2)?quint8(planes[2][p]):0;}rec.layer.pixels.setPixel(x,y,qRgba(red,green,blue,a));}}
                if(rec.layer.kind==Kind::Text){rec.layer.text=psd_text_value(rec.text_block);if(rec.layer.text.isEmpty())rec.layer.kind=Kind::Pixel;else{rec.layer.rich_text=psd_rich_text(rec.text_block,rec.layer.text);rec.layer.font_family=QString::fromStdString(rec.layer.rich_text.default_format.font_family);rec.layer.font_size=rec.layer.rich_text.default_format.font_size;}}
                QColor solid;if((rec.has_solid||rec.layer.kind==Kind::Pixel)&&image_is_solid(rec.layer.pixels,solid)){rec.layer.kind=Kind::Solid;rec.layer.solid_color=solid;}
                else if(rec.layer.kind==Kind::Shape&&image_has_flat_rgb(rec.layer.pixels,solid))rec.layer.solid_color=solid;
            }
            QVector<int> parents; for(int i=0;i<records.size();++i){auto &rec=records[i];if(rec.layer.kind==Kind::GroupEnd){if(!parents.isEmpty())parents.removeLast();continue;}rec.layer.parent=parents.isEmpty()?-1:parents.back();doc.layers.push_back(rec.layer);const int imported_index=doc.layers.size()-1;if(rec.layer.kind==Kind::Group)parents.push_back(imported_index);}
        } r.seek(mask_end);
    }}
    if(r.ok(2)) {
        const quint16 compression=r.u16();const int plane_size=width*height;QVector<QByteArray> planes;
        if(compression==0){for(int c=0;c<channel_count;++c)planes.push_back(r.bytes(plane_size));}
        else if(compression==1){QVector<int> row_lengths;for(int c=0;c<channel_count;++c)for(int y=0;y<height;++y)row_lengths.push_back(r.u16());int row=0;for(int c=0;c<channel_count;++c){QByteArray plane;plane.reserve(plane_size);for(int y=0;y<height;++y){const QByteArray packed=r.bytes(row_lengths[row++]);BeReader rr(packed);plane+=decode_packbits(rr,width);}planes.push_back(plane);}}
        else if(compression==2||compression==3){const QByteArray raw=decode_zlib(r.bytes(r.end()-r.pos()),plane_size*channel_count);if(raw.size()==plane_size*channel_count)for(int c=0;c<channel_count;++c){QByteArray plane=raw.mid(c*plane_size,plane_size);if(compression==3)for(int y=0;y<height;++y)for(int x=1;x<width;++x){const int p=y*width+x;plane[p]=char(quint8(plane[p])+quint8(plane[p-1]));}planes.push_back(plane);}}
        const int required=color_mode==4?4:(color_mode==3?3:1);if(planes.size()>=required){doc.merged_preview=QImage(width,height,QImage::Format_ARGB32);for(int y=0;y<height;++y)for(int x=0;x<width;++x){const int p=y*width+x;int red=quint8(planes[0][p]),green=red,blue=red,alpha=255;if(color_mode==3){green=quint8(planes[1][p]);blue=quint8(planes[2][p]);if(planes.size()>3)alpha=quint8(planes[3][p]);}else if(color_mode==4){const int c=quint8(planes[0][p]),m=quint8(planes[1][p]),yellow=quint8(planes[2][p]),k=quint8(planes[3][p]);red=c*k/255;green=m*k/255;blue=yellow*k/255;if(planes.size()>4)alpha=quint8(planes[4][p]);}else if(planes.size()>1)alpha=quint8(planes[1][p]);doc.merged_preview.setPixel(x,y,qRgba(red,green,blue,alpha));}}
    }
    if(doc.layers.isEmpty()&&!doc.merged_preview.isNull()){ImportedLayer flat;flat.name=QFileInfo(path).completeBaseName();flat.rect=QRect(QPoint(),doc.size);flat.pixels=doc.merged_preview;doc.layers.push_back(flat);}
    if(doc.layers.isEmpty())doc.error=fxm_tr("OBSTitles.ImportPsdNoLayers"); return doc;
}

static QByteArray decode_xcf_rle(BeReader &reader, int expected)
{
    QByteArray out; out.reserve(expected);
    while(out.size()<expected&&reader.ok(1)){
        const int control=reader.u8(); int count=0;
        if(control>=128){count=256-control;if(count==128)count=int(reader.u16());out+=reader.bytes(qMin(count,expected-out.size()));}
        else{count=control+1;if(count==128)count=int(reader.u16());const char value=char(reader.u8());out.append(qMin(count,expected-out.size()),value);}
    }
    if(out.size()<expected)out.append(expected-out.size(),char(0));return out.left(expected);
}

static QString xcf_string(BeReader &r)
{
    const quint32 length=r.u32(); if(length==0||length>quint32(r.end()-r.pos()))return{}; QByteArray value=r.bytes(length);if(value.endsWith('\0'))value.chop(1);return QString::fromUtf8(value);
}

static QString unescape_gimp_string(QString value)
{
    value.replace(QStringLiteral("\\n"),QStringLiteral("\n"));value.replace(QStringLiteral("\\r"),QStringLiteral("\r"));value.replace(QStringLiteral("\\\""),QStringLiteral("\""));value.replace(QStringLiteral("\\\\"),QStringLiteral("\\"));return value;
}

static QString parse_gimp_text_parasite(const QByteArray &data, QString *font, double *font_size)
{
    const QString source=QString::fromUtf8(data);
    auto quoted=[&](const QString &key){QRegularExpression re(QStringLiteral("(?:^|[\\s(])%1\\s+\"((?:\\\\.|[^\"])*)\"").arg(QRegularExpression::escape(key)));auto m=re.match(source);return m.hasMatch()?unescape_gimp_string(m.captured(1)):QString();};
    QString text=quoted(QStringLiteral("text"));if(text.isEmpty())text=quoted(QStringLiteral("markup"));
    if(font){QString found=quoted(QStringLiteral("font"));if(!found.isEmpty())*font=found;}
    if(font_size){QRegularExpression re(QStringLiteral("font-size\\s+([0-9.]+)"));auto m=re.match(source);if(m.hasMatch())*font_size=m.captured(1).toDouble();}
    return text;
}

static EffectBlendMode xcf_blend(int mode)
{
    /* Stable legacy GIMP mode ids; unknown/modern composite modes fall back
     * to Normal because FXM deliberately exposes a smaller blend vocabulary. */
    switch(mode){case 3:return EffectBlendMode::Multiply;case 4:return EffectBlendMode::Screen;case 5:return EffectBlendMode::Overlay;case 7:case 8:return EffectBlendMode::Additive;case 11:case 12:case 13:return EffectBlendMode::Color;default:return EffectBlendMode::Normal;}
}

static ImportedDocument read_xcf(const QString &path)
{
    ImportedDocument doc;QFile file(path);if(!file.open(QIODevice::ReadOnly)){doc.error=file.errorString();return doc;}const QByteArray data=file.readAll();BeReader r(data);
    const QByteArray header=r.bytes(14);if(!header.startsWith("gimp xcf ")){doc.error=fxm_tr("OBSTitles.ImportInvalidXcf");return doc;}
    int version=0;const QRegularExpressionMatch vm=QRegularExpression(QStringLiteral("v(\\d+)"),QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(header));if(vm.hasMatch())version=vm.captured(1).toInt();
    const bool wide_offsets=version>=11;auto offset=[&](BeReader &reader)->quint64{return wide_offsets?reader.u64():reader.u32();};
    const int width=int(r.u32()),height=int(r.u32()),base_type=int(r.u32());Q_UNUSED(base_type);if(version>=4)r.u32();
    if(width<=0||height<=0||width>30000||height>30000||quint64(width)*quint64(height)>100000000ULL){doc.error=fxm_tr("OBSTitles.ImportDocumentTooLarge");return doc;}doc.size=QSize(width,height);int compression=1;QVector<QRgb> palette;
    while(r.ok(8)){const quint32 type=r.u32(),length=r.u32();const QByteArray prop=r.bytes(length);if(type==0)break;if(type==17&&!prop.isEmpty())compression=quint8(prop[0]);else if(type==1&&prop.size()>=4){BeReader cm(prop);const int colors=int(cm.u32());for(int c=0;c<colors&&cm.ok(3);++c)palette.push_back(qRgb(cm.u8(),cm.u8(),cm.u8()));}}
    if(compression>2){doc.error=fxm_tr("OBSTitles.ImportUnsupportedXcfCompression");return doc;}
    QVector<quint64> layer_offsets;while(r.ok(wide_offsets?8:4)){const quint64 value=offset(r);if(!value)break;layer_offsets.push_back(value);}while(r.ok(wide_offsets?8:4)){if(!offset(r))break;}
    struct XcfPending{ImportedLayer layer;quint64 hierarchy=0;bool group=false;QVector<int>path;};QVector<XcfPending> pending;
    for(int index=0;index<layer_offsets.size();++index){r.seek(layer_offsets[index]);if(!r.ok(12))continue;XcfPending item;const int lw=int(r.u32()),lh=int(r.u32()),layer_type=int(r.u32());item.layer.rect=QRect(0,0,lw,lh);item.layer.name=xcf_string(r);if(item.layer.name.isEmpty())item.layer.name=QStringLiteral("Layer %1").arg(index+1);
        while(r.ok(8)){const quint32 type=r.u32(),length=r.u32();const QByteArray property=r.bytes(length);if(type==0)break;BeReader pr(property);
            if(type==6&&property.size()>=4)item.layer.opacity=qBound(0.0,double(pr.u32())/255.0,1.0);
            else if(type==7&&property.size()>=4)item.layer.blend=xcf_blend(int(pr.u32()));
            else if(type==8&&property.size()>=4)item.layer.visible=pr.u32()!=0;
            else if(type==15&&property.size()>=8)item.layer.rect.moveTo(pr.s32(),pr.s32());
            else if(type==29)item.group=true;
            else if(type==30){const int count=pr.ok(4)?int(pr.u32()):0;for(int n=0;n<count&&pr.ok(4);++n)item.path.push_back(int(pr.u32()));}
            else if(type==21){while(pr.ok(12)){const QString parasite_name=xcf_string(pr);if(parasite_name.isEmpty()||!pr.ok(8))break;pr.u32();const quint32 size=pr.u32();const QByteArray payload=pr.bytes(size);if(parasite_name.contains(QStringLiteral("text"),Qt::CaseInsensitive)){item.layer.text=parse_gimp_text_parasite(payload,&item.layer.font_family,&item.layer.font_size);if(!item.layer.text.isEmpty())item.layer.kind=Kind::Text;}}}
        }
        item.hierarchy=offset(r);offset(r);if(item.group)item.layer.kind=Kind::Group;pending.push_back(item);
        if(item.group||item.hierarchy==0||lw<=0||lh<=0)continue;
        BeReader hr(data,item.hierarchy);const int hw=int(hr.u32()),hh=int(hr.u32()),bpp=int(hr.u32());const quint64 level_offset=offset(hr);if(hw<=0||hh<=0||bpp<1||bpp>4||level_offset==0)continue;
        BeReader lr(data,level_offset);const int level_w=int(lr.u32()),level_h=int(lr.u32());Q_UNUSED(level_w);Q_UNUSED(level_h);QVector<quint64> tiles;while(lr.ok(wide_offsets?8:4)){const quint64 value=offset(lr);if(!value)break;tiles.push_back(value);}
        QImage image(lw,lh,QImage::Format_ARGB32);image.fill(Qt::transparent);const int columns=(lw+63)/64;
        for(int t=0;t<tiles.size();++t){const int tx=(t%columns)*64,ty=(t/columns)*64,tw=qMin(64,lw-tx),th=qMin(64,lh-ty),count=tw*th;if(tw<=0||th<=0||tiles[t]>=quint64(data.size()))continue;const quint64 end=t+1<tiles.size()?tiles[t+1]:quint64(data.size());if(end<tiles[t])continue;BeReader tr(data,tiles[t],qsizetype(qMin<quint64>(end-tiles[t],quint64(data.size())-tiles[t])));QVector<QByteArray> planes;
            if(compression==2){const QByteArray raw=decode_zlib(tr.bytes(tr.end()-tr.pos()),count*bpp);if(raw.size()==count*bpp)for(int c=0;c<bpp;++c)planes.push_back(raw.mid(c*count,count));}
            else for(int c=0;c<bpp;++c){if(compression==0)planes.push_back(tr.bytes(count));else if(compression==1)planes.push_back(decode_xcf_rle(tr,count));else{planes.clear();break;}}
            if(planes.size()!=bpp)continue;for(int y=0;y<th;++y)for(int x=0;x<tw;++x){const int p=y*tw+x;int red=0,green=0,blue=0,alpha=255;if(layer_type==0||layer_type==1){red=quint8(planes[0][p]);green=bpp>1?quint8(planes[1][p]):red;blue=bpp>2?quint8(planes[2][p]):red;if(bpp>3)alpha=quint8(planes[3][p]);}else if(layer_type==4||layer_type==5){const int index=quint8(planes[0][p]);const QRgb color=index<palette.size()?palette[index]:qRgb(index,index,index);red=qRed(color);green=qGreen(color);blue=qBlue(color);if(bpp>1)alpha=quint8(planes[1][p]);}else{red=green=blue=quint8(planes[0][p]);if(bpp>1)alpha=quint8(planes[bpp-1][p]);}image.setPixel(tx+x,ty+y,qRgba(red,green,blue,alpha));}}
        pending.back().layer.pixels=image;QColor solid;if(pending.back().layer.kind==Kind::Pixel&&image_is_solid(image,solid)){pending.back().layer.kind=Kind::Solid;pending.back().layer.solid_color=solid;}
    }
    QMap<QString,int> group_by_path;
    for(int i=0;i<pending.size();++i){ImportedLayer layer=pending[i].layer;QStringList parent_parts;for(int n:pending[i].path)parent_parts<<QString::number(n);if(!parent_parts.isEmpty()){parent_parts.removeLast();layer.parent=group_by_path.value(parent_parts.join('/'),-1);}doc.layers.push_back(layer);if(pending[i].group){QStringList parts;for(int n:pending[i].path)parts<<QString::number(n);group_by_path.insert(parts.join('/'),doc.layers.size()-1);}}
    if(doc.layers.isEmpty())doc.error=fxm_tr("OBSTitles.ImportXcfNoLayers");return doc;
}

static QPainter::CompositionMode painter_blend(EffectBlendMode mode)
{
    switch(mode){case EffectBlendMode::Multiply:return QPainter::CompositionMode_Multiply;case EffectBlendMode::Screen:return QPainter::CompositionMode_Screen;case EffectBlendMode::Overlay:return QPainter::CompositionMode_Overlay;case EffectBlendMode::Additive:return QPainter::CompositionMode_Plus;default:return QPainter::CompositionMode_SourceOver;}
}

static QImage render_imported_children(const ImportedDocument &doc, int parent)
{
    QImage result(doc.size,QImage::Format_ARGB32_Premultiplied);result.fill(Qt::transparent);QPainter painter(&result);
    for(int i=doc.layers.size()-1;i>=0;--i){const auto &layer=doc.layers[i];if(layer.parent!=parent||!layer.visible||layer.kind==Kind::GroupEnd)continue;painter.setOpacity(layer.opacity);painter.setCompositionMode(painter_blend(layer.blend));if(layer.kind==Kind::Group)painter.drawImage(QPoint(),render_imported_children(doc,i));else if(!layer.pixels.isNull())painter.drawImage(layer.rect.topLeft(),layer.pixels);}painter.end();return result;
}

static QImage merged_image(const ImportedDocument &doc)
{
    if(!doc.merged_preview.isNull())return doc.merged_preview;
    return render_imported_children(doc,-1);
}

static QString save_imported_png(const QImage &image,const QString &source,const QString &suffix,QString &error)
{
    const QString root=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/imported-media");if(!QDir().mkpath(root)){error=fxm_tr("OBSTitles.ImportCacheFailed");return{};}
    const QFileInfo source_info(source);const QByteArray identity=source_info.absoluteFilePath().toUtf8()+QByteArray::number(source_info.lastModified().toMSecsSinceEpoch())+QByteArray::number(source_info.size())+suffix.toUtf8();const QString hash=QString::fromLatin1(QCryptographicHash::hash(identity,QCryptographicHash::Sha256).toHex().left(20));const QString path=root+QLatin1Char('/')+hash+QStringLiteral(".png");
    if(QFileInfo::exists(path))return path;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){error=file.errorString();return{};}QImage copy=image;if(!copy.save(&file,"PNG")||!file.commit()){error=file.errorString();return{};}return path;
}

} // namespace fxm_document_import

static QStringList unified_import_image_patterns()
{
    QSet<QString> unique;
    for (const QByteArray &format : QImageReader::supportedImageFormats()) {
        const QString suffix = QString::fromLatin1(format).trimmed().toLower();
        if (!suffix.isEmpty()) unique.insert(QStringLiteral("*.") + suffix);
    }
    QStringList patterns;
    for (const QString &pattern : unique) patterns.push_back(pattern);
    patterns.sort(Qt::CaseInsensitive);
    return patterns;
}

static QString unified_import_filter()
{
    const QStringList title_patterns = {
        QStringLiteral("*.fxmp"), QStringLiteral("*.fxmt"),
        QStringLiteral("*.json")};
    const QStringList document_patterns = {
        QStringLiteral("*.svg"), QStringLiteral("*.psd"), QStringLiteral("*.xcf")};
    const QStringList image_patterns = unified_import_image_patterns();
    QStringList all_patterns = title_patterns + document_patterns + image_patterns;
    all_patterns.removeDuplicates();
    return QStringList{
        QStringLiteral("%1 (%2)").arg(
            fxm_tr("OBSTitles.ImportAllSupported"), all_patterns.join(' ')),
        QStringLiteral("%1 (%2)").arg(
            fxm_tr("OBSTitles.ImportFxmTitles"), title_patterns.join(' ')),
        QStringLiteral("%1 (%2)").arg(
            fxm_tr("OBSTitles.ImportDocuments"), document_patterns.join(' ')),
        QStringLiteral("%1 (%2)").arg(
            fxm_tr("OBSTitles.ImportImages"), image_patterns.join(' ')),
        fxm_tr("OBSTitles.AllFiles")
    }.join(QStringLiteral(";;"));
}

void TitleEditor::import_document()
{
    if (!title_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, fxm_tr("OBSTitles.ImportUnifiedTitle"), QString(),
        unified_import_filter());
    if (path.isEmpty()) return;

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("svg")) {
        import_vector_document(path);
        return;
    }
    if (suffix == QStringLiteral("psd")) {
        import_photoshop_document(path);
        return;
    }
    if (suffix == QStringLiteral("xcf")) {
        import_gimp_document(path);
        return;
    }
    if (suffix == QStringLiteral("fxmp") || suffix == QStringLiteral("fxmt") ||
        suffix == QStringLiteral("json")) {
        if (!confirm_save_before_close()) return;
        std::string error;
        auto imported = TitleDataStore::instance().import_title(
            path.toStdString(), &error, nullptr);
        if (!imported) {
            QMessageBox::warning(this, fxm_tr("OBSTitles.ImportFailed"),
                error.empty() ? fxm_tr("OBSTitles.ImportFailed")
                              : QString::fromStdString(error));
            return;
        }
        for (const auto &layer : imported->layers)
            if (layer && layer->type == LayerType::Group)
                layer->group_collapsed = true;
        TitleDataStore::instance().notify_change();
        TitleDataStore::instance().save_async();
        open_title(imported->id);
        show_editor_activity(fxm_tr("OBSTitles.ImportedStatusFormat")
            .arg(QString::fromStdString(imported->name)));
        return;
    }

    QImageReader reader(path);
    if (!reader.canRead()) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportFailed"),
                             reader.errorString());
        return;
    }
    const QPointF center = canvas_ ? canvas_->view_center_canvas_point()
                                   : QPointF(title_->width * 0.5, title_->height * 0.5);
    create_image_layer_from_external_source(path, center);
    show_editor_activity(fxm_tr("OBSTitles.ImportedStatusFormat")
        .arg(QFileInfo(path).fileName()));
}

void TitleEditor::import_vector_document()
{
    if (!title_) return;
    const QString path = QFileDialog::getOpenFileName(this, fxm_tr("OBSTitles.ImportVectorTitle"), QString(), fxm_tr("OBSTitles.ImportVectorFilter"));
    if (path.isEmpty()) return;
    import_vector_document(path);
}

void TitleEditor::import_vector_document(const QString &path)
{
    if (!title_ || path.isEmpty()) return;
    QVector<fxm_document_import::SvgShape> shapes; QRectF view_box; QString error;
    if (!fxm_document_import::read_svg(path, shapes, view_box, error)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportFailed"), error); return;
    }
    const QPointF document_offset(title_->width/2.0-view_box.center().x(), title_->height/2.0-view_box.center().y());
    std::shared_ptr<Layer> group;
    if (shapes.size() > 1) {
        group = create_basic_layer(LayerType::Group, QFileInfo(path).completeBaseName());
        group->group_collapsed = true; group->rect_width=float(view_box.width()); group->rect_height=float(view_box.height());
        group->size.static_value={view_box.width(),view_box.height()}; group->position.static_value={title_->width/2.0,title_->height/2.0}; title_->add_layer(group);
    }
    std::string last_id;
    for (const auto &shape : shapes) {
        const QRectF path_bounds=shape.path.boundingRect();
        const QRectF bounds=shape.native_text&&shape.text_local_bounds.isValid()&&!shape.text_local_bounds.isEmpty()?shape.text_local_bounds:path_bounds;
        if (!bounds.isValid()||bounds.isEmpty()) continue;
        std::shared_ptr<Layer> layer;
        if(shape.native_text&&!shape.text.isEmpty()){
            layer=create_basic_layer(LayerType::Text,shape.name);
            layer->text_content=shape.text.toStdString();
            RichTextDocument rich=rich_text_document_from_layer_defaults(*layer);
            rich.plain_text=layer->text_content;
            rich.default_format=fxm_document_import::svg_rich_text_format(shape.style);
            rich.default_paragraph_format.align_h=shape.style.text_anchor==QStringLiteral("end")?2:(shape.style.text_anchor==QStringLiteral("middle")?1:0);
            rich.default_paragraph_format.align_v=0;
            rich.ranges.clear();
            for(const auto &source_run:shape.text_runs){if(source_run.length==0||source_run.start>=rich.plain_text.size())continue;RichTextRange run;run.start=source_run.start;run.length=std::min(source_run.length,rich.plain_text.size()-source_run.start);run.format=fxm_document_import::svg_rich_text_format(source_run.style);run.mask=RichTextCharAll;rich.ranges.push_back(run);}
            rich.normalize();layer->rich_text=std::move(rich);rich_text_document_sync_layer_mirrors_canonical(*layer);
        }else{
            layer=create_basic_layer(LayerType::Shape,shape.name);layer->shape_type=ShapeType::Path;layer->path_closed=shape.closed;layer->path_points=fxm_document_import::painter_path_points(shape.path,bounds);
        }
        layer->rect_width=float(bounds.width()); layer->rect_height=float(bounds.height());
        layer->size.static_value={bounds.width(),bounds.height()};const QPointF imported_center=shape.native_text?shape.text_transform.map(bounds.center()):bounds.center();layer->position.static_value={imported_center.x()+document_offset.x(),imported_center.y()+document_offset.y()};
        if(shape.native_text){const double sx=std::hypot(shape.text_transform.m11(),shape.text_transform.m12());const double determinant=shape.text_transform.m11()*shape.text_transform.m22()-shape.text_transform.m12()*shape.text_transform.m21();const double sy=sx>1e-9?determinant/sx:1.0;layer->rotation.static_value=std::atan2(shape.text_transform.m12(),shape.text_transform.m11())*57.295779513082320876798154814105;layer->scale.static_value={sx,sy,1.0};}
        layer->fill_type=shape.style.fill_gradient_stops.size()>=2?1:0; layer->fill_color=shape.style.fill_none?0:fxm_document_import::argb(shape.style.fill,shape.style.fill_opacity);
        if(layer->fill_type==1){layer->gradient_type=shape.style.fill_gradient_type;layer->gradient_spread=shape.style.fill_gradient_spread;layer->gradient_angle=float(shape.style.fill_gradient_angle);layer->gradient_center_x=float(shape.style.fill_gradient_center_x);layer->gradient_center_y=float(shape.style.fill_gradient_center_y);layer->gradient_scale=float(shape.style.fill_gradient_scale);layer->gradient_focal_x=float(shape.style.fill_gradient_focal_x);layer->gradient_focal_y=float(shape.style.fill_gradient_focal_y);const auto first=shape.style.fill_gradient_stops.front(),last=shape.style.fill_gradient_stops.back();layer->gradient_start_color=fxm_document_import::argb(first.second,shape.style.fill_opacity);layer->gradient_end_color=fxm_document_import::argb(last.second,shape.style.fill_opacity);layer->gradient_start_pos=float(first.first);layer->gradient_end_pos=float(last.first);layer->gradient_stops.clear();for(int i=1;i+1<shape.style.fill_gradient_stops.size();++i){GradientStop stop;stop.position=float(shape.style.fill_gradient_stops[i].first);stop.color=fxm_document_import::argb(shape.style.fill_gradient_stops[i].second,shape.style.fill_opacity);stop.opacity=1.0f;layer->gradient_stops.push_back(stop);}}
        layer->outline_enabled=!shape.style.stroke_none&&shape.style.stroke_width>0.0; layer->stroke_fill_type=shape.style.stroke_gradient_stops.size()>=2?2:(layer->outline_enabled?1:0);
        layer->stroke_color=fxm_document_import::argb(shape.style.stroke,shape.style.stroke_opacity); layer->stroke_width=float(shape.style.stroke_width);layer->outline_join_style=shape.style.stroke_join_style;
        if(layer->stroke_fill_type==2){layer->stroke_gradient_type=shape.style.stroke_gradient_type;layer->stroke_gradient_spread=shape.style.stroke_gradient_spread;layer->stroke_gradient_angle=float(shape.style.stroke_gradient_angle);layer->stroke_gradient_center_x=float(shape.style.stroke_gradient_center_x);layer->stroke_gradient_center_y=float(shape.style.stroke_gradient_center_y);layer->stroke_gradient_scale=float(shape.style.stroke_gradient_scale);layer->stroke_gradient_focal_x=float(shape.style.stroke_gradient_focal_x);layer->stroke_gradient_focal_y=float(shape.style.stroke_gradient_focal_y);const auto first=shape.style.stroke_gradient_stops.front(),last=shape.style.stroke_gradient_stops.back();layer->stroke_gradient_start_color=fxm_document_import::argb(first.second,shape.style.stroke_opacity);layer->stroke_gradient_end_color=fxm_document_import::argb(last.second,shape.style.stroke_opacity);layer->stroke_gradient_start_pos=float(first.first);layer->stroke_gradient_end_pos=float(last.first);layer->stroke_gradient_stops.clear();for(int i=1;i+1<shape.style.stroke_gradient_stops.size();++i){GradientStop stop;stop.position=float(shape.style.stroke_gradient_stops[i].first);stop.color=fxm_document_import::argb(shape.style.stroke_gradient_stops[i].second,shape.style.stroke_opacity);stop.opacity=1.0f;layer->stroke_gradient_stops.push_back(stop);}}
        layer->opacity.static_value=shape.native_text?1.0:qBound(0.0,shape.style.opacity,1.0);set_channel_statics(*layer,false,layer->fill_color);if(shape.native_text)set_channel_statics(*layer,true,layer->text_color);
        if (group) layer->parent_id=group->id; title_->add_layer(layer); last_id=layer->id;
    }
    if (last_id.empty()) { if(group)title_->remove_layer(group->id); QMessageBox::warning(this,fxm_tr("OBSTitles.ImportFailed"),fxm_tr("OBSTitles.ImportSvgNoShapes")); return; }
    layers_->refresh(); timeline_->set_title(title_); synchronize_layer_selection({group?group->id:last_id}); force_next_title_visual_update(); on_title_modified();
    show_editor_activity(fxm_tr("OBSTitles.ImportVectorComplete").arg(shapes.size()));
}

static int layered_import_mode(QWidget *parent, const QString &application_name)
{
    QDialog dialog(parent); dialog.setWindowTitle(fxm_tr("OBSTitles.ImportOptionsTitle").arg(application_name));
    auto *layout=new QVBoxLayout(&dialog); auto *description=new QLabel(fxm_tr("OBSTitles.ImportOptionsDescription").arg(application_name),&dialog); description->setWordWrap(true); layout->addWidget(description);
    auto *merged=new QRadioButton(fxm_tr("OBSTitles.ImportMergedBitmap"),&dialog); auto *layers=new QRadioButton(fxm_tr("OBSTitles.ImportSeparateLayers"),&dialog); layers->setChecked(true);
    layout->addWidget(merged); layout->addWidget(layers);auto *note=new QLabel(fxm_tr("OBSTitles.ImportSeparateLayersNote"),&dialog);note->setWordWrap(true);note->setStyleSheet(QStringLiteral("color:palette(mid);font-size:11px;"));layout->addWidget(note); auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return -1; return merged->isChecked()?0:1;
}

void TitleEditor::import_gimp_document()
{
    const QString path=QFileDialog::getOpenFileName(this,fxm_tr("OBSTitles.ImportGimpTitle"),QString(),fxm_tr("OBSTitles.ImportGimpFilter"));if(path.isEmpty())return;
    import_gimp_document(path);
}

void TitleEditor::import_gimp_document(const QString &path)
{
    if(path.isEmpty())return;
    const int mode=layered_import_mode(this,QStringLiteral("GIMP"));if(mode<0)return;
    setProperty("fxm_layered_import_mode",mode);import_layered_document(path,false);setProperty("fxm_layered_import_mode",QVariant());
}

void TitleEditor::import_photoshop_document()
{
    const QString path=QFileDialog::getOpenFileName(this,fxm_tr("OBSTitles.ImportPhotoshopTitle"),QString(),fxm_tr("OBSTitles.ImportPhotoshopFilter"));if(path.isEmpty())return;
    import_photoshop_document(path);
}

void TitleEditor::import_photoshop_document(const QString &path)
{
    if(path.isEmpty())return;
    const int mode=layered_import_mode(this,QStringLiteral("Photoshop"));if(mode<0)return;
    setProperty("fxm_layered_import_mode",mode);import_layered_document(path,true);setProperty("fxm_layered_import_mode",QVariant());
}

void TitleEditor::import_layered_document(const QString &path, bool photoshop)
{
    if(!title_)return;const int mode=property("fxm_layered_import_mode").toInt();
    fxm_document_import::ImportedDocument doc=photoshop?fxm_document_import::read_psd(path):fxm_document_import::read_xcf(path);
    if(!doc.error.isEmpty()){QMessageBox::warning(this,fxm_tr("OBSTitles.ImportFailed"),doc.error);return;}
    QString error;const QPointF offset(title_->width/2.0-doc.size.width()/2.0,title_->height/2.0-doc.size.height()/2.0);std::string selected;
    if(mode==0){const QImage merged=fxm_document_import::merged_image(doc);const QString png=fxm_document_import::save_imported_png(merged,path,QStringLiteral("-merged"),error);if(png.isEmpty()){QMessageBox::warning(this,fxm_tr("OBSTitles.ImportFailed"),error);return;}
        auto layer=create_basic_layer(LayerType::Image,QFileInfo(path).completeBaseName());layer->image_path=png.toStdString();layer->image_width=float(doc.size.width());layer->image_height=float(doc.size.height());layer->image_size.static_value={double(doc.size.width()),double(doc.size.height())};layer->rect_width=float(doc.size.width());layer->rect_height=float(doc.size.height());layer->size.static_value={double(doc.size.width()),double(doc.size.height())};layer->position.static_value={title_->width/2.0,title_->height/2.0};layer->lock_aspect_ratio=true;title_->add_layer(layer);selected=layer->id;
    }else{
        auto root=create_basic_layer(LayerType::Group,QFileInfo(path).completeBaseName());root->group_collapsed=true;root->rect_width=float(doc.size.width());root->rect_height=float(doc.size.height());root->size.static_value={double(doc.size.width()),double(doc.size.height())};root->position.static_value={title_->width/2.0,title_->height/2.0};title_->add_layer(root);selected=root->id;
        QVector<std::string> imported_ids(doc.layers.size());int imported_content_count=0;
        for(int i=0;i<doc.layers.size();++i){const auto &source=doc.layers[i];std::shared_ptr<Layer> layer;QRect rect=source.rect.isValid()?source.rect:QRect(QPoint(),doc.size);
            if(source.kind==fxm_document_import::Kind::Group){layer=create_basic_layer(LayerType::Group,source.name);layer->group_collapsed=true;}
            else if(source.kind==fxm_document_import::Kind::Text&&!source.text.isEmpty()){layer=create_basic_layer(LayerType::Text,source.name);const std::string decoded_text=source.text.toUtf8().toStdString();layer->text_content=decoded_text;if(!source.rich_text.empty()){layer->rich_text=source.rich_text;layer->rich_text.plain_text=decoded_text;layer->rich_text.normalize();rich_text_document_sync_layer_mirrors_canonical(*layer);}else{layer->rich_text=rich_text_document_from_layer_defaults(*layer);rich_text_document_replace_text(layer->rich_text,decoded_text);rich_text_document_sync_layer_mirrors(*layer);if(!source.font_family.isEmpty())layer->font_family=source.font_family.toUtf8().toStdString();layer->font_size=qMax(1,qRound(source.font_size));layer->font_size_prop.static_value=layer->font_size;}}
            else if(source.kind==fxm_document_import::Kind::Solid){layer=create_basic_layer(LayerType::ColorSolid,source.name);layer->shape_type=ShapeType::Rectangle;layer->fill_color=fxm_document_import::argb(source.solid_color);set_channel_statics(*layer,false,layer->fill_color);layer->outline_enabled=false;}
            else if(source.kind==fxm_document_import::Kind::Shape&&!source.vector_path.isEmpty()){const QRectF bounds=source.vector_path.boundingRect();rect=bounds.toAlignedRect();layer=create_basic_layer(LayerType::Shape,source.name);layer->shape_type=ShapeType::Path;layer->path_closed=true;layer->path_points=fxm_document_import::painter_path_points(source.vector_path,bounds);layer->fill_color=fxm_document_import::argb(source.solid_color.isValid()?source.solid_color:QColor(Qt::white));set_channel_statics(*layer,false,layer->fill_color);layer->outline_enabled=false;}
            else if(source.kind==fxm_document_import::Kind::Adjustment){layer=create_basic_layer(LayerType::Adjustment,source.name);rect=QRect(QPoint(),doc.size);}
            else{if(source.pixels.isNull())continue;const QString png=fxm_document_import::save_imported_png(source.pixels,path,QStringLiteral("-layer-%1").arg(i),error);if(png.isEmpty()){QMessageBox::warning(this,fxm_tr("OBSTitles.ImportFailed"),error);continue;}layer=create_basic_layer(LayerType::Image,source.name);layer->image_path=png.toStdString();layer->image_width=float(source.pixels.width());layer->image_height=float(source.pixels.height());layer->image_size.static_value={double(source.pixels.width()),double(source.pixels.height())};layer->lock_aspect_ratio=true;}
            layer->rect_width=float(qMax(1,rect.width()));layer->rect_height=float(qMax(1,rect.height()));layer->size.static_value={double(qMax(1,rect.width())),double(qMax(1,rect.height()))};layer->position.static_value={rect.x()+rect.width()/2.0+offset.x(),rect.y()+rect.height()/2.0+offset.y()};layer->visible=source.visible;layer->opacity.static_value=qBound(0.0,source.opacity,1.0);layer->blend_mode=source.blend;layer->effects=source.effects;layer->effects.erase(std::remove_if(layer->effects.begin(),layer->effects.end(),[](const LayerEffect &effect){return !effect.enabled;}),layer->effects.end());
            layer->parent_id=(source.parent>=0&&source.parent<imported_ids.size()&&!imported_ids[source.parent].empty())?imported_ids[source.parent]:root->id;title_->add_layer(layer);imported_ids[i]=layer->id;if(source.kind!=fxm_document_import::Kind::Group)++imported_content_count;
        }
        if(imported_content_count==0){for(const auto &id:imported_ids)if(!id.empty())title_->remove_layer(id);title_->remove_layer(root->id);QMessageBox::warning(this,fxm_tr("OBSTitles.ImportFailed"),fxm_tr("OBSTitles.ImportNoReadableContent"));return;}
    }
    layers_->refresh();timeline_->set_title(title_);synchronize_layer_selection({selected});force_next_title_visual_update();on_title_modified();show_editor_activity(fxm_tr("OBSTitles.ImportLayeredComplete").arg(doc.layers.size()));
}
