#include "workspace.h"
#include "types.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void list_ws(struct wayws_state *state, struct ws *w) {
  if (w->listed)
    return;
  if (state->vlen == state->vcap) {
    state->vcap = state->vcap ? state->vcap * 2 : 8;
    state->vec = xrealloc(state->vec, state->vcap * sizeof *state->vec);
  }
  w->index = state->vlen;
  w->listed = 1;
  state->vec[state->vlen++] = w;
}

struct ws *ctx_of(struct ext_workspace_handle_v1 *h) {
  struct ws *w = wl_proxy_get_user_data((void *)h);
  if (!w) {
    w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->h = h;
    wl_proxy_set_user_data((void *)h, w);
  }
  return w;
}

struct workspace_group *group_ctx_of(struct ext_workspace_group_handle_v1 *h) {
  struct workspace_group *g = wl_proxy_get_user_data((void *)h);
  if (!g) {
    g = calloc(1, sizeof *g);
    if (!g) return NULL;
    g->h = h;
    wl_proxy_set_user_data((void *)h, g);
  }
  return g;
}

size_t group_size(struct wayws_state *state, struct workspace_group *g) {
  size_t n = 0;
  for (size_t i = 0; i < state->vlen; ++i)
    if (state->vec[i]->group == g)
      ++n;
  return n;
}

struct ws *current_ws(struct wayws_state *state, size_t *out) {
  if (state->opt_output_name) {
    struct ws *best = NULL;
    for (size_t i = 0; i < state->vlen; ++i) {
      struct ws *w = state->vec[i];
      if (!w->active || !w->group)
        continue;
      for (struct group_output *go = w->group->outputs; go; go = go->next) {
        if (go->output && go->output->name &&
            strcmp(go->output->name, state->opt_output_name) == 0) {
          if (!best || w->last_active_seq > best->last_active_seq)
            best = w;
        }
      }
    }
    if (best) {
      if (out)
        *out = best->index;
      return best;
    }
  }
  struct ws *best_multi = NULL;
  for (size_t i = 0; i < state->vlen; ++i) {
    struct ws *w = state->vec[i];
    if (!w->active || !w->group)
      continue;
    if (group_size(state, w->group) <= 1)
      continue;
    if (!best_multi || w->last_active_seq > best_multi->last_active_seq)
      best_multi = w;
  }
  if (best_multi) {
    if (out)
      *out = best_multi->index;
    return best_multi;
  }
  struct ws *best = NULL;
  for (size_t i = 0; i < state->vlen; ++i) {
    struct ws *w = state->vec[i];
    if (!w->active)
      continue;
    if (!best || w->last_active_seq > best->last_active_seq)
      best = w;
  }
  if (best && out)
    *out = best->index;
  return best;
}

struct ws *neighbor(struct wayws_state *state, enum dir d) {
  struct ws *cur = current_ws(state, NULL);
  if (!cur || !cur->group)
    return NULL;
  
  // Count workspaces in this group first
  size_t count = 0;
  for (size_t i = 0; i < state->vlen; ++i) {
    if (state->vec[i]->group == cur->group)
      count++;
  }
  
  if (!count)
    return NULL;
    
  // Allocate array on heap to avoid stack overflow
  struct ws **group_ws = malloc(count * sizeof(struct ws *));
  if (!group_ws) return NULL;
  
  // Fill the array
  size_t idx = 0;
  for (size_t i = 0; i < state->vlen; ++i) {
    if (state->vec[i]->group == cur->group)
      group_ws[idx++] = state->vec[i];
  }
  // Sort by index
  for (size_t i = 0; i + 1 < count; ++i) {
    for (size_t j = i + 1; j < count; ++j) {
      if (group_ws[i]->index > group_ws[j]->index) {
        struct ws *t = group_ws[i];
        group_ws[i] = group_ws[j];
        group_ws[j] = t;
      }
    }
  }
  
  // Find current position
  size_t cur_pos = 0;
  while (cur_pos < count && group_ws[cur_pos] != cur)
    cur_pos++;
  if (cur_pos == count) {
    free(group_ws);
    return NULL;
  }
  
  // Calculate grid position
  int x = (int)(cur_pos % state->grid_cols),
      y = (int)(cur_pos / state->grid_cols);
  int rows = (int)((count + state->grid_cols - 1) / state->grid_cols);
  
  // Calculate new position based on direction
  switch (d) {
  case DIR_UP:
    if (y == 0) {
      free(group_ws);
      return NULL;
    }
    --y;
    break;
  case DIR_DOWN:
    if (y >= rows - 1) {
      free(group_ws);
      return NULL;
    }
    ++y;
    break;
  case DIR_LEFT:
    if (x == 0) {
      free(group_ws);
      return NULL;
    }
    --x;
    break;
  case DIR_RIGHT:
    if (x >= state->grid_cols - 1) {
      free(group_ws);
      return NULL;
    }
    ++x;
    break;
  default:
    free(group_ws);
    return NULL;
  }
  
  size_t new_pos = (size_t)(y * state->grid_cols + x);
  if (new_pos >= count) {
    free(group_ws);
    return NULL;
  }
  
  struct ws *result = group_ws[new_pos];
  free(group_ws);
  return result;
}