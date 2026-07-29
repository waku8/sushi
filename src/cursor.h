/* sushi: cursor.h -- Plan 9 "nein" cursor theme */
#ifndef SUSHI_CURSOR_H
#define SUSHI_CURSOR_H

#include "config.h"

/* Enables/disables the compositor-drawn Plan 9 style cursors per
 * cfg->cursor_theme, using cfg->cursor_color_in/cursor_color_out for the
 * two-tone glyph colors. Safe to call again (e.g. on config reload) to
 * pick up color or theme-toggle changes. */
void cursor_apply(const struct sushi_config *cfg);

#endif
