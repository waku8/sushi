/* sushi: main.c -- entry point, swc glue, input dispatch, hot reload */
#include "sushi.h"
#include "cursor.h"
#include "decor.h"
#include "input.h"

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <ucontext.h>
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

/* Launches everything the config listed under `autostart`. Called once from
 * main() and deliberately not from reload_config(): saving the config file
 * should not spawn a second copy of every program in it. */
static void
autostart_spawn(const struct sushi_config *cfg)
{
	struct sushi_autostart *a;

	wl_list_for_each(a, &cfg->autostart, link)
		spawn(a->spawn_argv);
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

	/* sushi.windows is kept in stacking order (window_raise() moves to the
	 * head), so the first window covering the point is the one the user can
	 * actually see there. Stopping at it matters: decor_hit_test() only
	 * answers "is this my decoration", so walking past a window whose
	 * *content* is under the pointer would let a window buried behind it
	 * claim the click purely because its title bar band happens to line up
	 * -- stealing focus, or toggling fullscreen on a double click, on a
	 * window the user cannot even see. */
	struct sushi_window *win;
	wl_list_for_each(win, &sushi.windows, link) {
		if (win->workspace != sushi.active_workspace)
			continue;
		if (!win->mapped || !win->revealed)
			continue;
		if (!decor_window_contains(win, x, y))
			continue;

		enum sushi_hit_kind hit = decor_hit_test(win, x, y);
		if (hit != HIT_NONE) {
			*relay = false;
			handle_decor_click(win, hit, button, time);
			return;
		}
		break;
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

	/* A window can die mid-drag (the client just exits). Dropping these
	 * without clearing them leaves end_move()/end_resize() to dereference
	 * the freed sushi_window on the next button release. */
	if (active_move == win)
		active_move = NULL;
	if (active_resize == win)
		active_resize = NULL;

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
	.new_device = input_device_added,
};

/* ---- config hot reload ---- */

/* Leaving a field out of the config means the environment still decides, so
 * only the ones the user actually set are overridden. */
static void
keyboard_setenv(const char *var, const char *value)
{
	if (value)
		setenv(var, value, 1);
}

/* Pushes the config's keyboard settings into swc. Must run before
 * swc_initialize(): swc builds its keymap from libxkbcommon's defaults when
 * it creates the seat, which means from the XKB_DEFAULT_* variables, and
 * hands clients the repeat rate and delay when they bind their keyboard.
 * Neither can be changed afterwards, so a reload cannot pick these up.
 * keyboard_changed() below says so instead. */
static void
keyboard_apply(const struct sushi_config *cfg)
{
	swc_repeat_rate = cfg->repeat_rate;
	swc_repeat_delay = cfg->repeat_delay;

	if (!cfg->kb_rules && !cfg->kb_model && !cfg->kb_layout &&
	    !cfg->kb_variant && !cfg->kb_options)
		return;

	/* swc has no way to report a bad layout back to us. It would fail to
	 * build the keymap, fail to create the seat, and take swc_initialize()
	 * down with it, killing the session. So a typo is caught here and the
	 * settings dropped, leaving whatever the environment had selected. */
	if (!config_keymap_builds(cfg)) {
		fprintf(stderr, "sushi: ignoring keyboard layout '%s%s%s': xkbcommon "
		                "cannot build it\n",
		        cfg->kb_layout ? cfg->kb_layout : "",
		        cfg->kb_variant ? " variant " : "",
		        cfg->kb_variant ? cfg->kb_variant : "");
		return;
	}

	keyboard_setenv("XKB_DEFAULT_RULES", cfg->kb_rules);
	keyboard_setenv("XKB_DEFAULT_MODEL", cfg->kb_model);
	keyboard_setenv("XKB_DEFAULT_LAYOUT", cfg->kb_layout);
	keyboard_setenv("XKB_DEFAULT_VARIANT", cfg->kb_variant);
	keyboard_setenv("XKB_DEFAULT_OPTIONS", cfg->kb_options);
}

static bool
streq_null(const char *a, const char *b)
{
	return a && b ? !strcmp(a, b) : a == b;
}

static bool
keyboard_changed(const struct sushi_config *a, const struct sushi_config *b)
{
	return !streq_null(a->kb_rules, b->kb_rules) ||
	       !streq_null(a->kb_model, b->kb_model) ||
	       !streq_null(a->kb_layout, b->kb_layout) ||
	       !streq_null(a->kb_variant, b->kb_variant) ||
	       !streq_null(a->kb_options, b->kb_options) ||
	       a->repeat_rate != b->repeat_rate ||
	       a->repeat_delay != b->repeat_delay;
}

static void
reload_config(void)
{
	struct sushi_config *old = sushi.config;
	struct sushi_config *cfg = config_load(sushi.config_path);

	if (keyboard_changed(old, cfg)) {
		fprintf(stderr, "sushi: keyboard settings changed; restart sushi to "
		                "apply them\n");
	}

	bindings_unregister(old);
	sushi.config = cfg;
	bindings_register(cfg);
	config_free(old);
	cursor_apply(cfg);
	input_apply(cfg);

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

/* ---- crash reporting ----
 *
 * A compositor that dies takes every client with it, and the only thing
 * visible afterwards is the clients complaining about a broken pipe --
 * nothing that says whether sushi faulted or exited on its own. This says
 * which, and where, using only async-signal-safe calls. */

/* Writes a NUL-terminated string to stderr. write() is async-signal-safe;
 * printf() is not, and a handler that deadlocks on stdio's lock prints
 * nothing at all. */
static void
emit(const char *s)
{
	size_t len = 0;

	while (s[len])
		len++;
	(void)!write(STDERR_FILENO, s, len);
}

static void
emit_hex(unsigned long value)
{
	static const char digits[] = "0123456789abcdef";
	char buf[2 + sizeof(value) * 2 + 1];
	size_t i = sizeof(buf) - 1;

	buf[i] = '\0';
	do {
		buf[--i] = digits[value & 0xf];
		value >>= 4;
	} while (value);
	buf[--i] = 'x';
	buf[--i] = '0';

	emit(&buf[i]);
}

/* backtrace_symbols() is a glibc extension that musl does not have, so walk
 * the frame pointer chain by hand (hence -fno-omit-frame-pointer, set for
 * both sushi and swc) and print raw return addresses.
 *
 * Under PIE an address is only meaningful against the load base of whatever
 * object it lands in, and the interesting frames are usually in libswc.so
 * rather than in sushi itself -- so dump every executable mapping and let
 * whoever reads it pick the one bracketing each address:
 *
 *     addr2line -f -p -e <object> $((<addr> - <base of that object>))
 */
static bool
contains(const char *haystack, const char *needle)
{
	for (size_t i = 0; haystack[i]; i++) {
		size_t j = 0;

		while (needle[j] && haystack[i + j] == needle[j])
			j++;
		if (!needle[j])
			return true;
	}

	return false;
}

static void
emit_exec_maps(void)
{
	char buf[1024], line[512];
	size_t linelen = 0;
	ssize_t n;
	int fd;

	fd = open("/proc/self/maps", O_RDONLY);
	if (fd < 0)
		return;

	emit("sushi: executable mappings:\n");

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] != '\n') {
				if (linelen < sizeof(line) - 1)
					line[linelen++] = buf[i];
				continue;
			}

			line[linelen] = '\0';
			if (contains(line, "r-xp")) {
				emit("  ");
				emit(line);
				emit("\n");
			}
			linelen = 0;
		}
	}

	close(fd);
}

static void
emit_backtrace(void)
{
	void **frame = __builtin_frame_address(0);

	emit("sushi: backtrace (return addresses):\n");

	for (int depth = 0; depth < 32; depth++) {
		void **next = (void **)frame[0];
		void *ret = frame[1];

		if (!ret)
			break;

		emit("  ");
		emit_hex((unsigned long)ret);
		emit("\n");

		/* Stacks grow down, so a valid caller frame is always at a higher
		 * address than ours. Anything else means the chain is corrupt (or
		 * we walked into a frameless function) and following it would
		 * fault inside the handler. */
		if (next <= frame || ((unsigned long)next & 0x7))
			break;
		frame = next;
	}
}

static const char *
signal_name(int sig)
{
	switch (sig) {
	case SIGSEGV:
		return "SIGSEGV";
	case SIGBUS:
		return "SIGBUS";
	case SIGABRT:
		return "SIGABRT";
	case SIGFPE:
		return "SIGFPE";
	case SIGILL:
		return "SIGILL";
	default:
		return "signal";
	}
}

/* The frame pointer walk cannot report the innermost frame: a leaf function
 * gets no frame of its own, so the fault lands one level below anything the
 * chain can see. The interrupted instruction pointer is that missing level,
 * and it is the address that actually names the crashing function. */
static void
emit_fault_pc(void *ctx)
{
#if defined(__x86_64__) && defined(REG_RIP)
	ucontext_t *uc = ctx;

	if (!uc)
		return;

	emit("sushi: faulting pc ");
	emit_hex((unsigned long)uc->uc_mcontext.gregs[REG_RIP]);
	emit("\n");
#else
	(void)ctx;
#endif
}

static void
on_fatal_signal(int sig, siginfo_t *info, void *ctx)
{
	emit("\nsushi: fatal ");
	emit(signal_name(sig));
	emit(" at address ");
	emit_hex((unsigned long)(info ? info->si_addr : NULL));
	emit("\n");

	emit_fault_pc(ctx);

	emit_exec_maps();
	emit_backtrace();

	/* Re-raise with the default handler so the kernel still produces a
	 * core dump for whoever wants one. */
	signal(sig, SIG_DFL);
	raise(sig);
}

static void
install_crash_handler(void)
{
	struct sigaction sa = { 0 };
	static const int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };

	sa.sa_sigaction = on_fatal_signal;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);

	for (size_t i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++)
		sigaction(sigs[i], &sa, NULL);
}

/* ---- entry point ---- */

static void
usage(FILE *out, const char *argv0)
{
	fprintf(out,
	        "usage: %s [validate [path]]\n"
	        "\n"
	        "  (no arguments)   run the compositor\n"
	        "  validate [path]  check the config and exit; reports which file\n"
	        "                   would be used, and everything the compositor\n"
	        "                   would otherwise ignore in silence\n"
	        "\n"
	        "Exit status for validate is 0 when the config has no errors.\n",
	        argv0);
}

int
main(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcmp(argv[1], "validate")) {
			/* An explicit path is handy for checking a file before moving it
			 * into place; without one, check the file sushi would load. */
			char *path = argc > 2 ? xstrdup(argv[2]) : config_default_path();
			bool ok = config_validate(path);

			free(path);
			return ok ? EXIT_SUCCESS : EXIT_FAILURE;
		}

		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage(stdout, argv[0]);
			return EXIT_SUCCESS;
		}

		fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], argv[1]);
		usage(stderr, argv[0]);
		return EXIT_FAILURE;
	}

	install_crash_handler();
	signal(SIGCHLD, SIG_IGN);
	/* Without this, a client dying mid-write (e.g. the terminal exiting
	 * while sushi is still flushing its wl_display) delivers SIGPIPE on
	 * the next write to that now-closed socket, whose default
	 * disposition is to kill the whole process -- taking sushi itself
	 * down along with the one client. libwayland-server already reports
	 * the broken pipe as a normal error; we just need to not die from it. */
	signal(SIGPIPE, SIG_IGN);

	wl_list_init(&sushi.outputs);
	wl_list_init(&sushi.devices);
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

	/* Before swc_initialize(), which is where swc reads the keyboard
	 * settings once and for all; see keyboard_apply(). */
	sushi.config_path = config_default_path();
	sushi.config = config_load(sushi.config_path);
	keyboard_apply(sushi.config);

	if (!swc_initialize(sushi.display, sushi.event_loop, &manager)) {
		fprintf(stderr, "sushi: swc_initialize failed\n");
		return EXIT_FAILURE;
	}

	swc_add_binding(SWC_BINDING_BUTTON, 0, BTN_LEFT, on_click, NULL);
	swc_add_binding(SWC_BINDING_BUTTON, 0, BTN_RIGHT, on_click, NULL);

	bindings_register(sushi.config);
	cursor_apply(sushi.config);
	setup_config_watch();

	fprintf(stderr, "sushi: running on %s (config: %s)\n", socket, sushi.config_path);

	/* After the socket is in the environment, so the children find it. */
	autostart_spawn(sushi.config);

	wl_display_run(sushi.display);

	/* Distinguishes an orderly shutdown (this line) from a fault (the
	 * crash handler's line) when only the clients' broken-pipe errors are
	 * left on screen. */
	fprintf(stderr, "sushi: event loop terminated, shutting down\n");

	swc_finalize();
	wl_display_destroy(sushi.display);

	return EXIT_SUCCESS;
}
