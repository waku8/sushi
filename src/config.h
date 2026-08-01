/* config.h
 *
 * Hand-written parser for sushi's config file. No external parsing
 * dependencies (no KDL/TOML/YAML libs), just a small line-oriented grammar:
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
 * See doc/config for the full default config and grammar reference.
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

/* A program to launch once, when the session starts. */
struct sushi_autostart {
	struct wl_list link;
	char **spawn_argv; /* NULL-terminated argv */
};

/* Tri-state for a libinput toggle: unset means "leave whatever libinput
 * defaulted to", which is not the same as explicitly false. */
enum sushi_tri {
	TRI_UNSET = 0,
	TRI_OFF,
	TRI_ON,
};

enum sushi_accel_profile {
	ACCEL_UNSET = 0,
	ACCEL_FLAT,
	ACCEL_ADAPTIVE,
};

enum sushi_scroll_method {
	SCROLL_UNSET = 0,
	SCROLL_NONE,
	SCROLL_TWO_FINGER,
	SCROLL_EDGE,
	SCROLL_BUTTON,
};

/* libinput settings for the devices matching `pattern`. */
struct sushi_input_rule {
	struct wl_list link;

	/* "type:touchpad", "type:mouse", "type:keyboard", a device name
	 * (trailing '*' means prefix match), or "*" for every device. */
	char *pattern;

	enum sushi_tri natural_scroll;
	enum sushi_tri tap;
	enum sushi_tri drag;
	enum sushi_tri drag_lock;
	enum sushi_tri disable_while_typing;
	enum sushi_tri left_handed;
	enum sushi_tri middle_emulation;
	enum sushi_accel_profile accel_profile;
	enum sushi_scroll_method scroll_method;

	bool has_accel_speed;
	double accel_speed; /* -1.0 (slowest) .. 1.0 (fastest) */
};

struct sushi_rule {
	struct wl_list link;

	char *pattern; /* exact app_id match, or a prefix if it ends in '*' */

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
	char *theme; /* "flat", "classic", "simple", "love", or "win95" */
	int border_width;
	int title_height;
	char *title_font; /* fontconfig pattern like "monospace:size=10". NULL = swc's default */

	/* Title bar text color. Also used for the button glyphs on themes that
	 * have them, except win95, which draws its glyphs in a fixed color as
	 * part of its own hardcoded palette. */
	uint32_t text_color_active;
	uint32_t text_color_inactive;

	/* Border/title bar background. flat/classic/simple/love all share one
	 * color for both (no separate border frame color). win95 ignores these
	 * entirely and keeps its own fixed 3D palette. */
	uint32_t border_color_active;
	uint32_t border_color_inactive;

	/* XKB keymap selection. NULL means "leave it to libxkbcommon", which
	 * falls back to the XKB_DEFAULT_* environment variables and then to
	 * us/pc105, so an unset key changes nothing. */
	char *kb_layout;  /* "us", or "us,br" for a switchable pair */
	char *kb_variant; /* "intl", "altgr-intl", ... */
	char *kb_options; /* "grp:alt_shift_toggle,ctrl:nocaps", ... */
	char *kb_model;
	char *kb_rules;

	/* Key repeat: rate in characters per second, delay in milliseconds. */
	int repeat_rate;
	int repeat_delay;

	bool cursor_theme; /* draw the built-in Plan 9 style cursors */
	uint32_t cursor_color_in;
	uint32_t cursor_color_out;

	struct wl_list bindings; /* struct sushi_binding::link */
	struct wl_list rules; /* struct sushi_rule::link */

	/* Launched once at startup and deliberately not on reload: a config
	 * reload should not open a second terminal every time the file is
	 * saved. */
	struct wl_list autostart; /* struct sushi_autostart::link */

	struct wl_list input_rules; /* struct sushi_input_rule::link */
};

/* Loads and parses the config at `path`. Always returns a usable config
 * (falls back to built-in defaults for anything missing or if the file
 * doesn't exist). */
struct sushi_config *config_load(const char *path);

/* Parses `path` reporting everything the runtime loader ignores silently:
 * unknown keys, bad values, unclosed blocks, commands not on PATH, a layout
 * xkbcommon cannot build. Also says which file was actually read. Prints to
 * stdout and returns false if anything is outright wrong. */
bool config_validate(const char *path);
void config_free(struct sushi_config *cfg);

/* True when xkbcommon can build the config's keyboard layout. Also true when
 * the config names no layout at all, since then there is nothing to reject. */
bool config_keymap_builds(const struct sushi_config *cfg);

/* Returns the first rule whose pattern matches app_id, or NULL. app_id may
 * be NULL (matches nothing). */
const struct sushi_rule *config_match_rule(const struct sushi_config *cfg,
                                           const char *app_id);

/* Resolves the default config path (~/.config/sushi/config), creating the
 * parent directory if missing. Returned string is malloc'd. */
char *config_default_path(void);

#endif
