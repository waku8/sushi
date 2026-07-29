/* sushi: main.c -- entry point, swc glue, input dispatch, hot reload */
#include "sushi.h"
#include "cursor.h"
#include "decor.h"

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <swc.h>

struct sushi_state sushi;

static char *
xstrdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

/* swc_cursor_position() returns wl_fixed_t (24.8 fixed-point) despite the
 * plain int32_t signature -- divide by 256 to get real screen pixels. */
static bool
cursor_position(int32_t *x, int32_t *y)
{
	if (!swc_cursor_position(x, y))
		return false;
	*x /= 256;
	*y /= 256;
	return true;
}

/* ---- spawning ---- */

static void
spawn(char **argv)
{
	if (!argv || !argv[0])
		return;

	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		/* SIGCHLD is SIG_IGN in sushi (see main()), and that disposition
		 * survives exec(). Reset it here so the spawned program (e.g. a
		 * terminal reaping its own shell) gets normal SIGCHLD behavior
		 * instead of inheriting ours. */
		signal(SIGCHLD, SIG_DFL);
		execvp(argv[0], argv);
		_exit(EXIT_FAILURE);
	}
}

/* ---- interactive move/resize ----
 *
 * swc_window_begin_move()/begin_resize() don't self-terminate on the
 * triggering button's release -- the WM has to call the matching end_*
 * explicitly, or the window keeps tracking pointer motion indefinitely
 * (as if the button were still held). These wrap begin_* with that
 * bookkeeping; end_move()/end_resize() are safe to call speculatively
 * (no-op if nothing is in progress). */

static struct sushi_window *active_move;
static struct sushi_window *active_resize;

static void
start_move(struct sushi_window *win)
{
	window_raise(win);
	window_focus(win);
	swc_window_begin_move(win->swc);
	active_move = win;
}

static void
end_move(void)
{
	if (active_move) {
		swc_window_end_move(active_move->swc);
		active_move = NULL;
	}
}

static void
start_resize(struct sushi_window *win)
{
	window_raise(win);
	window_focus(win);
	swc_window_begin_resize(win->swc, SWC_WINDOW_EDGE_AUTO);
	active_resize = win;
}

static void
end_resize(void)
{
	if (active_resize) {
		swc_window_end_resize(active_resize->swc);
		active_resize = NULL;
	}
}

/* ---- action dispatch (shared by key/button bindings) ---- */

static void
dispatch_action(struct sushi_binding *b)
{
	struct sushi_window *win = sushi.focused;

	switch (b->action) {
	case ACTION_SPAWN:
		spawn(b->spawn_argv);
		break;
	case ACTION_CLOSE:
		if (win)
			window_close(win);
		break;
	case ACTION_QUIT:
		wl_display_terminate(sushi.display);
		break;
	case ACTION_FULLSCREEN:
		if (win)
			window_toggle_fullscreen(win);
		break;
	case ACTION_MAXIMIZE:
		if (win)
			window_toggle_maximize(win);
		break;
	case ACTION_CENTER:
		if (win)
			window_center(win);
		break;
	case ACTION_WORKSPACE:
		workspace_switch(b->arg);
		break;
	case ACTION_MOVE_TO_WORKSPACE:
		if (win)
			window_move_to_workspace(win, b->arg);
		break;
	case ACTION_MOVE: {
		int32_t x, y;
		struct sushi_window *target =
		    cursor_position(&x, &y) ? window_at(x, y) : NULL;
		if (target)
			start_move(target);
		break;
	}
	case ACTION_RESIZE: {
		int32_t x, y;
		struct sushi_window *target =
		    cursor_position(&x, &y) ? window_at(x, y) : NULL;
		if (target)
			start_resize(target);
		break;
	}
	case ACTION_CYCLE_FOCUS:
		window_cycle_focus();
		break;
	default:
		break;
	}
}

static void
on_key_binding(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	(void)time;
	(void)value;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;
	dispatch_action((struct sushi_binding *)data);
}

static void
on_button_binding(void *data, uint32_t time, uint32_t value, uint32_t state)
{
	(void)time;
	(void)value;
	struct sushi_binding *b = data;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		dispatch_action(b);
		return;
	}

	/* Release: only move/resize bindings need to know about this (to end
	 * the drag); everything else already fired on press. */
	if (b->action == ACTION_MOVE)
		end_move();
	else if (b->action == ACTION_RESIZE)
		end_resize();
}

void
bindings_register(struct sushi_config *cfg)
{
	struct sushi_binding *b;
	wl_list_for_each(b, &cfg->bindings, link) {
		if (b->type == BIND_KEY)
			swc_add_binding(SWC_BINDING_KEY, b->modifiers, b->value, on_key_binding, b);
		else
			swc_add_binding(SWC_BINDING_BUTTON, b->modifiers, b->value,
			                on_button_binding, b);
	}
}

void
bindings_unregister(struct sushi_config *cfg)
{
	struct sushi_binding *b;
	wl_list_for_each(b, &cfg->bindings, link) {
		if (b->type == BIND_KEY)
			swc_remove_binding(SWC_BINDING_KEY, b->modifiers, b->value);
		else
			swc_remove_binding(SWC_BINDING_BUTTON, b->modifiers, b->value);
	}
}

/* ---- global click handling: decoration hit-test, click-to-focus/raise,
 * and transparent forwarding of unhandled clicks to the focused client. ---- */

static bool relay_left, relay_right;

/* Double-click-on-titlebar-to-fullscreen tracking. sushi rebuilds decor on
 * state change rather than per-frame, so there's no mouse-move-based click
 * state machine anywhere else to hook into -- this is the only place a
 * "click" (as opposed to a drag) is ever really recognized. */
#define DOUBLE_CLICK_MS 400

static struct sushi_window *last_titlebar_click_win;
static uint32_t last_titlebar_click_time;

static void
handle_decor_click(struct sushi_window *win, enum sushi_hit_kind hit, uint32_t button,
                   uint32_t time)
{
	switch (hit) {
	case HIT_CLOSE:
		window_close(win);
		break;
	case HIT_MAXIMIZE:
		window_raise(win);
		window_focus(win);
		window_toggle_maximize(win);
		break;
	case HIT_MINIMIZE:
		/* No taskbar/dock exists to restore a truly-hidden window, so
		 * "minimize" just sends it to the back instead of losing it. */
		window_raise(win);
		swc_window_stack(win->swc, 1);
		break;
	case HIT_TITLEBAR:
		if (button == BTN_LEFT) {
			bool double_click = win == last_titlebar_click_win &&
			                    time - last_titlebar_click_time <= DOUBLE_CLICK_MS;
			last_titlebar_click_win = win;
			last_titlebar_click_time = time;
			if (double_click) {
				last_titlebar_click_win = NULL;
				window_raise(win);
				window_focus(win);
				window_toggle_fullscreen(win);
			} else {
				start_move(win);
			}
		} else {
			window_raise(win);
			window_focus(win);
		}
		break;
	default:
		break;
	}
}

static void
on_click(void *data, uint32_t time, uint32_t button, uint32_t state)
{
	(void)data;
	bool *relay = button == BTN_LEFT ? &relay_left : &relay_right;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		if (button == BTN_LEFT)
			end_move();
		else
			end_resize();
		if (*relay)
			swc_pointer_send_button(time, button, state);
		*relay = false;
		return;
	}

	int32_t x, y;
	if (!cursor_position(&x, &y)) {
		*relay = true;
		swc_pointer_send_button(time, button, state);
		return;
	}

	struct sushi_window *win;
	wl_list_for_each(win, &sushi.windows, link) {
		if (win->workspace != sushi.active_workspace)
			continue;
		enum sushi_hit_kind hit = decor_hit_test(win, x, y);
		if (hit != HIT_NONE) {
			*relay = false;
			handle_decor_click(win, hit, button, time);
			return;
		}
	}

	win = window_at(x, y);
	if (win && win != sushi.focused) {
		window_raise(win);
		window_focus(win);
	}

	*relay = true;
	swc_pointer_send_button(time, button, state);
}

/* ---- window lifecycle ---- */

static void
window_destroy_cb(void *data)
{
	struct sushi_window *win = data;
	bool was_focused = sushi.focused == win;

	if (win->reveal_timer)
		wl_event_source_remove(win->reveal_timer);
	if (last_titlebar_click_win == win)
		last_titlebar_click_win = NULL;

	wl_list_remove(&win->link);

	if (was_focused) {
		sushi.focused = NULL;
		struct sushi_window *next = NULL, *w;
		wl_list_for_each(w, &sushi.windows, link) {
			if (w->workspace == sushi.active_workspace) {
				next = w;
				break;
			}
		}
		if (next)
			window_raise(next);
		window_focus(next);
	}

	free(win->app_id);
	free(win->title);
	free(win);
}

static void
window_title_changed_cb(void *data)
{
	struct sushi_window *win = data;
	free(win->title);
	win->title = xstrdup(win->swc->title);
	if (win->mapped) {
		decor_apply(win);
		window_maybe_reveal(win);
	}
}

static void
window_app_id_changed_cb(void *data)
{
	struct sushi_window *win = data;
	free(win->app_id);
	win->app_id = xstrdup(win->swc->app_id);
	if (win->mapped) {
		window_refresh_decor(win);
		window_maybe_reveal(win);
	}
}

/* Fired when a client wants an interactive move/resize but isn't in stacked
 * mode. sushi puts every window in stacked mode immediately on creation, so
 * this is defensive/should be unreachable in practice. */
static void
window_move_cb(void *data)
{
	struct sushi_window *win = data;
	swc_window_set_stacked(win->swc);
	start_move(win);
}

static void
window_resize_cb(void *data)
{
	struct sushi_window *win = data;
	swc_window_set_stacked(win->swc);
	start_resize(win);
}

static const struct swc_window_handler window_handler = {
	.destroy = window_destroy_cb,
	.title_changed = window_title_changed_cb,
	.app_id_changed = window_app_id_changed_cb,
	.move = window_move_cb,
	.resize = window_resize_cb,
};

static void
new_window(struct swc_window *swc)
{
	struct sushi_window *win = calloc(1, sizeof(*win));
	win->swc = swc;
	win->app_id = xstrdup(swc->app_id);
	win->title = xstrdup(swc->title);

	swc_window_set_handler(swc, &window_handler, win);
	wl_list_insert(&sushi.windows, &win->link);

	window_place_new(win);
}

/* ---- screens ---- */

static void
screen_entered_cb(void *data)
{
	struct sushi_output *out = data;
	sushi.active_screen = out->swc;
}

static const struct swc_screen_handler screen_handler = {
	.entered = screen_entered_cb,
};

static void
new_screen(struct swc_screen *swc)
{
	struct sushi_output *out = calloc(1, sizeof(*out));
	out->swc = swc;
	wl_list_insert(&sushi.outputs, &out->link);
	swc_screen_set_handler(swc, &screen_handler, out);

	if (!sushi.active_screen)
		sushi.active_screen = swc;
}

static const struct swc_manager manager = {
	.new_screen = new_screen,
	.new_window = new_window,
};

/* ---- config hot reload ---- */

static void
reload_config(void)
{
	struct sushi_config *old = sushi.config;
	struct sushi_config *cfg = config_load(sushi.config_path);

	bindings_unregister(old);
	sushi.config = cfg;
	bindings_register(cfg);
	config_free(old);
	cursor_apply(cfg);

	struct sushi_window *win;
	wl_list_for_each(win, &sushi.windows, link)
		window_refresh_decor(win);

	fprintf(stderr, "sushi: config reloaded (%s)\n", sushi.config_path);
}

static int
on_inotify_readable(int fd, uint32_t mask, void *data)
{
	(void)mask;
	(void)data;

	char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t len = read(fd, buf, sizeof(buf));
	if (len <= 0)
		return 0;

	const char *base = strrchr(sushi.config_path, '/');
	base = base ? base + 1 : sushi.config_path;

	bool relevant = false;
	for (char *p = buf; p < buf + len;) {
		struct inotify_event *ev = (struct inotify_event *)p;
		if (ev->len && !strcmp(ev->name, base))
			relevant = true;
		p += sizeof(struct inotify_event) + ev->len;
	}

	if (relevant)
		reload_config();

	return 0;
}

static void
setup_config_watch(void)
{
	int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0) {
		perror("sushi: inotify_init1");
		return;
	}

	char *dir = xstrdup(sushi.config_path);
	char *slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';

	int wd = inotify_add_watch(fd, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE);
	free(dir);

	if (wd < 0) {
		perror("sushi: inotify_add_watch");
		close(fd);
		return;
	}

	wl_event_loop_add_fd(sushi.event_loop, fd, WL_EVENT_READABLE, on_inotify_readable, NULL);
}

/* ---- entry point ---- */

int
main(void)
{
	signal(SIGCHLD, SIG_IGN);
	/* Without this, a client dying mid-write (e.g. the terminal exiting
	 * while sushi is still flushing its wl_display) delivers SIGPIPE on
	 * the next write to that now-closed socket, whose default
	 * disposition is to kill the whole process -- taking sushi itself
	 * down along with the one client. libwayland-server already reports
	 * the broken pipe as a normal error; we just need to not die from it. */
	signal(SIGPIPE, SIG_IGN);

	wl_list_init(&sushi.outputs);
	wl_list_init(&sushi.windows);
	sushi.active_workspace = 1;

	sushi.display = wl_display_create();
	if (!sushi.display) {
		fprintf(stderr, "sushi: failed to create wl_display\n");
		return EXIT_FAILURE;
	}

	const char *socket = wl_display_add_socket_auto(sushi.display);
	if (!socket) {
		fprintf(stderr, "sushi: failed to add socket\n");
		return EXIT_FAILURE;
	}
	setenv("WAYLAND_DISPLAY", socket, 1);

	sushi.event_loop = wl_display_get_event_loop(sushi.display);

	if (!swc_initialize(sushi.display, sushi.event_loop, &manager)) {
		fprintf(stderr, "sushi: swc_initialize failed\n");
		return EXIT_FAILURE;
	}

	swc_add_binding(SWC_BINDING_BUTTON, 0, BTN_LEFT, on_click, NULL);
	swc_add_binding(SWC_BINDING_BUTTON, 0, BTN_RIGHT, on_click, NULL);

	sushi.config_path = config_default_path();
	sushi.config = config_load(sushi.config_path);
	bindings_register(sushi.config);
	cursor_apply(sushi.config);
	setup_config_watch();

	fprintf(stderr, "sushi: running on %s (config: %s)\n", socket, sushi.config_path);

	wl_display_run(sushi.display);

	swc_finalize();
	wl_display_destroy(sushi.display);

	return EXIT_SUCCESS;
}
