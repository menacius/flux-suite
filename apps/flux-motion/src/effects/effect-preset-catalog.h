#pragma once

#include "layer-effects.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

class QMimeData;

namespace fxm::effects {

inline constexpr const char *kEffectPresetMimeType = "application/x-obs-fxm-effect-preset";
inline constexpr const char *kEffectPresetExtension = ".fxmeffect";

struct EffectPresetDescriptor {
    QString file_path;
    QString id;
    QString display_name;
    QString kind;
    QStringList category_path;
    LayerEffect effect;
};

LayerEffect make_default_layer_effect(LayerEffectType type);
QString effect_type_id(LayerEffectType type);
bool effect_type_from_id(const QString &id, LayerEffectType *type);

QString effect_presets_root_path();
bool load_effect_preset_file(const QString &file_path,
                             EffectPresetDescriptor *descriptor,
                             QString *error_message = nullptr);

QByteArray encode_effect_preset_mime(const QString &file_path);
QString effect_preset_path_from_mime(const QMimeData *mime_data);
bool mime_has_effect_preset(const QMimeData *mime_data);

} // namespace fxm::effects
