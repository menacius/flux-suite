#pragma once

#include "title-data.h"

#include <QIcon>
#include <QString>

namespace fxm::editor::tool_icons {

QString shape_display_name(ShapeType shape_type);
QIcon shape_tool_icon(ShapeType shape_type);
QIcon cursor_tool_icon();
QIcon direct_selection_tool_icon();
QIcon pen_tool_icon();
QIcon gradient_tool_icon();
QString text_tool_display_name(LayerType type);
QIcon text_tool_icon(LayerType type);

} // namespace fxm::editor::tool_icons
