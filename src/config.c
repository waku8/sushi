/* sushi: config.c */
#include "config.h"

#include <ctype.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <swc.h>

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

/* Colors are configured as plain "rrggbb" hex -- always fully opaque, since
 * nothing in sushi's decor ever uses partial transparency. Internally
 * colors still travel around as ARGB8888 (the software rasterizer and
 * swc's decor API both want that), so this just forces the alpha byte on.
 *
 * A leading '#' is accepted too, but only if quoted ("#rrggbb") -- '#'
 * unquoted starts a comment (see tokenize()) that eats the rest of the
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
 * mutated copy of `line` (NUL-terminated in place); the caller must keep
 * `buf` alive as long as the tokens are used. */
static int
tokenize(char *buf, char *tokens[], int max_tokens)
{
	int ntok = 0;
	char *p = buf;
	bool in_quotes = false;

	/* strip an unquoted comment first */
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
	cfg->cursor_theme = true;
	cfg->cursor_color_in = 0xFFFFFFFF;
	cfg->cursor_color_out = 0xFF000000;
	wl_list_init(&cfg->bindings);
	wl_list_init(&cfg->rules);
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
		 * was meant as the key, which isn't supported; treat as error. */
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
	if (ntok < 3)
		return;

	enum sushi_bind_type type;
	uint32_t mods, value;
	if (!parse_combo(cfg, tok[1], &type, &mods, &value))
		return;

	const char *action = tok[2];
	struct sushi_binding *b;

	if (!strcmp(action, "spawn")) {
		int argc = ntok - 3;
		if (argc <= 0)
			return;
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
	} else if (!strcmp(action, "workspace")) {
		if (ntok < 4)
			return;
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_WORKSPACE;
		b->arg = atoi(tok[3]);
	} else if (!strcmp(action, "move-to-workspace")) {
		if (ntok < 4)
			return;
		b = add_binding(cfg, type, mods, value);
		b->action = ACTION_MOVE_TO_WORKSPACE;
		b->arg = atoi(tok[3]);
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
	if (ntok < 2)
		return;

	if (!strcmp(tok[0], "workspace")) {
		r->has_workspace = true;
		r->workspace = atoi(tok[1]);
	} else if (!strcmp(tok[0], "title")) {
		r->has_title = true;
		r->title = truthy(tok[1]);
	} else if (!strcmp(tok[0], "border")) {
		r->has_border = true;
		r->border = truthy(tok[1]);
	}
}

/* Applies a global "key value" line (mod/terminal/launcher/theme/
 * border-width/title-height/title-font/text-color-active/
 * text-color-inactive/border-color-active/border-color-inactive/cursor/
 * cursor-color-in/cursor-color-out).
 * No-op for anything else, so it's safe to call while skipping over
 * bind/window lines. */
static void
parse_global_line(struct sushi_config *cfg, char **tok, int ntok)
{
	if (ntok < 2)
		return;

	if (!strcmp(tok[0], "mod")) {
		uint32_t m;
		if (parse_mod_name(tok[1], &m))
			cfg->mod = m;
	} else if (!strcmp(tok[0], "terminal")) {
		free(cfg->terminal);
		cfg->terminal = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "launcher")) {
		free(cfg->launcher);
		cfg->launcher = xstrdup(tok[1]);
	} else if (!strcmp(tok[0], "theme")) {
		free(cfg->theme);
		cfg->theme = xstrdup(tok[1]);
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
	} else if (!strcmp(tok[0], "cursor")) {
		cfg->cursor_theme = truthy(tok[1]);
	} else if (!strcmp(tok[0], "cursor-color-in")) {
		cfg->cursor_color_in = parse_color(tok[1]);
	} else if (!strcmp(tok[0], "cursor-color-out")) {
		cfg->cursor_color_out = parse_color(tok[1]);
	}
}

/* Adds the built-in default bindings (terminal/launcher spawn, close,
 * fullscreen, center, quit, workspace switching, mod-drag move/resize).
 * These use cfg->mod/terminal/launcher, so they must be seeded *after*
 * globals have been parsed, and are added via add_binding() so that any
 * `bind` line in the config for the same combo simply replaces them. */
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

/* One pass over the file. When globals_only is set, bind/window lines are
 * ignored (used for the pre-pass that resolves mod/terminal/launcher before
 * the defaults are seeded); otherwise the full grammar is parsed. */
static void
parse_stream(struct sushi_config *cfg, FILE *f, bool globals_only)
{
	char line[MAX_LINE];
	struct sushi_rule *in_rule = NULL;

	while (fgets(line, sizeof(line), f)) {
		char *tok[MAX_TOKENS];
		int ntok = tokenize(line, tok, MAX_TOKENS);
		if (ntok == 0)
			continue;

		if (globals_only) {
			parse_global_line(cfg, tok, ntok);
			continue;
		}

		if (in_rule) {
			if (!strcmp(tok[0], "}"))
				in_rule = NULL;
			else
				parse_rule_line(in_rule, tok, ntok);
			continue;
		}

		if (!strcmp(tok[0], "window") && ntok >= 2) {
			in_rule = rule_find_or_create(cfg, tok[1]);
			continue;
		}

		if (!strcmp(tok[0], "bind")) {
			parse_bind_line(cfg, tok, ntok);
			continue;
		}

		parse_global_line(cfg, tok, ntok);
	}
}

struct sushi_config *
config_load(const char *path)
{
	struct sushi_config *cfg = calloc(1, sizeof(*cfg));
	config_defaults(cfg);

	FILE *f = path ? fopen(path, "r") : NULL;
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

	mkdir(dir, 0755); /* best-effort; fine if it already exists */

	char *path = malloc(strlen(dir) + strlen("/config") + 1);
	sprintf(path, "%s/config", dir);
	return path;
}
