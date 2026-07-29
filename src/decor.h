/* sushi: decor.h -- theme engine (flat / classic / simple) */
#ifndef SUSHI_DECOR_H
#define SUSHI_DECOR_H

#include "sushi.h"

/* (Re)builds and pushes the swc_decor for a window: border, title bar
 * background (themes "flat"/"classic" only -- "simple" never shows a
 * title bar), and (theme "classic" only) the minimize/maximize-restore/
 * close button glyphs, based on the configured theme and the window's
 * decor_title/decor_border flags. Also refreshes
 * win->decor_{top,right,bottom,left}. Call on: window creation, focus
 * change, maximize toggle, title change, theme/rule reload. */
void decor_apply(struct sushi_window *win);

/* Hit-tests a compositor-global point against the window's *current*
 * decoration (geometry is re-read live, so this is always accurate even
 * mid-drag without needing a cached, potentially-stale hitbox). Returns
 * HIT_NONE if the point isn't over this window's decor at all. */
enum sushi_hit_kind decor_hit_test(struct sushi_window *win, int32_t x, int32_t y);

#endif
