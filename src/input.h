/* input.h: libinput device configuration */
#ifndef SUSHI_INPUT_H
#define SUSHI_INPUT_H

#include "config.h"

struct libinput_device;

/* Hook for swc's manager.new_device: remembers the device and applies the
 * current config to it. */
void input_device_added(struct libinput_device *device);

/* Re-applies `cfg` to every device seen so far. Called on config reload,
 * which is why the devices are remembered at all. */
void input_apply(const struct sushi_config *cfg);

void input_finalize(void);

#endif
