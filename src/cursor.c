/* sushi: cursor.c -- Plan 9 "nein" cursor theme
 *
 * Pixel data adapted from Plan 9 cursor bitmaps (whitearrow, box, cross,
 * sight, and the up/down resize glyphs). The original generated form baked
 * two colors (NEIN_IN / NEIN_OUT) in as compile-time macros; here the same
 * shapes are kept as a compact two-tone + transparent pattern (0 =
 * transparent, 1 = "in", 2 = "out") and rendered to ARGB8888 at runtime
 * against sushi.config's cursor_color_in/cursor_color_out, so the colors
 * are a normal (and hot-reloadable) config option instead of a rebuild.
 */
#include "cursor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <swc.h>

enum {
	NEIN_CURSOR_WHITEARROW = 0,
	NEIN_CURSOR_BOXCURSOR = 1,
	NEIN_CURSOR_CROSSCURSOR = 2,
	NEIN_CURSOR_SIGHTCURSOR = 3,
	NEIN_CURSOR_T = 4,
	NEIN_CURSOR_B = 5,
	NEIN_CURSOR_COUNT = 6,
};

/* 0 = transparent, 1 = "in" color, 2 = "out" color */
static const uint8_t nein_cursor_pattern[] = {
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 0, 0, 0, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0, 0, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 0,
	2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2,
	2, 2, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 2, 2, 2, 0,
	2, 2, 1, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 0, 0,
	2, 2, 1, 2, 0, 0, 2, 2, 2, 1, 2, 2, 2, 0, 0, 0,
	2, 2, 2, 2, 0, 0, 0, 2, 2, 2, 2, 2, 0, 0, 0, 0,
	2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0,
	2, 2, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0,
	0, 1, 2, 2, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 1, 0,
	1, 1, 2, 1, 1, 0, 1, 2, 2, 1, 0, 1, 1, 2, 1, 1,
	1, 2, 1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 1, 1, 2, 1,
	1, 2, 1, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 1, 2, 1,
	1, 2, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 2, 1,
	1, 2, 1, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 1, 2, 1,
	1, 2, 1, 1, 0, 0, 1, 2, 2, 1, 0, 0, 1, 1, 2, 1,
	0, 1, 2, 1, 1, 0, 1, 2, 2, 1, 0, 1, 1, 2, 1, 1,
	0, 1, 2, 2, 1, 1, 1, 2, 2, 1, 1, 1, 2, 2, 1, 0,
	0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 0, 0,
	0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 2, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 1, 1, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1,
	1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 1, 1, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0,
	0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0,
	0, 0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 2, 1, 1, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
};

struct nein_cursor_meta {
	enum swc_cursor_kind swc_kind;
	uint32_t width, height;
	int32_t hotspot_x, hotspot_y;
	size_t offset;
};

static const struct nein_cursor_meta nein_cursor_metadata[NEIN_CURSOR_COUNT] = {
	[NEIN_CURSOR_WHITEARROW] = { SWC_CURSOR_DEFAULT, 16, 16, 0, 0, 0 },
	[NEIN_CURSOR_BOXCURSOR] = { SWC_CURSOR_BOX, 16, 16, 7, 7, 256 },
	[NEIN_CURSOR_CROSSCURSOR] = { SWC_CURSOR_CROSS, 16, 16, 7, 7, 512 },
	[NEIN_CURSOR_SIGHTCURSOR] = { SWC_CURSOR_SIGHT, 16, 16, 7, 7, 768 },
	[NEIN_CURSOR_T] = { SWC_CURSOR_UP, 16, 10, 7, 6, 1024 },
	[NEIN_CURSOR_B] = { SWC_CURSOR_DOWN, 16, 10, 7, 3, 1184 },
};

/* swc_set_cursor_image() keeps the pointer, it doesn't copy -- these must
 * stay alive for as long as they're in use, so we hold on to the previous
 * generation only long enough to free it right after it's been replaced. */
static uint32_t *nein_cursor_buffers[NEIN_CURSOR_COUNT];

static uint32_t *
build_cursor_buffer(const struct nein_cursor_meta *meta, uint32_t color_in,
                    uint32_t color_out)
{
	size_t n = (size_t)meta->width * meta->height;
	uint32_t *buf = malloc(n * sizeof(uint32_t));
	if (!buf)
		return NULL;

	const uint8_t *pattern = &nein_cursor_pattern[meta->offset];
	for (size_t i = 0; i < n; i++) {
		switch (pattern[i]) {
		case 1: buf[i] = color_in; break;
		case 2: buf[i] = color_out; break;
		default: buf[i] = 0x00000000; break;
		}
	}

	return buf;
}

void
cursor_apply(const struct sushi_config *cfg)
{
	if (!cfg->cursor_theme) {
		/* swc_set_cursor_mode(CLIENT) only governs whether a client's own
		 * cursor surface is honored *while the pointer is over that
		 * client*; the compositor image last registered via
		 * swc_set_cursor_image() keeps showing wherever there's no client
		 * surface (e.g. bare desktop) regardless of mode. Without this,
		 * disabling the theme after it was ever on (which is the only way
		 * to reach this line after startup, since config reloads are the
		 * only way cursor_theme can change) leaves the nein art showing
		 * there forever. */
		for (int i = 0; i < NEIN_CURSOR_COUNT; i++) {
			swc_clear_cursor_image(nein_cursor_metadata[i].swc_kind);
			free(nein_cursor_buffers[i]);
			nein_cursor_buffers[i] = NULL;
		}
		swc_set_cursor_mode(SWC_CURSOR_MODE_CLIENT);
		return;
	}

	uint32_t *old[NEIN_CURSOR_COUNT];
	for (int i = 0; i < NEIN_CURSOR_COUNT; i++)
		old[i] = nein_cursor_buffers[i];

	for (int i = 0; i < NEIN_CURSOR_COUNT; i++) {
		const struct nein_cursor_meta *meta = &nein_cursor_metadata[i];
		uint32_t *buf =
		    build_cursor_buffer(meta, cfg->cursor_color_in, cfg->cursor_color_out);
		if (!buf)
			continue;

		swc_set_cursor_image(meta->swc_kind, buf, meta->width, meta->height,
		                     meta->hotspot_x, meta->hotspot_y);
		nein_cursor_buffers[i] = buf;
	}

	swc_set_cursor_mode(SWC_CURSOR_MODE_COMPOSITOR);

	for (int i = 0; i < NEIN_CURSOR_COUNT; i++)
		free(old[i]);
}
