/* sushi: window.c -- floating window management, workspaces, focus */
#include "sushi.h"
#include "decor.h"

#include <stdlib.h>
#include <string.h>

#include <swc.h>

static int next_seq = 1;

struct sushi_window *
window_from_swc(struct swc_window *swc)
{
	struct sushi_window *win;
	wl_list_for_each(win, &sushi.windows, link) {
		if (win->swc == swc)
			return win;
	}
	return NULL;
}

void
window_apply_rule(struct sushi_window *win)
{
	win->decor_title = true;
	win->decor_border = true;
	win->workspace = sushi.active_workspace;

	const struct sushi_rule *r = config_match_rule(sushi.config, win->app_id);
	if (!r)
		return;

	if (r->has_title)
		win->decor_title = r->title;
	if (r->has_border)
		win->decor_border = r->border;
	if (r->has_workspace)
		win->workspace = r->workspace;
}

void
window_refresh_decor(struct sushi_window *win)
{
	win->decor_title = true;
	win->decor_border = true;

	const struct sushi_rule *r = config_match_rule(sushi.config, win->app_id);
	if (r) {
		if (r->has_title)
			win->decor_title = r->title;
		if (r->has_border)
			win->decor_border = r->border;
	}

	decor_apply(win);
}

/* Used only when the real screen/window geometry isn't known yet (see
 * below) -- just enough to avoid parking a window at some arbitrary corner
 * while we wait for the real numbers. */
#define FALLBACK_SCREEN_W 1920
#define FALLBACK_SCREEN_H 1080
#define FALLBACK_WINDOW_W 640
#define FALLBACK_WINDOW_H 480

void
window_center(struct sushi_window *win)
{
	struct swc_rectangle usable;
	if (sushi.active_screen && sushi.active_screen->usable_geometry.width &&
	    sushi.active_screen->usable_geometry.height) {
		usable = sushi.active_screen->usable_geometry;
	} else if (sushi.active_screen && sushi.active_screen->geometry.width &&
	          sushi.active_screen->geometry.height) {
		/* usable_geometry not computed yet (e.g. no panels have reserved
		 * space so far) -- the full screen geometry is still accurate. */
		usable = sushi.active_screen->geometry;
	} else {
		usable.x = usable.y = 0;
		usable.width = FALLBACK_SCREEN_W;
		usable.height = FALLBACK_SCREEN_H;
	}

	struct swc_rectangle geom;
	bool have_geom = swc_window_get_geometry(win->swc, &geom) && geom.width && geom.height;
	if (!have_geom) {
		/* The client hasn't committed real content yet, so its final size
		 * is unknown. Center using a guessed size now rather than leaving
		 * the window wherever swc defaults it -- window_place_new() and
		 * the title/app_id-changed callbacks retry this once the real
		 * geometry shows up (see win->positioned). */
		geom.width = FALLBACK_WINDOW_W;
		geom.height = FALLBACK_WINDOW_H;
	}

	uint32_t outer_w = geom.width + win->decor_left + win->decor_right;
	uint32_t outer_h = geom.height + win->decor_top + win->decor_bottom;

	int32_t outer_x = usable.x + ((int32_t)usable.width - (int32_t)outer_w) / 2;
	int32_t outer_y = usable.y + ((int32_t)usable.height - (int32_t)outer_h) / 2;

	swc_window_set_position(win->swc, outer_x + (int32_t)win->decor_left,
	                        outer_y + (int32_t)win->decor_top);

	win->positioned = have_geom;
}

void
window_raise(struct sushi_window *win)
{
	/* swc_window_stack() only moves a window one step per call, not all
	 * the way to the front -- there's no "raise to top" in its API, so
	 * step it up once per other window, an upper bound on how many are
	 * ever stacked above it. */
	int steps = 0;
	struct sushi_window *w;
	wl_list_for_each(w, &sushi.windows, link)
		steps++;

	for (int i = 0; i < steps; i++)
		swc_window_stack(win->swc, -1);

	wl_list_remove(&win->link);
	wl_list_insert(&sushi.windows, &win->link);
}

void
window_focus(struct sushi_window *win)
{
	struct sushi_window *prev = sushi.focused;

	if (prev == win)
		return;

	sushi.focused = win;
	swc_window_focus(win ? win->swc : NULL);

	if (prev)
		decor_apply(prev);
	if (win)
		decor_apply(win);
}

void
window_close(struct sushi_window *win)
{
	swc_window_close(win->swc);
}

void
window_toggle_fullscreen(struct sushi_window *win)
{
	if (win->maximized)
		window_toggle_maximize(win);

	if (win->fullscreen) {
		win->fullscreen = false;
		swc_window_set_stacked(win->swc);
		if (win->has_saved_geom)
			swc_window_set_geometry(win->swc, &win->saved_geom);
		decor_apply(win);
	} else {
		if (swc_window_get_geometry(win->swc, &win->saved_geom))
			win->has_saved_geom = true;
		win->fullscreen = true;
		swc_window_set_decor(win->swc, NULL);
		swc_window_set_fullscreen(win->swc, sushi.active_screen);
	}
}

void
window_toggle_maximize(struct sushi_window *win)
{
	if (win->fullscreen)
		return;
	if (!sushi.active_screen)
		return;

	if (win->maximized) {
		win->maximized = false;
		if (win->has_saved_geom)
			swc_window_set_geometry(win->swc, &win->saved_geom);
		decor_apply(win);
		return;
	}

	if (swc_window_get_geometry(win->swc, &win->saved_geom))
		win->has_saved_geom = true;

	const struct swc_rectangle *usable = &sushi.active_screen->usable_geometry;
	struct swc_rectangle target = {
		.x = usable->x + (int32_t)win->decor_left,
		.y = usable->y + (int32_t)win->decor_top,
		.width = usable->width - win->decor_left - win->decor_right,
		.height = usable->height - win->decor_top - win->decor_bottom,
	};

	win->maximized = true;
	swc_window_set_geometry(win->swc, &target);
	decor_apply(win);
}

void
window_set_workspace(struct sushi_window *win, int workspace)
{
	win->workspace = workspace;
	if (workspace == sushi.active_workspace) {
		/* A window placed on a background workspace may have been
		 * revealed (title/app_id-changed centering retries stopped)
		 * before its real geometry ever showed up, since it was never
		 * actually visible for those retries to matter until now. */
		if (!win->positioned)
			window_center(win);
		swc_window_show(win->swc);
	} else {
		swc_window_hide(win->swc);
	}
}

void
window_move_to_workspace(struct sushi_window *win, int workspace)
{
	bool was_focused = sushi.focused == win;

	window_set_workspace(win, workspace);

	if (was_focused && workspace != sushi.active_workspace) {
		struct sushi_window *next = NULL, *w;
		wl_list_for_each(w, &sushi.windows, link) {
			if (w != win && w->workspace == sushi.active_workspace) {
				next = w;
				break;
			}
		}
		window_focus(next);
		if (next)
			window_raise(next);
	}
}

void
workspace_switch(int workspace)
{
	if (workspace == sushi.active_workspace)
		return;

	sushi.active_workspace = workspace;

	struct sushi_window *w, *first_visible = NULL;
	wl_list_for_each(w, &sushi.windows, link) {
		if (w->workspace == workspace) {
			if (!w->positioned)
				window_center(w);
			swc_window_show(w->swc);
			if (!first_visible)
				first_visible = w;
		} else {
			swc_window_hide(w->swc);
		}
	}

	window_focus(first_visible);
}

struct sushi_window *
window_at(int32_t x, int32_t y)
{
	struct swc_window *swc = swc_window_at(x, y);
	return swc ? window_from_swc(swc) : NULL;
}

/* swc has no "client committed its first real buffer" callback, so
 * window_place_new() can't just wait for a specific event -- it polls
 * window_center() instead. REVEAL_POLL_MS is how often it retries; most
 * clients have real geometry within a poll or two of mapping, so this
 * keeps the common case fast. REVEAL_GRACE_MS is the total time to poll
 * before giving up and revealing the window at its guessed position
 * anyway, for a client that never commits in time. */
#define REVEAL_POLL_MS 10
#define REVEAL_GRACE_MS 150

static void
do_reveal(struct sushi_window *win)
{
	if (win->reveal_timer) {
		wl_event_source_remove(win->reveal_timer);
		win->reveal_timer = NULL;
	}

	win->revealed = true;

	if (win->workspace == sushi.active_workspace) {
		swc_window_show(win->swc);
		window_focus(win);
	}
}

static int
on_reveal_timeout(void *data)
{
	struct sushi_window *win = data;

	window_maybe_reveal(win);
	if (win->revealed)
		return 0; /* do_reveal() already tore down reveal_timer */

	win->reveal_elapsed_ms += REVEAL_POLL_MS;
	if (win->reveal_elapsed_ms >= REVEAL_GRACE_MS) {
		win->reveal_timer = NULL;
		window_center(win); /* last best-effort attempt, even if still a guess */
		do_reveal(win);
		return 0;
	}

	wl_event_source_timer_update(win->reveal_timer, REVEAL_POLL_MS);
	return 0;
}

void
window_maybe_reveal(struct sushi_window *win)
{
	if (win->revealed)
		return;

	window_center(win);
	if (win->positioned)
		do_reveal(win);
}

void
window_place_new(struct sushi_window *win)
{
	win->seq = next_seq++;

	swc_window_set_stacked(win->swc);

	window_apply_rule(win);
	decor_apply(win);

	win->mapped = true;

	window_maybe_reveal(win);
	if (!win->revealed) {
		win->reveal_timer =
		    wl_event_loop_add_timer(sushi.event_loop, on_reveal_timeout, win);
		wl_event_source_timer_update(win->reveal_timer, REVEAL_POLL_MS);
	}
}

void
window_cycle_focus(void)
{
	struct sushi_window *w, *lowest = NULL, *next = NULL;

	wl_list_for_each(w, &sushi.windows, link) {
		if (w->workspace != sushi.active_workspace)
			continue;
		if (!lowest || w->seq < lowest->seq)
			lowest = w;
		if (sushi.focused && w->seq > sushi.focused->seq && (!next || w->seq < next->seq))
			next = w;
	}

	if (!next)
		next = lowest;
	if (!next || next == sushi.focused)
		return;

	window_raise(next);
	window_focus(next);
}
