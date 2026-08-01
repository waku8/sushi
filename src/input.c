/* input.c: libinput device configuration
 *
 * swc hands the raw struct libinput_device to the window manager through
 * manager.new_device, so device settings are sushi's to apply. There is no
 * swc-side API in the middle.
 *
 * Devices are kept in a list so a config reload can re-apply to hardware that
 * was plugged in long before the file changed.
 */
#include "input.h"
#include "sushi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libinput.h>

struct sushi_device {
	struct wl_list link;
	struct libinput_device *device;
};

static bool
is_touchpad(struct libinput_device *device)
{
	/* A tap finger count only exists on devices libinput considers
	 * touchpads, which is a sharper test than the pointer capability that
	 * mice share. */
	return libinput_device_config_tap_get_finger_count(device) > 0;
}

static bool
matches(const char *pattern, struct libinput_device *device)
{
	if (!strcmp(pattern, "*")) {
		return true;
	}

	if (!strcmp(pattern, "type:touchpad")) {
		return is_touchpad(device);
	}
	if (!strcmp(pattern, "type:mouse")) {
		return libinput_device_has_capability(device,
		                                     LIBINPUT_DEVICE_CAP_POINTER) &&
		       !is_touchpad(device);
	}
	if (!strcmp(pattern, "type:keyboard")) {
		return libinput_device_has_capability(device,
		                                      LIBINPUT_DEVICE_CAP_KEYBOARD);
	}

	const char *name = libinput_device_get_name(device);
	if (!name) {
		return false;
	}

	size_t len = strlen(pattern);
	if (len > 0 && pattern[len - 1] == '*') {
		return strncmp(name, pattern, len - 1) == 0;
	}
	return strcmp(name, pattern) == 0;
}

/* A setting the config asked for that the hardware cannot do is almost always
 * a mistake in the file, and silently dropping it leaves no way to tell that
 * from a setting that was applied and did nothing visible.
 *
 * Not reported for a `*` block, though: that one means "whatever applies", and
 * a machine with a keyboard and a mouse would otherwise complain about every
 * touchpad setting in it on every reload. A block that names type:touchpad or
 * a device and still gets ignored is the case worth hearing about. */
static void
unsupported(bool warn, struct libinput_device *device, const char *option)
{
	const char *name;

	if (!warn) {
		return;
	}

	name = libinput_device_get_name(device);
	fprintf(stderr, "sushi: '%s' not supported by device '%s', ignored\n",
	        option, name ? name : "?");
}

static void
apply_rule(const struct sushi_input_rule *r, struct libinput_device *device,
           bool warn)
{
	bool is_tap = libinput_device_config_tap_get_finger_count(device) > 0;

	if (r->natural_scroll != TRI_UNSET) {
		if (libinput_device_config_scroll_has_natural_scroll(device)) {
			libinput_device_config_scroll_set_natural_scroll_enabled(
			    device, r->natural_scroll == TRI_ON);
		} else {
			unsupported(warn, device, "natural-scroll");
		}
	}

	if (r->tap != TRI_UNSET) {
		if (is_tap) {
			libinput_device_config_tap_set_enabled(
			    device, r->tap == TRI_ON ? LIBINPUT_CONFIG_TAP_ENABLED
			                             : LIBINPUT_CONFIG_TAP_DISABLED);
		} else {
			unsupported(warn, device, "tap");
		}
	}

	if (r->drag != TRI_UNSET) {
		if (is_tap) {
			libinput_device_config_tap_set_drag_enabled(
			    device, r->drag == TRI_ON ? LIBINPUT_CONFIG_DRAG_ENABLED
			                              : LIBINPUT_CONFIG_DRAG_DISABLED);
		} else {
			unsupported(warn, device, "drag");
		}
	}

	if (r->drag_lock != TRI_UNSET) {
		if (is_tap) {
			libinput_device_config_tap_set_drag_lock_enabled(
			    device, r->drag_lock == TRI_ON
			                ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED
			                : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
		} else {
			unsupported(warn, device, "drag-lock");
		}
	}

	if (r->disable_while_typing != TRI_UNSET) {
		if (libinput_device_config_dwt_is_available(device)) {
			libinput_device_config_dwt_set_enabled(
			    device, r->disable_while_typing == TRI_ON
			                ? LIBINPUT_CONFIG_DWT_ENABLED
			                : LIBINPUT_CONFIG_DWT_DISABLED);
		} else {
			unsupported(warn, device, "disable-while-typing");
		}
	}

	if (r->left_handed != TRI_UNSET) {
		if (libinput_device_config_left_handed_is_available(device)) {
			libinput_device_config_left_handed_set(device,
			                                      r->left_handed == TRI_ON);
		} else {
			unsupported(warn, device, "left-handed");
		}
	}

	if (r->middle_emulation != TRI_UNSET) {
		if (libinput_device_config_middle_emulation_is_available(device)) {
			libinput_device_config_middle_emulation_set_enabled(
			    device, r->middle_emulation == TRI_ON
			                ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
			                : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
		} else {
			unsupported(warn, device, "middle-emulation");
		}
	}

	if (r->accel_profile != ACCEL_UNSET) {
		bool flat = r->accel_profile == ACCEL_FLAT;
		uint32_t want = flat ? LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
		                     : LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;

		if (libinput_device_config_accel_get_profiles(device) & want) {
			libinput_device_config_accel_set_profile(device, want);
		} else {
			/* Names the profile, since a device can offer one and not the
			 * other. */
			unsupported(warn, device,
			            flat ? "accel-profile flat" : "accel-profile adaptive");
		}
	}

	if (r->has_accel_speed) {
		if (libinput_device_config_accel_is_available(device)) {
			double speed = r->accel_speed;

			/* libinput rejects anything outside this range outright. */
			if (speed < -1.0) {
				speed = -1.0;
			}
			if (speed > 1.0) {
				speed = 1.0;
			}
			libinput_device_config_accel_set_speed(device, speed);
		} else {
			unsupported(warn, device, "accel-speed");
		}
	}

	if (r->scroll_method != SCROLL_UNSET) {
		enum libinput_config_scroll_method want;
		const char *name;

		switch (r->scroll_method) {
		case SCROLL_TWO_FINGER:
			want = LIBINPUT_CONFIG_SCROLL_2FG;
			name = "scroll-method two-finger";
			break;
		case SCROLL_EDGE:
			want = LIBINPUT_CONFIG_SCROLL_EDGE;
			name = "scroll-method edge";
			break;
		case SCROLL_BUTTON:
			want = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
			name = "scroll-method button";
			break;
		default:
			want = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
			name = "scroll-method none";
			break;
		}

		if (libinput_device_config_scroll_get_methods(device) & want) {
			libinput_device_config_scroll_set_method(device, want);
		} else {
			unsupported(warn, device, name);
		}
	}
}

static void
configure(struct libinput_device *device, const struct sushi_config *cfg)
{
	struct sushi_input_rule *r;

	/* In file order, so a later block overrides an earlier one for a device
	 * that matches both: "*" first, then a narrowing "type:touchpad". */
	wl_list_for_each(r, &cfg->input_rules, link) {
		if (matches(r->pattern, device)) {
			apply_rule(r, device, strcmp(r->pattern, "*") != 0);
		}
	}
}

void
input_device_added(struct libinput_device *device)
{
	struct sushi_device *entry = calloc(1, sizeof(*entry));

	if (entry) {
		/* Referenced so the entry stays valid for later reloads. libinput
		 * owns the device otherwise. */
		entry->device = libinput_device_ref(device);
		wl_list_insert(&sushi.devices, &entry->link);
	}

	configure(device, sushi.config);
}

void
input_apply(const struct sushi_config *cfg)
{
	struct sushi_device *entry;

	wl_list_for_each(entry, &sushi.devices, link)
		configure(entry->device, cfg);
}

void
input_finalize(void)
{
	struct sushi_device *entry, *tmp;

	wl_list_for_each_safe(entry, tmp, &sushi.devices, link) {
		wl_list_remove(&entry->link);
		libinput_device_unref(entry->device);
		free(entry);
	}
}
