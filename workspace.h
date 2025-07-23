#ifndef WORKSPACE_H
#define WORKSPACE_H

#include "types.h"

void list_ws(struct wayws_state *state, struct ws *w);
struct ws *ctx_of(struct ext_workspace_handle_v1 *h);
struct workspace_group *group_ctx_of(struct ext_workspace_group_handle_v1 *h);
size_t group_size(struct wayws_state *state, struct workspace_group *g);
struct ws *current_ws(struct wayws_state *state, size_t *out);
struct ws *neighbor(struct wayws_state *state, enum dir d);

#endif // WORKSPACE_H
