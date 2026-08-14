#pragma once

#include "layer-transition.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

class QMimeData;

namespace fxm::transitions {

inline constexpr const char *kTransitionPresetMimeType = "application/x-obs-fxm-transition-preset";
inline constexpr const char *kTextTransitionExtension = ".fxmtranst";
inline constexpr const char *kGeneralTransitionExtension = ".fxmtransg";

struct TransitionPresetDescriptor {
    QString file_path;
    QString id;
    QString display_name;
    QStringList category_path;
    LayerTransition transition;
};

bool is_transition_preset_path(const QString &file_path);
bool is_transition_file_in_library(const QString &file_path);
bool load_transition_preset_file(const QString &file_path,
                                 TransitionPresetDescriptor *descriptor,
                                 QString *error_message = nullptr);

QByteArray encode_transition_preset_mime(const QString &file_path);
QString transition_preset_path_from_mime(const QMimeData *mime_data);
bool mime_has_transition_preset(const QMimeData *mime_data);

QString transition_type_id(LayerTransitionType type);
bool transition_type_from_id(const QString &id, LayerTransitionType *type);
QString transition_unit_id(LayerTransitionUnit unit);
bool transition_unit_from_id(const QString &id, LayerTransitionUnit *unit);
QString transition_direction_id(LayerTransitionDirection direction);
bool transition_direction_from_id(const QString &id, LayerTransitionDirection *direction);
QString transition_easing_id(EasingType easing);
bool transition_easing_from_id(const QString &id, EasingType *easing);

} // namespace fxm::transitions
