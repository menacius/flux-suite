#pragma once

#include <QString>

class QWidget;

namespace fxm::obs_plugin {

bool uncue_before_scene_transition();
void set_uncue_before_scene_transition(bool enabled);
QString configured_editor_executable();
void set_configured_editor_executable(const QString &path);
QString resolved_editor_executable();
void show_plugin_preferences(QWidget *parent = nullptr);

} // namespace fxm::obs_plugin
