#pragma once

#include "title-rich-text.h"

#include <QJsonObject>

#include <cstdint>
#include <string>

namespace fxm::style_presets {

bool apply_text_preset_payload(const QJsonObject &payload,
                               RichTextCharFormat &format);
uint32_t text_preset_char_mask();
bool resolve_text_preset(const std::string &preset_id,
                         RichTextCharFormat &format, uint32_t &mask);

} // namespace fxm::style_presets
