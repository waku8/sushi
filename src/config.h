/* sushi: config.h
 *
 * Hand-written parser for sushi's config file. No external parsing
 * dependencies (no KDL/TOML/YAML libs) -- just a small line-oriented
 * grammar:
 *
 *   mod logo
 *   terminal foot
 *   launcher fuzzel
 *   theme flat
 *
 *   bind mod+return spawn foot
 *   bind mod+q close
 *
 *   window "firefox" {
 *       workspace 2
 *   }
 *
 * See doc/sushi.conf for the full default config and grammar reference.
 */
#ifndef SUSHI_CONFIG_H
#define SUSHI_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-util.h>

enum sushi_action {
	ACTION_NONE = 0,
	ACTION_SPAWN,
	ACTION_CLOSE,
	ACTION_QUIT,
	ACTION_FULLSCREEN,
	ACTION_MAXIMIZE,
	ACTION_CENTER,
	ACTION_WORKSPACE,
	ACTION_MOVE_TO_WORKSPACE,
	ACTION_MOVE,
	ACTION_RESIZE,
	ACTION_CYCLE_FOCUS,
};

enum sushi_bind_type {
	BIND_KEY,
	BIND_BUTTON,
};

struct sushi_binding {
	struct wl_list link;

	enum sushi_bind_type type;
	uint32_t modifiers;
	uint32_t value; /* xkb keysym for BIND_KEY, BTN_* for BIND_BUTTON */

	enum sushi_action action;
	int arg; /* workspace number, for ACTION_WORKSPACE / ACTION_MOVE_TO_WORKSPACE */
	char **spawn_argv; /* NULL-terminated argv, for ACTION_SPAWN */
};

struct sushi_rule {
	struct wl_list link;

	char *pattern; /* app_id match; trailing '*' means prefix match */

	bool has_workspace;
	int workspace;
	bool has_title;
	bool title;
	bool has_border;
	bool border;
};

struct sushi_config {
	uint32_t mod;
	char *terminal;
	char *launcher;
	char *theme; /* "flat", "classic", or "simple" */
	int border_width;
	int title_height;
	char *title_font; /* fontconfig pattern, e.g. "monospace:size=10"; NULL = swc's built-in default */

	/* Used for both the title bar text and the title bar button glyphs
	 * (themes that have any), so they're always the same color by
	 * construction. */
	uint32_t text_color_active;
	uint32_t text_color_inactive;

	/* Border/title bar background. flat/classic/simple/love all share one
	 * color for both (no separate border frame color); win95 ignores these
	 * entirely and keeps its own fixed 3D palette. */
	uint32_t border_color_active;
	uint32_t border_color_inactive;

	bool cursor_theme; /* draw the built-in Plan 9 style cursors */
	uint32_t cursor_color_in;
	uint32_t cursor_color_out;

	struct wl_list bindings; /* struct sushi_binding::link */
	struct wl_list rules; /* struct sushi_rule::link */
};

/* Loads and parses the config at `path`. Always returns a usable config
 * (falls back to built-in defaults for anything missing or if the file
 * doesn't exist). */
struct sushi_config *config_load(const char *path);
void config_free(struct sushi_config *cfg);

/* Returns the first rule whose pattern matches app_id, or NULL. app_id may
 * be NULL (matches nothing). */
const struct sushi_rule *config_match_rule(const struct sushi_config *cfg,
                                           const char *app_id);

/* Resolves the default config path (~/.config/sushi/config), creating the
 * parent directory if missing. Returned string is malloc'd. */
char *config_default_path(void);

#endif
