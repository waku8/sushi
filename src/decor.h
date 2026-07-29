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

/* Whether a compositor-global point falls anywhere within the window's
 * outer rectangle -- decorations included, not just the content area.
 * Used to stop a hit-test walk at the topmost window covering a point,
 * since decor_hit_test() alone cannot tell "not my decoration" apart from
 * "not my window at all". */
bool decor_window_contains(struct sushi_window *win, int32_t x, int32_t y);

/* Hit-tests a compositor-global point against the window's *current*
 * decoration (geometry is re-read live, so this is always accurate even
 * mid-drag without needing a cached, potentially-stale hitbox). Returns
 * HIT_NONE if the point isn't over this window's decor at all. */
enum sushi_hit_kind decor_hit_test(struct sushi_window *win, int32_t x, int32_t y);

#endif
