/* sushi: sushi.h -- shared state */
#ifndef SUSHI_H
#define SUSHI_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>
#include <swc.h>

#include "config.h"

struct sushi_output {
	struct wl_list link;
	struct swc_screen *swc;
};

enum sushi_hit_kind {
	HIT_NONE = 0,
	HIT_TITLEBAR,
	HIT_CLOSE,
	HIT_MAXIMIZE,
	HIT_MINIMIZE,
};

struct sushi_window {
	struct wl_list link;
	struct swc_window *swc;

	char *app_id;
	char *title;

	int workspace;
	bool mapped; /* has the window ever been placed/shown */
	bool fullscreen;
	bool maximized;

	/* Set once window_center() has run against the client's real
	 * geometry (as opposed to a guessed fallback size because the client
	 * hadn't committed real content yet). */
	bool positioned;

	/* Set once the window has gone through its one-time initial
	 * show/focus. Kept false (and the window kept hidden) until either
	 * `positioned` becomes true or reveal_timer's grace period expires,
	 * so a freshly opened window is never shown at a guessed position
	 * and then visibly snapped to its real centered one. */
	bool revealed;
	struct wl_event_source *reveal_timer;
	int reveal_elapsed_ms; /* how long reveal_timer has been polling so far */

	/* Stable creation-order index, used only for alt-tab cycling so
	 * repeated taps walk every window predictably instead of just
	 * toggling the top two (which is what happens if cycling followed
	 * raise/focus order, since raising a window changes the very order
	 * you're cycling through). */
	int seq;

	bool decor_title;
	bool decor_border;

	/* geometry saved before maximize/fullscreen, to restore on toggle-off */
	struct swc_rectangle saved_geom;
	bool has_saved_geom;

	/* cached outward decor insets, kept in sync by decor_apply() */
	uint32_t decor_top, decor_right, decor_bottom, decor_left;
};

struct sushi_state {
	struct wl_display *display;
	struct wl_event_loop *event_loop;

	struct sushi_config *config;
	char *config_path;

	struct wl_list outputs; /* sushi_output::link */
	struct wl_list windows; /* sushi_window::link, front-to-back stacking */

	struct sushi_window *focused;
	struct swc_screen *active_screen;
	int active_workspace;
};

extern struct sushi_state sushi;

/* window.c */
void window_place_new(struct sushi_window *win);
void window_center(struct sushi_window *win);
void window_focus(struct sushi_window *win);
void window_raise(struct sushi_window *win);
void window_close(struct sushi_window *win);
void window_toggle_fullscreen(struct sushi_window *win);
void window_toggle_maximize(struct sushi_window *win);
void window_set_workspace(struct sushi_window *win, int workspace);
void window_move_to_workspace(struct sushi_window *win, int workspace);
void workspace_switch(int workspace);
struct sushi_window *window_at(int32_t x, int32_t y);
struct sushi_window *window_from_swc(struct swc_window *swc);
void window_apply_rule(struct sushi_window *win);
void window_refresh_decor(struct sushi_window *win);
void window_cycle_focus(void);

/* Tries to center against real geometry and, the first time that succeeds
 * (or once the grace-period timer from window_place_new() times out),
 * performs the window's one-time initial show+focus. Safe to call
 * repeatedly (e.g. from title_changed/app_id_changed) -- a no-op once
 * already revealed. */
void window_maybe_reveal(struct sushi_window *win);

/* decor.c */
void decor_apply(struct sushi_window *win);
enum sushi_hit_kind decor_hit_test(struct sushi_window *win, int32_t x, int32_t y);

/* config hot reload, called from main after (re)loading config */
void bindings_register(struct sushi_config *cfg);
void bindings_unregister(struct sushi_config *cfg);

#endif
