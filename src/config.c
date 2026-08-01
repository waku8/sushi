/* config.c */
#include "config.h"

#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <swc.h>

/* Diagnostics.
 *
 * The runtime loader ignores anything it does not understand: a config with a
 * typo in it works, minus the line that was dropped. That is the right
 * behaviour for a compositor mid-session and useless for finding the typo, so
 * the parse helpers report through here instead of dropping quietly.
 *
 * Kept in file scope rather than threaded through every helper, which would
 * touch all of them for one caller. NULL outside config_validate(), so the
 * runtime path stays silent. Parsing is not reentrant. */
struct config_diag {
	const char *path;
	int line;
	int errors;
	int warnings;
};

static struct config_diag *diag;

static void
diag_at(bool error, const char *fmt, ...)
{
	va_list ap;

	if (!diag)
		return;

	if (error)
		diag->errors++;
	else
		diag->warnings++;

	fprintf(stdout, "%s:%d: %s: ", diag->path, diag->line,
	        error ? "error" : "warning");
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fputc('\n', stdout);
}

/* Whether `cmd` can actually be started. Runs on every load, but diag_at()
 * only reports when `diag` is set, so a missing program is silent at runtime
 * and a warning under the validator (it may just not be installed yet). */
static bool
on_path(const char *cmd)
{
	if (strchr(cmd, '/'))
		return access(cmd, X_OK) == 0;

	const char *path = getenv("PATH");
	if (!path)
		return true; /* nothing to check against, so do not cry wolf */

	char *copy = strdup(path), *save = NULL;
	if (!copy)
		return true;
	bool found = false;

	for (char *dir = strtok_r(copy, ":", &save); dir;
	     dir = strtok_r(NULL, ":", &save)) {
		char buf[1024];
		if (snprintf(buf, sizeof(buf), "%s/%s", dir, cmd) >= (int)sizeof(buf))
			continue;
		if (access(buf, X_OK) == 0) {
			found = true;
			break;
		}
	}

	free(copy);
	return found;
}

#define MAX_LINE 512
#define MAX_TOKENS 32

static char *
xstrdup(const char *s)
{
	return s ? strdup(s) : NULL;
}

static bool
truthy(const char *s)
{
	return strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "1") == 0 ||
	       strcmp(s, "on") == 0;
}

/* Colors are configured as plain "rrggbb" hex, always fully opaque, since
 * nothing in sushi's decor ever uses partial transparency. Internally
 * colors still travel around as ARGB8888 (the software rasterizer and
 * swc's decor API both want that), so this just forces the alpha byte on.
 *
 * A leading '#' is accepted too, but only if quoted ("#rrggbb"): an
 * unquoted '#' starts a comment (see tokenize()) that eats the rest of the
 * line, silently dropping the value, so an unquoted "color #rrggbb" line
 * is indistinguishable from writing nothing at all. */
static uint32_t
parse_color(const char *s)
{
	if (*s == '#')
		s++;
	return 0xFF000000u | ((uint32_t)strtoul(s, NULL, 16) & 0x00FFFFFFu);
}

/* Split `line` into whitespace-separated tokens, treating "..." as a single
 * token (quotes stripped, no escape support). A '#' outside quotes starts a
 * comment that runs to the end of the line. Tokens are pointers into a
 * mutated copy of `line` (NUL-terminated in place). The caller must keep
 * `buf` alive as long as the tokens are used. */
static int
tokenize(char *buf, char *tokens[], int max_tokens)
{
	int ntok = 0;
	char *p = buf;
	bool in_quotes = false;

	for (char *c = buf; *c; c++) {
		if (*c == '"')
			in_quotes = !in_quotes;
		else if (*c == '#' && !in_quotes) {
			*c = '\0';
			break;
		}
	}

	while (*p && ntok < max_tokens) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (!*p)
			break;

		if (*p == '"') {
			p++;
			tokens[ntok++] = p;
			while (*p && *p != '"')
				p++;
			if (*p == '"')
				*p++ = '\0';
		} else {
			tokens[ntok++] = p;
			while (*p && !isspace((unsigned char)*p))
				p++;
			if (*p)
				*p++ = '\0';
		}
	}

	return ntok;
}

static bool
parse_mod_name(const char *name, uint32_t *out)
{
	if (!strcmp(name, "logo") || !strcmp(name, "mod4") || !strcmp(name, "super")) {
		*out = SWC_MOD_LOGO;
	} else if (!strcmp(name, "alt") || !strcmp(name, "mod1")) {
		*out = SWC_MOD_ALT;
	} else if (!strcmp(name, "ctrl") || !strcmp(name, "control")) {
		*out = SWC_MOD_CTRL;
	} else if (!strcmp(name, "shift")) {
		*out = SWC_MOD_SHIFT;
	} else {
		return false;
	}
	return true;
}

static void
config_defaults(struct sushi_config *cfg)
{
	cfg->mod = SWC_MOD_LOGO;
	cfg->terminal = xstrdup("foot");
	cfg->launcher = xstrdup("fuzzel");
	cfg->theme = xstrdup("flat");
	cfg->border_width = 4;
	cfg->title_height = 18;
	cfg->text_color_active = 0xFFFFFFFF;
	cfg->text_color_inactive = 0xFF2F2F2F;
	cfg->border_color_active = 0xFF4C7A4C;
	cfg->border_color_inactive = 0xFFA9C4A9;
	/* Keyboard layout fields stay NULL on purpose: that hands the choice to
	 * libxkbcommon, which honors XKB_DEFAULT_LAYOUT and friends. Naming a
	 * default here would silently override whatever the user set outside
	 * sushi. The repeat values match swc's own defaults. */
	cfg->repeat_rate = 40;
	cfg->repeat_delay = 500;
	cfg->cursor_theme = true;
	cfg->cursor_color_in = 0xFFFFFFFF;
	cfg->cursor_color_out = 0xFF000000;
	wl_list_init(&cfg->bindings);
	wl_list_init(&cfg->rules);
	wl_list_init(&cfg->autostart);
	wl_list_init(&cfg->input_rules);
}

static void
binding_free(struct sushi_binding *b)
{
	if (b->spawn_argv) {
		for (int i = 0; b->spawn_argv[i]; i++)
			free(b->spawn_argv[i]);
		free(b->spawn_argv);
	}
	wl_list_remove(&b->link);
	free(b);
}

/* Adds a binding, replacing any existing one with the same (type,
 * modifiers, value) so later config entries (or reloads) override earlier
 * ones instead of stacking. */
static struct sushi_binding *
add_binding(struct sushi_config *cfg, enum sushi_bind_type type, uint32_t mods,
           uint32_t value)
{
	struct sushi_binding *b, *tmp;

	wl_list_for_each_safe(b, tmp, &cfg->bindings, link) {
		if (b->type == type && b->modifiers == mods && b->value == value)
			binding_free(b);
	}

	b = calloc(1, sizeof(*b));
	b->type = type;
	b->modifiers = mods;
	b->value = value;
	wl_list_insert(&cfg->bindings, &b->link);
	return b;
}

/* Parses a key/button combo like "mod+shift+q" or "mod+button1". Returns
 * false on an unrecognized token. */
static bool
parse_combo(struct sushi_config *cfg, char *combo, enum sushi_bind_type *type,
           uint32_t *mods, uint32_t *value)
{
	uint32_t m = 0;
	char *save = NULL;
	char *tok = strtok_r(combo, "+", &save);
	char *last = NULL;

	while (tok) {
		char *next = strtok_r(NULL, "+", &save);
		if (next) {
			uint32_t bit;
			if (!strcmp(tok, "mod")) {
				m |= cfg->mod;
			} else if (parse_mod_name(tok, &bit)) {
				m |= bit;
			} else {
				return false;
			}
		} else {
			last = tok;
		}
		tok = next;
	}

	if (!last)
		return false;

	if (!strncmp(last, "button", 6) && isdigit((unsigned char)last[6])) {
		int n = atoi(last + 6);
		*type = BIND_BUTTON;
		*mods = m;
		switch (n) {
		case 1: *value = BTN_LEFT; break;
		case 2: *value = BTN_MIDDLE; break;
		case 3: *value = BTN_RIGHT; break;
		default: return false;
		}
		return true;
	}

	if (!strcmp(last, "mod")) {
		/* "mod" alone used as the trailing token means the modifier itself
		 * was meant as the key, which isn't supported. Treat as error. */
		return false;
	}

	xkb_keysym_t sym = xkb_keysym_from_name(last, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol)
		return false;

	*type = BIND_KEY;
	*mods = m;
	*value = sym;
	return true;
}

static void
parse_bind_line(struct sushi_config *cfg, char **tok, int ntok)
{
	/* bind <combo> <action> [args...] */
	if (ntok < 3) {
		diag_at(true, "bind needs a combo and an action");
		return;
	}

	enum sushi_bind_type type;
	uint32_t mods, value;
	/* parse_combo() mutates the token, so keep a copy for the message. */
	char combo[128];
	snprintf(combo, sizeof(combo), "%s", tok[1]);
	if (!parse_combo(cfg, tok[1], &type, &mods, &value)) {
		diag_at(true, "cannot parse the combo '%s'", combo);
		return;
	}

	const char *action = tok[2];
	struct sushi_binding *b;

	if (!strcmp(action, "spawn")) {
		int argc = ntok - 3;
		if (argc <= 0) {
			diag_at(true, "bind %s spawn needs a command", combo);
			return;
		}
		if (!on_path(tok[3]))
			diag_at(false, "bind %s spawns '%s', not found on PATH", combo,
			        tok[3]);
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_SPAWN;
		b->spawn_argv = calloc(argc + 1, sizeof(char *));
		for (int i = 0; i < argc; i++)
			b->spawn_argv[i] = xstrdup(tok[3 + i]);
		b->spawn_argv[argc] = NULL;
	} else if (!strcmp(action, "close")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_CLOSE;
	} else if (!strcmp(action, "quit")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_QUIT;
	} else if (!strcmp(action, "fullscreen")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_FULLSCREEN;
	} else if (!strcmp(action, "maximize")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_MAXIMIZE;
	} else if (!strcmp(action, "center")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_CENTER;
	} else if (!strcmp(action, "move")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_MOVE;
	} else if (!strcmp(action, "resize")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_RESIZE;
	} else if (!strcmp(action, "cycle-focus")) {
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_CYCLE_FOCUS;
	} else if (!strcmp(action, "workspace") ||
	           !strcmp(action, "move-to-workspace")) {
		if (ntok < 4) {
			diag_at(true, "bind %s %s needs a workspace number", combo, action);
			return;
		}
		b = add_binding(cfg, type, mods, value);
		b->action = !strcmp(action, "workspace") ? ACTION_WORKSPACE
		                                        : ACTION_MOVE_TO_WORKSPACE;
		b->arg = atoi(tok[3]);
		if (b->arg < 1 || b->arg > 10)
			diag_at(true, "workspace %d is outside 1..10", b->arg);
	} else {
		diag_at(true, "unknown action '%s'", action);
	}
}

static struct sushi_rule *
rule_find_or_create(struct sushi_config *cfg, const char *pattern)
{
	struct sushi_rule *r;
	wl_list_for_each(r, &cfg->rules, link) {
		if (!strcmp(r->pattern, pattern))
			return r;
	}
	r = calloc(1, sizeof(*r));
	r->pattern = xstrdup(pattern);
	wl_list_insert(cfg->rules.prev, &r->link);
	return r;
}

static void
parse_rule_line(struct sushi_rule *r, char **tok, int ntok)
{
	if (ntok < 2) {
		diag_at(true, "'%s' inside a window block needs a value", tok[0]);
		return;
	}

	if (!strcmp(tok[0], "workspace")) {
		r->has_workspace = true;
		r->workspace = atoi(tok[1]);
		if (r->workspace < 1 || r->workspace > 10)
			diag_at(true, "workspace %d is outside 1..10", r->workspace);
	} else if (!strcmp(tok[0], "title")) {
		r->has_title = true;
		r->title = truthy(tok[1]);
	} else if (!strcmp(tok[0], "border")) {
		r->has_border = true;
		r->border = truthy(tok[1]);
	} else {
		diag_at(false, "unknown window setting '%s'", tok[0]);
	}
}

/* Applies a top-level "key value" line: appearance, keyboard, and cursor
 * settings, i.e. anything that isn't `bind`, `window`, `input`, or
 * `autostart`. No-op for anything else, so it's safe to call while skipping
 * over bind/window lines. */
static void
parse_global_line(struct sushi_config *cfg, char **tok, int ntok)
{
	if (ntok < 2) {
		diag_at(true, "'%s' needs a value", tok[0]);
		return;
	}

	if (!strcmp(tok[0], "mod")) {
		uint32_t m;
		if (parse_mod_name(tok[1], &m))
			cfg->mod = m;
		else
			diag_at(true, "mod '%s' is not logo, alt, ctrl or shift", tok[1]);
	} else if (!strcmp(tok[0], "terminal")) {
		free(cfg->terminal);
		cfg->terminal = xstrdup(tok[1]);
		if (!on_path(tok[1]))
			diag_at(false, "terminal '%s' not found on PATH", tok[1]);
	} else if (!strcmp(tok[0], "launcher")) {
		free(cfg->launcher);
		cfg->launcher = xstrdup(tok[1]);
		if (!on_path(tok[1]))
			diag_at(false, "launcher '%s' not found on PATH", tok[1]);
	} else if (!strcmp(tok[0], "theme")) {
		free(cfg->theme);
		cfg->theme = xstrdup(tok[1]);
		if (strcmp(tok[1], "flat") && strcmp(tok[1], "classic") &&
		    strcmp(tok[1], "simple") && strcmp(tok[1], "love") &&
		    strcmp(tok[1], "win95"))
			diag_at(true, "unknown theme '%s'; falling back to flat", tok[1]);
	} else if (!strcmp(tok[0], "border-width")) {
		cfg->border_width = atoi(tok[1]);
	} else if (!strcmp(tok[0], "title-height")) {
		cfg->title_height = atoi(tok[1]);
	} else if (!strcmp(tok[0], "title-font")) {
		free(cfg->title_font);
		cfg->title_font = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "text-color-active")) {
		cfg->text_color_active = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "text-color-inactive")) {
		cfg->text_color_inactive = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "border-color-active")) {
		cfg->border_color_active = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "border-color-inactive")) {
		cfg->border_color_inactive = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "keyboard-layout")) {
		free(cfg->kb_layout);
		cfg->kb_layout = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "keyboard-variant")) {
		free(cfg->kb_variant);
		cfg->kb_variant = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "keyboard-options")) {
		free(cfg->kb_options);
		cfg->kb_options = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "keyboard-model")) {
		free(cfg->kb_model);
		cfg->kb_model = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "keyboard-rules")) {
		free(cfg->kb_rules);
		cfg->kb_rules = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "repeat-rate")) {
		cfg->repeat_rate = atoi(tok[1]);
	} else if (!strcmp(tok[0], "repeat-delay")) {
		cfg->repeat_delay = atoi(tok[1]);
	} else if (!strcmp(tok[0], "cursor")) {
		cfg->cursor_theme = truthy(tok[1]);
	} else if (!strcmp(tok[0], "cursor-color-in")) {
		cfg->cursor_color_in = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "cursor-color-out")) {
		cfg->cursor_color_out = parse_color(tok[1]);
	} else {
		diag_at(false, "unknown setting '%s'", tok[0]);
	}
}

/* Adds the built-in default bindings (terminal/launcher spawn, close,
 * fullscreen, center, quit, workspace switching, mod-drag move/resize,
 * alt+Tab cycle-focus). These use cfg->mod/terminal/launcher, so they must
 * be seeded *after* globals have been parsed, and are added via
 * add_binding() so that any `bind` line in the config for the same combo
 * simply replaces them. */
static void
add_default_bindings(struct sushi_config *cfg)
{
	struct sushi_binding *b;

	b = add_binding(cfg, BIND_KEY, cfg->mod, XKB_KEY_Return);
	b->action = ACTION_SPAWN;
	b->spawn_argv = calloc(2, sizeof(char *));
	b->spawn_argv[0] = xstrdup(cfg->terminal);

	b = add_binding(cfg, BIND_KEY, cfg->mod, XKB_KEY_d);
	b->action = ACTION_SPAWN;
	b->spawn_argv = calloc(2, sizeof(char *));
	b->spawn_argv[0] = xstrdup(cfg->launcher);

	b = add_binding(cfg, BIND_KEY, cfg->mod, XKB_KEY_q);
	b->action = ACTION_CLOSE;

	b = add_binding(cfg, BIND_KEY, cfg->mod, XKB_KEY_f);
	b->action = ACTION_FULLSCREEN;

	b = add_binding(cfg, BIND_KEY, cfg->mod, XKB_KEY_c);
	b->action = ACTION_CENTER;

	b = add_binding(cfg, BIND_KEY, cfg->mod | SWC_MOD_SHIFT, XKB_KEY_q);
	b->action = ACTION_QUIT;

	static const xkb_keysym_t digits[10] = {
		XKB_KEY_1, XKB_KEY_2, XKB_KEY_3, XKB_KEY_4, XKB_KEY_5,
		XKB_KEY_6, XKB_KEY_7, XKB_KEY_8, XKB_KEY_9, XKB_KEY_0,
	};
	for (int i = 0; i < 10; i++) {
		int ws = i + 1;

		b = add_binding(cfg, BIND_KEY, cfg->mod, digits[i]);
		b->action = ACTION_WORKSPACE;
		b->arg = ws;

		b = add_binding(cfg, BIND_KEY, cfg->mod | SWC_MOD_SHIFT, digits[i]);
		b->action = ACTION_MOVE_TO_WORKSPACE;
		b->arg = ws;
	}

	b = add_binding(cfg, BIND_BUTTON, cfg->mod, BTN_LEFT);
	b->action = ACTION_MOVE;

	b = add_binding(cfg, BIND_BUTTON, cfg->mod, BTN_RIGHT);
	b->action = ACTION_RESIZE;

	/* alt+Tab is the conventional MOD1+Tab focus-cycle, independent of
	 * whatever `mod` is configured to. */
	b = add_binding(cfg, BIND_KEY, SWC_MOD_ALT, XKB_KEY_Tab);
	b->action = ACTION_CYCLE_FOCUS;
}

static enum sushi_tri
parse_tri(const char *s)
{
	/* "enabled"/"disabled" as well as the booleans truthy() knows, since
	 * that is how libinput's own documentation spells these. */
	if (!strcmp(s, "enabled") || truthy(s))
		return TRI_ON;
	return TRI_OFF;
}

static void
parse_input_line(struct sushi_input_rule *r, char **tok, int ntok)
{
	if (ntok < 2) {
		diag_at(true, "'%s' inside an input block needs a value", tok[0]);
		return;
	}

	if (!strcmp(tok[0], "natural-scroll")) {
		r->natural_scroll = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "tap")) {
		r->tap = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "drag")) {
		r->drag = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "drag-lock")) {
		r->drag_lock = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "disable-while-typing")) {
		r->disable_while_typing = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "left-handed")) {
		r->left_handed = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "middle-emulation")) {
		r->middle_emulation = parse_tri(tok[1]);
	} else if (!strcmp(tok[0], "accel-profile")) {
		if (!strcmp(tok[1], "flat"))
			r->accel_profile = ACCEL_FLAT;
		else if (!strcmp(tok[1], "adaptive"))
			r->accel_profile = ACCEL_ADAPTIVE;
		else
			diag_at(true, "accel-profile '%s' is not flat or adaptive", tok[1]);
	} else if (!strcmp(tok[0], "accel-speed")) {
		char *end = NULL;
		r->accel_speed = strtod(tok[1], &end);
		r->has_accel_speed = true;
		if (end == tok[1] || (end && *end))
			diag_at(true, "accel-speed '%s' is not a number", tok[1]);
		else if (r->accel_speed < -1.0 || r->accel_speed > 1.0)
			diag_at(true, "accel-speed %.2f is outside -1.0..1.0",
			        r->accel_speed);
	} else if (!strcmp(tok[0], "scroll-method")) {
		if (!strcmp(tok[1], "none"))
			r->scroll_method = SCROLL_NONE;
		else if (!strcmp(tok[1], "two-finger"))
			r->scroll_method = SCROLL_TWO_FINGER;
		else if (!strcmp(tok[1], "edge"))
			r->scroll_method = SCROLL_EDGE;
		else if (!strcmp(tok[1], "button"))
			r->scroll_method = SCROLL_BUTTON;
		else
			diag_at(true, "scroll-method '%s' is not two-finger, edge, "
			              "button or none", tok[1]);
	} else {
		diag_at(false, "unknown input setting '%s'", tok[0]);
	}
}

static struct sushi_input_rule *
input_rule_find_or_create(struct sushi_config *cfg, const char *pattern)
{
	struct sushi_input_rule *r;

	wl_list_for_each(r, &cfg->input_rules, link) {
		if (!strcmp(r->pattern, pattern))
			return r;
	}

	r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;
	r->pattern = xstrdup(pattern);
	/* Appended so that, for a device matching several patterns, the later
	 * block in the file wins. */
	wl_list_insert(cfg->input_rules.prev, &r->link);
	return r;
}

static void
parse_autostart_line(struct sushi_config *cfg, char **tok, int ntok)
{
	if (ntok < 2) {
		diag_at(true, "autostart needs a command");
		return;
	}

	if (!on_path(tok[1]))
		diag_at(false, "autostart '%s' not found on PATH", tok[1]);

	struct sushi_autostart *a = calloc(1, sizeof(*a));
	if (!a)
		return;

	int argc = ntok - 1;
	a->spawn_argv = calloc(argc + 1, sizeof(char *));
	if (!a->spawn_argv) {
		free(a);
		return;
	}
	for (int i = 0; i < argc; i++)
		a->spawn_argv[i] = xstrdup(tok[1 + i]);
	a->spawn_argv[argc] = NULL;

	/* Appended, so entries run in the order they appear in the file. */
	wl_list_insert(cfg->autostart.prev, &a->link);
}

/* One pass over the file. When globals_only is set, bind/window lines are
 * ignored (used for the pre-pass that resolves mod/terminal/launcher before
 * the defaults are seeded). Otherwise the full grammar is parsed. */
static void
parse_stream(struct sushi_config *cfg, FILE *f, bool globals_only)
{
	char line[MAX_LINE];
	struct sushi_rule *in_rule = NULL;
	struct sushi_input_rule *in_input = NULL;

	if (diag)
		diag->line = 0;

	while (fgets(line, sizeof(line), f)) {
		char *tok[MAX_TOKENS];
		int ntok;

		if (diag)
			diag->line++;

		ntok = tokenize(line, tok, MAX_TOKENS);
		if (ntok == 0)
			continue;

		if (globals_only) {
			parse_global_line(cfg, tok, ntok);
			continue;
		}

		if (in_input) {
			if (!strcmp(tok[0], "}"))
				in_input = NULL;
			else
				parse_input_line(in_input, tok, ntok);
			continue;
		}

		if (in_rule) {
			if (!strcmp(tok[0], "}"))
				in_rule = NULL;
			else
				parse_rule_line(in_rule, tok, ntok);
			continue;
		}

		if (!strcmp(tok[0], "input")) {
			if (ntok < 2) {
				diag_at(true, "input needs a pattern");
				continue;
			}
			in_input = input_rule_find_or_create(cfg, tok[1]);
			continue;
		}

		if (!strcmp(tok[0], "window")) {
			if (ntok < 2) {
				diag_at(true, "window needs an app_id pattern");
				continue;
			}
			in_rule = rule_find_or_create(cfg, tok[1]);
			continue;
		}

		if (!strcmp(tok[0], "bind")) {
			parse_bind_line(cfg, tok, ntok);
			continue;
		}

		/* Handled here rather than in parse_global_line(), which runs over
		 * every line in both passes. Appending from there would add each
		 * entry twice. */
		if (!strcmp(tok[0], "autostart")) {
			parse_autostart_line(cfg, tok, ntok);
			continue;
		}

		parse_global_line(cfg, tok, ntok);
	}

	if (in_rule)
		diag_at(true, "window block is never closed with '}'");
	if (in_input)
		diag_at(true, "input block is never closed with '}'");
}

bool
config_validate(const char *path)
{
	struct config_diag d = {0};
	struct sushi_config *cfg = calloc(1, sizeof(*cfg));
	const char *used = path;
	FILE *f;

	if (!cfg)
		return false;
	config_defaults(cfg);

	f = path ? fopen(path, "r") : NULL;
	if (f) {
		printf("checking %s\n", path);
	} else {
		used = SUSHI_DEFAULT_CONFIG;
		f = fopen(used, "r");
		if (f) {
			printf("no config at %s\n", path ? path : "(none)");
			printf("using the installed default: %s\n", used);
		} else {
			printf("no config at %s\n", path ? path : "(none)");
			printf("no installed default at %s either\n", used);
			printf("sushi would run on its built-in defaults\n");
		}
	}

	if (f) {
		/* Globals first, exactly as config_load() does, but quietly: this
		 * pass sends every line to parse_global_line(), block contents
		 * included, so reporting from it would invent errors for lines that
		 * the second pass handles correctly. */
		parse_stream(cfg, f, true);
		rewind(f);
		add_default_bindings(cfg);

		d.path = used;
		diag = &d;
		parse_stream(cfg, f, false);
		diag = NULL;
		fclose(f);
	}

	if (cfg->repeat_rate < 0)
		printf("%s: error: repeat-rate %d is negative\n", used,
		       cfg->repeat_rate), d.errors++;
	if (cfg->repeat_delay < 0)
		printf("%s: error: repeat-delay %d is negative\n", used,
		       cfg->repeat_delay), d.errors++;

	/* At startup a bad layout only costs a line on stderr. Here it is an
	 * error, which is the point of validate.
	 *
	 * xkbcommon writes its own (useful) diagnosis to stderr, unbuffered, so
	 * flush first or our buffered lines all land after it. */
	fflush(stdout);
	if (!config_keymap_builds(cfg)) {
		printf("%s: error: xkbcommon cannot build the keyboard layout "
		       "(layout '%s', variant '%s', options '%s')\n",
		       used, cfg->kb_layout ? cfg->kb_layout : "",
		       cfg->kb_variant ? cfg->kb_variant : "",
		       cfg->kb_options ? cfg->kb_options : "");
		d.errors++;
	}

	config_free(cfg);

	printf("\n%d error%s, %d warning%s\n", d.errors, d.errors == 1 ? "" : "s",
	       d.warnings, d.warnings == 1 ? "" : "s");

	return d.errors == 0;
}

bool
config_keymap_builds(const struct sushi_config *cfg)
{
	if (!cfg->kb_rules && !cfg->kb_model && !cfg->kb_layout &&
	    !cfg->kb_variant && !cfg->kb_options)
		return true;

	struct xkb_rule_names names = {
		.rules = cfg->kb_rules,
		.model = cfg->kb_model,
		.layout = cfg->kb_layout,
		.variant = cfg->kb_variant,
		.options = cfg->kb_options,
	};
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap =
	    ctx ? xkb_keymap_new_from_names(ctx, &names, 0) : NULL;
	bool ok = keymap != NULL;

	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);

	return ok;
}

struct sushi_config *
config_load(const char *path)
{
	struct sushi_config *cfg = calloc(1, sizeof(*cfg));
	config_defaults(cfg);

	FILE *f = path ? fopen(path, "r") : NULL;

	/* With no config of their own, fall back to the one installed
	 * alongside sushi, so the shipped defaults are the documented file
	 * rather than a second copy of them buried in config_defaults(). */
	if (!f) {
		f = fopen(SUSHI_DEFAULT_CONFIG, "r");
		if (f)
			fprintf(stderr, "sushi: no config at %s, using %s\n",
			        path ? path : "(none)", SUSHI_DEFAULT_CONFIG);
	}

	if (f) {
		parse_stream(cfg, f, true);
		rewind(f);
	}

	add_default_bindings(cfg);

	if (f) {
		parse_stream(cfg, f, false);
		fclose(f);
	}

	return cfg;
}

void
config_free(struct sushi_config *cfg)
{
	if (!cfg)
		return;

	struct sushi_binding *b, *btmp;
	wl_list_for_each_safe(b, btmp, &cfg->bindings, link)
		binding_free(b);

	struct sushi_input_rule *ir, *irtmp;
	wl_list_for_each_safe(ir, irtmp, &cfg->input_rules, link) {
		wl_list_remove(&ir->link);
		free(ir->pattern);
		free(ir);
	}

	struct sushi_autostart *a, *atmp;
	wl_list_for_each_safe(a, atmp, &cfg->autostart, link) {
		wl_list_remove(&a->link);
		for (int i = 0; a->spawn_argv && a->spawn_argv[i]; i++)
			free(a->spawn_argv[i]);
		free(a->spawn_argv);
		free(a);
	}

	struct sushi_rule *r, *rtmp;
	wl_list_for_each_safe(r, rtmp, &cfg->rules, link) {
		wl_list_remove(&r->link);
		free(r->pattern);
		free(r);
	}

	free(cfg->terminal);
	free(cfg->launcher);
	free(cfg->theme);
	free(cfg->title_font);
	free(cfg->kb_layout);
	free(cfg->kb_variant);
	free(cfg->kb_options);
	free(cfg->kb_model);
	free(cfg->kb_rules);
	free(cfg);
}

static bool
pattern_match(const char *pattern, const char *app_id)
{
	size_t plen = strlen(pattern);
	if (plen > 0 && pattern[plen - 1] == '*')
		return strncmp(pattern, app_id, plen - 1) == 0;
	return strcmp(pattern, app_id) == 0;
}

const struct sushi_rule *
config_match_rule(const struct sushi_config *cfg, const char *app_id)
{
	if (!app_id)
		return NULL;

	const struct sushi_rule *r;
	wl_list_for_each(r, &cfg->rules, link) {
		if (pattern_match(r->pattern, app_id))
			return r;
	}
	return NULL;
}

char *
config_default_path(void)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	char dir[1024];

	if (xdg && *xdg)
		snprintf(dir, sizeof(dir), "%s/sushi", xdg);
	else
		snprintf(dir, sizeof(dir), "%s/.config/sushi", getenv("HOME"));

	mkdir(dir, 0755); /* best-effort, fine if it already exists */

	char *path = malloc(strlen(dir) + strlen("/config") + 1);
	sprintf(path, "%s/config", dir);
	return path;
}
