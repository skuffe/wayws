#ifndef WAYLAND_H
#define WAYLAND_H

#include "types.h"

void wayland_init(struct wayws_state *state);
void wayland_destroy(struct wayws_state *state);
void wayland_set_global_state(struct wayws_state *state);

#endif // WAYLAND_H
