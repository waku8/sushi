/* decor.c: theme engine (flat / classic / simple / love / win95)
 *
 * Builds swc_decor structs (border + title bar + button glyphs) as raw
 * ARGB8888 pixel blocks. All rendering here is a tiny hand-rolled
 * rectangle+glyph rasterizer, no font/drawing library needed, since
 * swc's decor engine already renders the title text itself (via a
 * fontconfig pattern string) and just wants pixel blocks for the button
 * art.
 *
 * `flat`: title bar, no buttons.
 * `classic`: `flat`'s colors (no separate border frame color, the border
 * is just the title color) plus three clickable glyphs: minimize,
 * maximize/restore, and close, in that left-to-right order (close
 * outermost).
 * `simple`: no title bar at all, ever, just the border (if enabled).
 * `love`: like `classic`, but all three buttons show the same heart glyph.
 * `win95`: Chicago-style beveled gray frame/buttons, navy title bar, a
 * little window icon at the top-left. Unlike the other themes it has its
 * own hardcoded 3D palette (border/title bg, button glyphs) since the
 * whole point is the bevel look. Only the title text still comes from
 * text-color-active/inactive.
 */
#include "decor.h"

#include <stdlib.h>
#include <string.h>

#include <swc.h>

/* win95's own fixed palette, see the header comment. flat/classic/simple/
 * love instead take their border/title color from cfg (border-color-active/
 * border-color-inactive). */
static const uint32_t WIN95_FACE = 0xFFC0C0C0;
static const uint32_t WIN95_LIGHT1 = 0xFFC0C0C0;
static const uint32_t WIN95_LIGHT2 = 0xFFFFFFFF;
static const uint32_t WIN95_DARK1 = 0xFF808080;
static const uint32_t WIN95_DARK2 = 0xFF000000;
static const uint32_t WIN95_TITLE_ACTIVE = 0xFF000080;
static const uint32_t WIN95_TITLE_INACTIVE = 0xFF808080;

enum theme_id {
	THEME_FLAT,
	THEME_CLASSIC,
	THEME_SIMPLE,
	THEME_LOVE,
	THEME_WIN95,
};

static enum theme_id
theme_from_name(const char *name)
{
	if (name && !strcmp(name, "classic"))
		return THEME_CLASSIC;
	if (name && !strcmp(name, "simple"))
		return THEME_SIMPLE;
	if (name && !strcmp(name, "love"))
		return THEME_LOVE;
	if (name && !strcmp(name, "win95"))
		return THEME_WIN95;
	return THEME_FLAT;
}

static bool
theme_has_titlebar(enum theme_id theme)
{
	return theme != THEME_SIMPLE;
}

static bool
theme_has_buttons(enum theme_id theme)
{
	return theme == THEME_CLASSIC || theme == THEME_LOVE || theme == THEME_WIN95;
}

#define GLYPH_W 16
#define GLYPH_H 13

/* Fixed per-button cell width, independent of title_height: at
 * title_height's default of 18, GLYPH_W left only a 1px margin on each
 * side, so the min/max/close icons ended up nearly touching, looking like
 * they were overlapping. This gives each icon a comfortable, consistent
 * margin regardless of title_height. */
#define BUTTON_W (GLYPH_W + 6)

/* Fixed left inset for the title text, the same for every theme (see the
 * offset_x comment in decor_apply()). */
#define TITLE_LEFT_MARGIN 6

static const uint8_t GLYPH_MIN[GLYPH_H][GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH_MAX[GLYPH_H][GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH_RESTORE[GLYPH_H][GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH_CLOSE[GLYPH_H][GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
/* theme "love": every button shows this instead. */
static const uint8_t GLYPH_HEART[GLYPH_H][GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0 },
	{ 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
};

#define WIN95_GLYPH_W 16
#define WIN95_GLYPH_H 14
#define WIN95_ICON_SIZE 14

static const uint8_t GLYPH95_MIN[WIN95_GLYPH_H][WIN95_GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH95_MAX[WIN95_GLYPH_H][WIN95_GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH95_RESTORE[WIN95_GLYPH_H][WIN95_GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
static const uint8_t GLYPH95_CLOSE[WIN95_GLYPH_H][WIN95_GLYPH_W] = {
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static void
fill_rect(uint32_t *buf, int bw, int bh, int x, int y, int w, int h, uint32_t color)
{
	if (w <= 0 || h <= 0)
		return;

	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > bw ? bw : x + w;
	int y1 = y + h > bh ? bh : y + h;

	for (int j = y0; j < y1; j++) {
		uint32_t *row = buf + (size_t)j * bw;
		for (int i = x0; i < x1; i++)
			row[i] = color;
	}
}

static void
draw_glyph(uint32_t *buf, int bw, int bh, int x0, int y0,
          const uint8_t glyph[GLYPH_H][GLYPH_W], int scale, uint32_t fg)
{
	for (int gy = 0; gy < GLYPH_H; gy++) {
		for (int gx = 0; gx < GLYPH_W; gx++) {
			if (glyph[gy][gx])
				fill_rect(buf, bw, bh, x0 + gx * scale, y0 + gy * scale, scale,
				         scale, fg);
		}
	}
}

static void
draw_glyph95(uint32_t *buf, int bw, int bh, int x0, int y0,
            const uint8_t glyph[WIN95_GLYPH_H][WIN95_GLYPH_W], uint32_t fg)
{
	for (int gy = 0; gy < WIN95_GLYPH_H; gy++) {
		for (int gx = 0; gx < WIN95_GLYPH_W; gx++) {
			if (glyph[gy][gx])
				fill_rect(buf, bw, bh, x0 + gx, y0 + gy, 1, 1, fg);
		}
	}
}

/* win95's 3D bevel: raised gray frame/buttons with a navy title band.
 * These helpers only ever compute the "at rest" (not pressed) look:
 * sushi's decor is rebuilt on focus/state change, not per mouse-move, so
 * there's no live press feedback for any theme. Each block below is
 * computed from purely local coordinates (never from the window's overall
 * outer size), using two flags to say whether this particular block's own
 * right/bottom edge happens to be a true window edge (and therefore gets
 * the "dark/shadow" bevel side) or just an internal seam against the next
 * tiled part. */
static uint32_t
win95_corner_px(int lx, int ly, int w, int h, bool touches_right, bool touches_bottom,
                int bw, int th, uint32_t title_bg, bool title_band)
{
	bool r_edge = touches_right && lx == w - 1;
	bool r_edge1 = touches_right && lx == w - 2;
	bool b_edge = touches_bottom && ly == h - 1;
	bool b_edge1 = touches_bottom && ly == h - 2;
	bool ring0 = (bw > 0) && (lx == 0 || ly == 0 || r_edge || b_edge);
	bool ring1 = !ring0 && (bw > 1) && (lx == 1 || ly == 1 || r_edge1 || b_edge1);

	if (ring0)
		return (r_edge || b_edge) ? WIN95_DARK2 : WIN95_LIGHT1;
	if (ring1)
		return (r_edge1 || b_edge1) ? WIN95_DARK1 : WIN95_LIGHT2;
	if (title_band && ly >= bw && ly < bw + th && lx >= bw &&
	    (!touches_right || lx < w - bw))
		return title_bg;
	return WIN95_FACE;
}

static uint32_t
win95_top_row(int ly, int bw, int th, uint32_t title_bg)
{
	if (bw > 0 && ly == 0)
		return WIN95_LIGHT1;
	if (bw > 1 && ly == 1)
		return WIN95_LIGHT2;
	if (ly < bw)
		return WIN95_FACE;
	if (ly < bw + th)
		return title_bg;
	return WIN95_FACE;
}

static uint32_t
win95_bottom_row(int ly, int bw)
{
	if (ly == bw - 1)
		return WIN95_DARK2;
	if (bw > 1 && ly == bw - 2)
		return WIN95_DARK1;
	return WIN95_FACE;
}

static uint32_t
win95_left_col(int lx, int bw)
{
	if (lx == 0)
		return WIN95_LIGHT1;
	if (bw > 1 && lx == 1)
		return WIN95_LIGHT2;
	return WIN95_FACE;
}

static uint32_t
win95_right_col(int lx, int bw)
{
	if (lx == bw - 1)
		return WIN95_DARK2;
	if (bw > 1 && lx == bw - 2)
		return WIN95_DARK1;
	return WIN95_FACE;
}

static void
win95_button_bevel(uint32_t *buf, int bufw, int bufh, int x0, int y0, int w, int h)
{
	fill_rect(buf, bufw, bufh, x0, y0, w, h, WIN95_FACE);
	fill_rect(buf, bufw, bufh, x0, y0, w, 1, WIN95_LIGHT2);
	fill_rect(buf, bufw, bufh, x0, y0, 1, h, WIN95_LIGHT2);
	fill_rect(buf, bufw, bufh, x0, y0 + h - 1, w, 1, WIN95_DARK2);
	fill_rect(buf, bufw, bufh, x0 + w - 1, y0, 1, h, WIN95_DARK2);
	if (w > 2 && h > 2) {
		fill_rect(buf, bufw, bufh, x0 + 1, y0 + 1, w - 2, 1, WIN95_LIGHT1);
		fill_rect(buf, bufw, bufh, x0 + 1, y0 + 1, 1, h - 2, WIN95_LIGHT1);
		fill_rect(buf, bufw, bufh, x0 + 1, y0 + h - 2, w - 2, 1, WIN95_DARK1);
		fill_rect(buf, bufw, bufh, x0 + w - 2, y0 + 1, 1, h - 2, WIN95_DARK1);
	}
}

/* A tiny "little window" pictogram: black outline, white body, navy title
 * strip. This is the app icon the win95 theme shows at the top-left. */
static void
draw_win95_appicon(uint32_t *buf, int bufw, int bufh, int x0, int y0, int size)
{
	fill_rect(buf, bufw, bufh, x0, y0, size, size, WIN95_DARK2);
	fill_rect(buf, bufw, bufh, x0 + 1, y0 + 1, size - 2, size - 2, WIN95_LIGHT2);
	int band = size / 3;
	if (band < 2)
		band = 2;
	fill_rect(buf, bufw, bufh, x0 + 1, y0 + 1, size - 2, band, WIN95_TITLE_ACTIVE);
}

struct win95_bufs {
	uint32_t *top_left, *top, *top_right, *left, *right;
	uint32_t *bottom_left, *bottom, *bottom_right;
};

static uint32_t *
alloc_buf(int w, int h)
{
	if (w <= 0 || h <= 0)
		return NULL;
	return calloc((size_t)w * h, sizeof(uint32_t));
}

static void
win95_free(struct win95_bufs *b)
{
	free(b->top_left);
	free(b->top);
	free(b->top_right);
	free(b->left);
	free(b->right);
	free(b->bottom_left);
	free(b->bottom);
	free(b->bottom_right);
}

/* Fills every swc_decor_parts slot for win95: a bevel-framed border (built
 * from tileable 1px-thick edge strips plus four corner blocks, all computed
 * from purely local coordinates so this never needs the window's actual
 * content geometry) and a top-right title/button block matching the other
 * themes' build_top_right. The top-left block doubles as the border corner
 * and the app-icon slot, and its width becomes the fixed left margin for
 * the title text (see decor_apply). */
static int
win95_build(bool active, bool maximized, int bw, int th,
           struct swc_decor_parts *parts, struct win95_bufs *bufs)
{
	uint32_t title_bg = active ? WIN95_TITLE_ACTIVE : WIN95_TITLE_INACTIVE;
	int btn = BUTTON_W;
	int tr_w = btn * 3 + bw;
	int tr_h = bw + th;
	int icon_pad = 3;
	int tl_w = bw + icon_pad + WIN95_ICON_SIZE + icon_pad;
	int tl_h = bw + th;

	memset(bufs, 0, sizeof(*bufs));
	memset(parts, 0, sizeof(*parts));

	if (bw > 0) {
		bufs->top = alloc_buf(1, tr_h);
		for (int y = 0; bufs->top && y < tr_h; y++)
			bufs->top[y] = win95_top_row(y, bw, th, title_bg);
		parts->top.width = 1;
		parts->top.height = (uint32_t)tr_h;
		parts->top.stride = 4;
		parts->top.data = bufs->top;

		bufs->bottom = alloc_buf(1, bw);
		for (int y = 0; bufs->bottom && y < bw; y++)
			bufs->bottom[y] = win95_bottom_row(y, bw);
		parts->bottom.width = 1;
		parts->bottom.height = (uint32_t)bw;
		parts->bottom.stride = 4;
		parts->bottom.data = bufs->bottom;

		bufs->left = alloc_buf(bw, 1);
		for (int x = 0; bufs->left && x < bw; x++)
			bufs->left[x] = win95_left_col(x, bw);
		parts->left.width = (uint32_t)bw;
		parts->left.height = 1;
		parts->left.stride = (uint32_t)bw * 4;
		parts->left.data = bufs->left;

		bufs->right = alloc_buf(bw, 1);
		for (int x = 0; bufs->right && x < bw; x++)
			bufs->right[x] = win95_right_col(x, bw);
		parts->right.width = (uint32_t)bw;
		parts->right.height = 1;
		parts->right.stride = (uint32_t)bw * 4;
		parts->right.data = bufs->right;

		bufs->bottom_left = alloc_buf(bw, bw);
		for (int y = 0; bufs->bottom_left && y < bw; y++)
			for (int x = 0; x < bw; x++)
				bufs->bottom_left[y * bw + x] =
				    win95_corner_px(x, y, bw, bw, false, true, bw, 0, 0, false);
		parts->bottom_left.width = (uint32_t)bw;
		parts->bottom_left.height = (uint32_t)bw;
		parts->bottom_left.stride = (uint32_t)bw * 4;
		parts->bottom_left.data = bufs->bottom_left;

		bufs->bottom_right = alloc_buf(bw, bw);
		for (int y = 0; bufs->bottom_right && y < bw; y++)
			for (int x = 0; x < bw; x++)
				bufs->bottom_right[y * bw + x] =
				    win95_corner_px(x, y, bw, bw, true, true, bw, 0, 0, false);
		parts->bottom_right.width = (uint32_t)bw;
		parts->bottom_right.height = (uint32_t)bw;
		parts->bottom_right.stride = (uint32_t)bw * 4;
		parts->bottom_right.data = bufs->bottom_right;
	}

	bufs->top_left = alloc_buf(tl_w, tl_h);
	for (int y = 0; bufs->top_left && y < tl_h; y++)
		for (int x = 0; x < tl_w; x++)
			bufs->top_left[y * tl_w + x] =
			    win95_corner_px(x, y, tl_w, tl_h, false, false, bw, th, title_bg, true);
	if (bufs->top_left && th > 0) {
		int isz = WIN95_ICON_SIZE;
		int ix = bw + icon_pad;
		int iy = bw + (th - isz) / 2;
		if (iy < bw)
			iy = bw;
		draw_win95_appicon(bufs->top_left, tl_w, tl_h, ix, iy, isz);
	}
	parts->top_left.width = (uint32_t)tl_w;
	parts->top_left.height = (uint32_t)tl_h;
	parts->top_left.stride = (uint32_t)tl_w * 4;
	parts->top_left.data = bufs->top_left;

	bufs->top_right = alloc_buf(tr_w, tr_h);
	for (int y = 0; bufs->top_right && y < tr_h; y++)
		for (int x = 0; x < tr_w; x++)
			bufs->top_right[y * tr_w + x] =
			    win95_corner_px(x, y, tr_w, tr_h, true, false, bw, th, title_bg, true);
	if (bufs->top_right && th > 0) {
		for (int i = 0; i < 3; i++) {
			int bx = i * btn;
			int by = bw;
			win95_button_bevel(bufs->top_right, tr_w, tr_h, bx, by, btn, th);
			const uint8_t (*glyph)[WIN95_GLYPH_W];
			if (i == 0)
				glyph = GLYPH95_MIN;
			else if (i == 1)
				glyph = maximized ? GLYPH95_RESTORE : GLYPH95_MAX;
			else
				glyph = GLYPH95_CLOSE;
			int gx0 = bx + (btn - WIN95_GLYPH_W) / 2;
			int gy0 = by + (th - WIN95_GLYPH_H) / 2;
			draw_glyph95(bufs->top_right, tr_w, tr_h, gx0, gy0, glyph, WIN95_DARK2);
		}
	}
	parts->top_right.width = (uint32_t)tr_w;
	parts->top_right.height = (uint32_t)tr_h;
	parts->top_right.stride = (uint32_t)tr_w * 4;
	parts->top_right.data = bufs->top_right;

	return tl_w;
}

/* The top-right corner block: title-bg fill plus, if the theme has buttons,
 * the button glyphs in a row, close outermost (the usual convention).
 * Border and title share one color, so unlike a themed border/frame this
 * needs no separate tiling for the rest of the top edge. decor.color alone
 * covers that uniformly. */
static uint32_t *
build_top_right(const struct sushi_config *cfg, enum theme_id theme, bool active,
                bool has_buttons, bool maximized, int border_width, int title_height,
                int *out_w, int *out_h)
{
	int btn = BUTTON_W;
	int nbuttons = has_buttons ? 3 : 0;
	int w = btn * nbuttons + border_width;
	int h = border_width + title_height;
	if (w <= 0 || h <= 0)
		return NULL;

	uint32_t *buf = calloc((size_t)w * h, sizeof(uint32_t));
	uint32_t title_bg = active ? cfg->border_color_active : cfg->border_color_inactive;
	uint32_t title_fg = active ? cfg->text_color_active : cfg->text_color_inactive;

	fill_rect(buf, w, h, 0, 0, w, h, title_bg);

	/* swc vertically centers the title text within the *whole* top decor
	 * band (border + title, i.e. this tile's full height h), not just the
	 * title_height portion below the border. Center the glyphs the same
	 * way so their vertical middle lines up with the text's. */
	for (int i = 0; i < nbuttons; i++) {
		int bx = i * btn;
		const uint8_t (*glyph)[GLYPH_W];
		if (theme == THEME_LOVE)
			glyph = GLYPH_HEART;
		else if (i == 0)
			glyph = GLYPH_MIN;
		else if (i == 1)
			glyph = maximized ? GLYPH_RESTORE : GLYPH_MAX;
		else
			glyph = GLYPH_CLOSE;
		int gx0 = bx + (btn - GLYPH_W) / 2;
		int gy0 = (h - GLYPH_H) / 2;
		draw_glyph(buf, w, h, gx0, gy0, glyph, 1, title_fg);
	}

	*out_w = w;
	*out_h = h;
	return buf;
}

void
decor_apply(struct sushi_window *win)
{
	const struct sushi_config *cfg = sushi.config;
	enum theme_id theme = theme_from_name(cfg->theme);
	bool active = win == sushi.focused;
	bool has_buttons = theme_has_buttons(theme);

	/* A fullscreen window has no decoration, and this runs on every focus
	 * and title change: a browser retitling itself as a video plays would
	 * otherwise hand its own decoration back mid-fullscreen. The insets go
	 * with it, so the hit-tests do not reserve a title bar band that is not
	 * on screen. */
	if (win->fullscreen) {
		win->decor_top = 0;
		win->decor_bottom = 0;
		win->decor_left = 0;
		win->decor_right = 0;
		swc_window_set_decor(win->swc, NULL);
		return;
	}

	/* `simple` never shows a title bar, regardless of any per-app_id
	 * "title" rule. The other themes still honor that rule normally. */
	bool show_title = win->decor_title && theme_has_titlebar(theme);

	int border_width = win->decor_border ? cfg->border_width : 0;
	int title_height = show_title ? cfg->title_height : 0;

	win->decor_top = (uint32_t)(border_width + title_height);
	win->decor_bottom = (uint32_t)border_width;
	win->decor_left = (uint32_t)border_width;
	win->decor_right = (uint32_t)border_width;

	if (!show_title && !win->decor_border) {
		swc_window_set_decor(win->swc, NULL);
		return;
	}

	if (theme == THEME_WIN95) {
		struct swc_decor decor = { 0 };
		struct swc_decor_parts parts;
		struct win95_bufs bufs;
		int tl_w = win95_build(active, win->maximized, border_width, title_height,
		                       &parts, &bufs);

		decor.color = WIN95_FACE;
		decor.top = win->decor_top;
		decor.right = win->decor_right;
		decor.bottom = win->decor_bottom;
		decor.left = win->decor_left;
		decor.parts = &parts;

		if (show_title) {
			int tr_w = BUTTON_W * 3 + border_width;
			decor.title.enabled = true;
			decor.title.edge = SWC_DECOR_EDGE_TOP;
			decor.title.align = SWC_DECOR_ALIGN_START;
			decor.title.string = win->title && *win->title ? win->title
			                     : (win->app_id ? win->app_id : "");
			decor.title.color = active ? cfg->text_color_active : cfg->text_color_inactive;
			decor.title.font = cfg->title_font;
			/* Same offset_x trick as the other themes (see below), just with
			 * win95's own left margin (border + icon + padding) standing in
			 * for TITLE_LEFT_MARGIN, since the app icon needs real room. */
			decor.title.padding = (uint32_t)tr_w;
			decor.title.offset_x = tl_w - tr_w;
		}

		swc_window_set_decor(win->swc, &decor);
		win95_free(&bufs);
		return;
	}

	struct swc_decor decor = { 0 };
	decor.color = active ? cfg->border_color_active : cfg->border_color_inactive;
	decor.top = win->decor_top;
	decor.right = win->decor_right;
	decor.bottom = win->decor_bottom;
	decor.left = win->decor_left;

	struct swc_decor_parts parts = { 0 };
	uint32_t *tr_buf = NULL;
	int tr_w = 0, tr_h = 0;

	if (show_title) {
		tr_buf = build_top_right(cfg, theme, active, has_buttons, win->maximized,
		                         border_width, title_height, &tr_w, &tr_h);
		if (tr_buf) {
			parts.top_right.width = (uint32_t)tr_w;
			parts.top_right.height = (uint32_t)tr_h;
			parts.top_right.stride = (uint32_t)tr_w * 4;
			parts.top_right.data = tr_buf;
			decor.parts = &parts;
		}

		decor.title.enabled = true;
		decor.title.edge = SWC_DECOR_EDGE_TOP;
		decor.title.align = SWC_DECOR_ALIGN_START;
		decor.title.string = win->title && *win->title ? win->title
		                     : (win->app_id ? win->app_id : "");
		decor.title.color = active ? cfg->text_color_active : cfg->text_color_inactive;
		decor.title.font = cfg->title_font;
		/* swc only exposes symmetric padding (it insets both the start
		 * position and the truncation width by the same amount), so a
		 * padding wide enough to keep long titles from running under the
		 * buttons would also push the text's start inward on themes with
		 * buttons ("classic") but not on themes without ("flat"). Padding
		 * still needs to be the full button-block width for truncation to
		 * stay safe, but offset_x then shifts the visual start back to a
		 * small fixed margin, so title position doesn't depend on theme. */
		decor.title.padding = (uint32_t)tr_w;
		decor.title.offset_x = TITLE_LEFT_MARGIN - tr_w;
	}

	swc_window_set_decor(win->swc, &decor);

	free(tr_buf);
}

bool
decor_window_contains(struct sushi_window *win, int32_t px, int32_t py)
{
	struct swc_rectangle geom;

	if (!swc_window_get_geometry(win->swc, &geom))
		return false;

	int32_t x0 = geom.x - (int32_t)win->decor_left;
	int32_t y0 = geom.y - (int32_t)win->decor_top;
	int32_t x1 = geom.x + (int32_t)geom.width + (int32_t)win->decor_right;
	int32_t y1 = geom.y + (int32_t)geom.height + (int32_t)win->decor_bottom;

	return px >= x0 && px < x1 && py >= y0 && py < y1;
}

enum sushi_hit_kind
decor_hit_test(struct sushi_window *win, int32_t px, int32_t py)
{
	const struct sushi_config *cfg = sushi.config;
	enum theme_id theme = theme_from_name(cfg->theme);

	if (!win->decor_title || !theme_has_titlebar(theme))
		return HIT_NONE;

	struct swc_rectangle geom;
	if (!swc_window_get_geometry(win->swc, &geom))
		return HIT_NONE;

	int border_width = win->decor_border ? cfg->border_width : 0;
	int title_height = cfg->title_height;

	int32_t outer_x = geom.x - (int32_t)win->decor_left;
	int32_t outer_y = geom.y - (int32_t)win->decor_top;
	uint32_t outer_w = geom.width + win->decor_left + win->decor_right;

	int32_t title_y0 = outer_y + border_width;
	int32_t title_y1 = title_y0 + title_height;
	if (py < title_y0 || py >= title_y1)
		return HIT_NONE;
	if (px < outer_x || px >= outer_x + (int32_t)outer_w)
		return HIT_NONE;

	if (theme_has_buttons(theme)) {
		int btn = BUTTON_W;
		int32_t tr_w = btn * 3 + border_width;
		int32_t block_x0 = outer_x + (int32_t)outer_w - tr_w;
		int32_t block_x1 = block_x0 + btn * 3;

		if (px >= block_x0 && px < block_x1) {
			int idx = (int)((px - block_x0) / btn);
			if (idx == 0)
				return HIT_MINIMIZE;
			if (idx == 1)
				return HIT_MAXIMIZE;
			return HIT_CLOSE;
		}
	}

	return HIT_TITLEBAR;
}
