#pragma once

#include "title-text-layout.h"

#include <QRawFont>

/* Qt shaping may resolve a different physical fallback face than the family
 * requested by the document. Family/style reconstruction is not reliable on
 * every platform (notably Fontconfig/Linux), so the shaping adapter retains a
 * bounded copy of the exact QRawFont keyed by the immutable face fingerprint.
 * The GPU atlas uses this first and only falls back to database reconstruction
 * when a layout was produced outside the current process/session. */
QRawFont text_layout_registered_raw_font(const TextLayoutFontKey &key);
void text_layout_register_raw_font(const TextLayoutFontKey &key,
                                   const QRawFont &font);
void text_layout_clear_raw_font_registry();
