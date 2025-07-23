#include "wayland.h"
#include "types.h"
#include "util.h"
#include "workspace.h"
#include "event.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global state pointer - this is how we access the state from callbacks
static struct wayws_state *g_state = NULL;

void wayland_set_global_state(struct wayws_state *state) {
  g_state = state;
}

static void cb_name(void *d, struct ext_workspace_handle_v1 *h, const char *n) {
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(h);
  free(w->name);
  w->name = xstrdup(n);
  
  // Emit workspace name event (may be deferred if output not available)
  const char *output_name = get_output_name_for_workspace(w);
  if (strcmp(output_name, "(unknown)") == 0) {
    // Defer the event until output information is available
    add_pending_event(state, EVENT_WORKSPACE_NAME, w, w->x, w->y, 
                     w->active, w->urgent, w->hidden, DIR_NONE);
  } else {
    emit_event(state, EVENT_WORKSPACE_NAME, w->name, output_name, w->index + 1,
               w->x, w->y, w->active, w->urgent, w->hidden, DIR_NONE, NULL);
  }
}

static void cb_coordinates(void *d, struct ext_workspace_handle_v1 *h,
                           struct wl_array *coords) {
  (void)d;
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(h);
  if (coords->size >= (sizeof(int32_t) * 2)) {
    int32_t *data = coords->data;
    w->x = data[0];
    w->y = data[1];
  }
  
  // Emit workspace coordinates event (may be deferred if output not available)
  const char *output_name = get_output_name_for_workspace(w);
  if (strcmp(output_name, "(unknown)") == 0) {
    // Defer the event until output information is available
    add_pending_event(state, EVENT_WORKSPACE_COORDINATES, w, w->x, w->y, 
                     w->active, w->urgent, w->hidden, DIR_NONE);
  } else {
    emit_event(state, EVENT_WORKSPACE_COORDINATES, w->name, output_name, w->index + 1,
               w->x, w->y, w->active, w->urgent, w->hidden, DIR_NONE, NULL);
  }
}

static void cb_state(void *d, struct ext_workspace_handle_v1 *h,
                     uint32_t bits) {
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(h);
  int was_active = w->active;
  w->active = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
  w->urgent = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT);
  w->hidden = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN);
  if (w->active && !was_active)
    w->last_active_seq = ++state->active_seq;
  list_ws(state, w);
  
  // Emit workspace state event (may be deferred if output not available)
  const char *output_name = get_output_name_for_workspace(w);
  if (strcmp(output_name, "(unknown)") == 0) {
    // Defer the event until output information is available
    add_pending_event(state, EVENT_WORKSPACE_STATE, w, w->x, w->y, 
                     w->active, w->urgent, w->hidden, DIR_NONE);
  } else {
    emit_event(state, EVENT_WORKSPACE_STATE, w->name, output_name, w->index + 1,
               w->x, w->y, w->active, w->urgent, w->hidden, DIR_NONE, NULL);
  }
}

static void stub_id(void *d, struct ext_workspace_handle_v1 *h, const char *s) {
  (void)d;
  (void)h;
  (void)s;
}

static void stub_u32(void *d, struct ext_workspace_handle_v1 *h, uint32_t v) {
  (void)d;
  (void)h;
  (void)v;
}

static void cb_ws_removed(void *d, struct ext_workspace_handle_v1 *h) {
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(h);
  if (!w) return;
  
  // Clean up any pending events for this workspace
  cleanup_pending_events_for_workspace(state, w);
  
  // Emit workspace destroyed event
  emit_event(state, EVENT_WORKSPACE_DESTROYED, w->name, NULL, w->index + 1,
             w->x, w->y, w->active, w->urgent, w->hidden, DIR_NONE, NULL);
  
  // Remove from vector
  for (size_t i = 0; i < state->vlen; i++) {
    if (state->vec[i] == w) {
      state->vec[i] = state->vec[state->vlen - 1];
      state->vlen--;
      break;
    }
  }
  
  // Clean up workspace data
  free(w->name);
  w->name = NULL;
  
  // Don't destroy the wayland object here - let wayland handle it
  // ext_workspace_handle_v1_destroy(h);
  // free(w);
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id = stub_id,
    .name = cb_name,
    .coordinates = cb_coordinates,
    .state = cb_state,
    .capabilities = stub_u32,
    .removed = cb_ws_removed,
};

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y,
                         int32_t w, int32_t h, int32_t sub, const char *make,
                         const char *model, int32_t transform) {
  (void)o;
  (void)sub;
  (void)make;
  (void)model;
  (void)transform;
  struct output *out = d;
  out->x = x;
  out->y = y;
  out->width = w;
  out->height = h;
}

static void out_name(void *d, struct wl_output *o, const char *name) {
  (void)o;
  ((struct output *)d)->name = xstrdup(name);
}

static void out_description(void *d, struct wl_output *o, const char *desc) {
  (void)d;
  (void)o;
  (void)desc;
}

static void out_done(void *d, struct wl_output *o) {
  (void)d;
  (void)o;
}

static void out_scale(void *d, struct wl_output *o, int32_t factor) {
  (void)d;
  (void)o;
  (void)factor;
}

static void out_mode(void *d, struct wl_output *o, uint32_t flags,
                     int32_t width, int32_t height, int32_t refresh) {
  (void)d;
  (void)o;
  (void)flags;
  (void)width;
  (void)height;
  (void)refresh;
}

static const struct wl_output_listener out_listener = {
    .geometry = out_geometry,
    .mode = out_mode,
    .done = out_done,
    .scale = out_scale,
    .name = out_name,
    .description = out_description,
};

static void group_capabilities(void *d, struct ext_workspace_group_handle_v1 *h,
                               uint32_t capabilities) {
  (void)d;
  (void)h;
  (void)capabilities;
}

static void group_output_enter(void *d, struct ext_workspace_group_handle_v1 *h,
                               struct wl_output *output) {
  (void)h;
  struct workspace_group *g = d;
  struct wayws_state *state = g_state;
  struct group_output *node = calloc(1, sizeof *node);
  if (!node) return;
  
  for (struct output *o = state->all_outputs; o; o = o->next) {
    if (o->output == output) {
      node->output = o;
      break;
    }
  }
  node->next = g->outputs;
  g->outputs = node;
  if (node->output) {
    // Emit output enter event
    emit_event(state, EVENT_OUTPUT_ENTER, NULL, 
               node->output->name ? node->output->name : "(unknown)",
               0, 0, 0, 0, 0, 0, DIR_NONE, NULL);
    
    // Emit workspace enter events for pending workspaces and emit pending events
    for (size_t i = 0; i < state->vlen; ++i) {
      if (state->vec[i]->group == g && state->vec[i]->pending_enter) {
        const char *out_name = node->output->name ? node->output->name : "(unknown)";
        emit_event(state, EVENT_WORKSPACE_ENTER, 
                   state->vec[i]->name ? state->vec[i]->name : "", out_name,
                   state->vec[i]->index + 1, state->vec[i]->x, state->vec[i]->y,
                   state->vec[i]->active, state->vec[i]->urgent, state->vec[i]->hidden,
                   DIR_NONE, NULL);
        state->vec[i]->pending_enter = 0;
        
        // Emit any pending events for this workspace now that output is available
        emit_pending_events_for_workspace(state, state->vec[i]);
      }
    }
  }
}

static void group_output_leave(void *d, struct ext_workspace_group_handle_v1 *h,
                               struct wl_output *output) {
  (void)h;
  struct workspace_group *g = d;
  struct wayws_state *state = g_state;
  struct group_output **pp = &g->outputs;
  while (*pp) {
    if ((*pp)->output && (*pp)->output->output == output) {
      struct group_output *tmp = *pp;
      // Emit output leave event
      emit_event(state, EVENT_OUTPUT_LEAVE, NULL,
                 tmp->output && tmp->output->name ? tmp->output->name : "(unknown)",
                 0, 0, 0, 0, 0, 0, DIR_NONE, NULL);
      *pp = tmp->next;
      free(tmp);
      break;
    }
    pp = &(*pp)->next;
  }
}

static void group_workspace_enter(void *d,
                                  struct ext_workspace_group_handle_v1 *h,
                                  struct ext_workspace_handle_v1 *workspace) {
  (void)h;
  struct workspace_group *g = d;
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(workspace);
  w->group = g;
  if (g->outputs && g->outputs->output && g->outputs->output->name) {
    // Emit workspace enter event
    emit_event(state, EVENT_WORKSPACE_ENTER, 
               w->name ? w->name : "", g->outputs->output->name,
               w->index + 1, w->x, w->y, w->active, w->urgent, w->hidden,
               DIR_NONE, NULL);
    
    // Emit any pending events for this workspace now that output is available
    emit_pending_events_for_workspace(state, w);
  } else {
    w->pending_enter = 1;
  }
}

static void group_workspace_leave(void *d,
                                  struct ext_workspace_group_handle_v1 *h,
                                  struct ext_workspace_handle_v1 *workspace) {
  (void)h;
  struct workspace_group *g = d;
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(workspace);
  const char *out_name = "(unknown)";
  if (g->outputs && g->outputs->output && g->outputs->output->name)
    out_name = g->outputs->output->name;
  // Emit workspace leave event
  emit_event(state, EVENT_WORKSPACE_LEAVE, 
             w->name ? w->name : "", out_name,
             w->index + 1, w->x, w->y, w->active, w->urgent, w->hidden,
             DIR_NONE, NULL);
  w->group = NULL;
  w->pending_enter = 0;
}

static void group_removed(void *d, struct ext_workspace_group_handle_v1 *h) {
  (void)h;
  struct workspace_group *g = d;
  struct wayws_state *state = g_state;
  struct workspace_group **pp = &state->workspace_groups;
  while (*pp) {
    if (*pp == g) {
      *pp = g->next;
      break;
    }
    pp = &(*pp)->next;
  }
  for (struct group_output *n = g->outputs, *next; n; n = next) {
    next = n->next;
    free(n);
  }
  for (size_t i = 0; i < state->vlen; ++i)
    if (state->vec[i]->group == g)
      state->vec[i]->group = NULL;
  free(g);
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
    .capabilities = group_capabilities,
    .output_enter = group_output_enter,
    .output_leave = group_output_leave,
    .workspace_enter = group_workspace_enter,
    .workspace_leave = group_workspace_leave,
    .removed = group_removed,
};

static void mgr_workspace(void *d, struct ext_workspace_manager_v1 *m,
                          struct ext_workspace_handle_v1 *h) {
  (void)d;
  struct wayws_state *state = g_state;
  struct ws *w = ctx_of(h);
  
  ext_workspace_handle_v1_add_listener(h, &ws_listener, w);
  
  // Add workspace to vector
  if (state->vlen >= state->vcap) {
    state->vcap = state->vcap ? state->vcap * 2 : 8;
    state->vec = xrealloc(state->vec, state->vcap * sizeof *state->vec);
  }
  w->index = state->vlen;
  w->listed = 1;  // Mark as listed to prevent duplicates in list_ws
  state->vec[state->vlen++] = w;
  
  // Emit workspace created event (may be deferred if output not available)
  const char *output_name = get_output_name_for_workspace(w);
  if (strcmp(output_name, "(unknown)") == 0) {
    // Defer the event until output information is available
    add_pending_event(state, EVENT_WORKSPACE_CREATED, w, w->x, w->y, 
                     w->active, w->urgent, w->hidden, DIR_NONE);
  } else {
    emit_event(state, EVENT_WORKSPACE_CREATED, w->name, output_name, w->index + 1,
               w->x, w->y, w->active, w->urgent, w->hidden, DIR_NONE, NULL);
  }
}

static void mgr_workspace_group(void *d, struct ext_workspace_manager_v1 *m,
                                struct ext_workspace_group_handle_v1 *h) {
  (void)d;
  struct wayws_state *state = g_state;
  struct workspace_group *g = group_ctx_of(h);
  ext_workspace_group_handle_v1_add_listener(h, &group_listener, g);
  g->next = state->workspace_groups;
  state->workspace_groups = g;
}

static void stub_mgr(void *d, struct ext_workspace_manager_v1 *m) {
  (void)d;
  (void)m;
}

static const struct ext_workspace_manager_v1_listener mgr_listener = {
    .workspace_group = mgr_workspace_group,
    .workspace = mgr_workspace,
    .done = stub_mgr,
    .finished = stub_mgr,
};

static void reg_global(void *d, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver) {
  (void)r;
  struct wayws_state *state = d;
  if (strcmp(iface, "ext_workspace_manager_v1") == 0) {
    state->mgr = wl_registry_bind(r, name, &ext_workspace_manager_v1_interface, 1);
  } else if (strcmp(iface, "wl_output") == 0) {
    struct output *out = calloc(1, sizeof *out);
    if (!out) return;
    
    out->output = wl_registry_bind(r, name, &wl_output_interface, 4);
    if (!out->output) {
      free(out);
      return;
    }
    
    wl_output_add_listener(out->output, &out_listener, out);
    out->next = state->all_outputs;
    state->all_outputs = out;
  }
}

static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {
  (void)d;
  (void)r;
  (void)n;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_remove,
};

void wayland_init(struct wayws_state *state) {
  state->dpy = wl_display_connect(NULL);
  if (!state->dpy)
    die("Failed to connect to Wayland display.\n");
  state->mgr = NULL;
  
  // Clear any existing workspace data
  state->vlen = 0;
  state->vcap = 0;
  state->vec = NULL;
  
  struct wl_registry *reg = wl_display_get_registry(state->dpy);
  wl_registry_add_listener(reg, &reg_listener, state);
  wl_display_roundtrip(state->dpy);
  if (!state->mgr)
    die("Compositor does not support ext-workspace-v1.\n");
  ext_workspace_manager_v1_add_listener(state->mgr, &mgr_listener, state);
  wl_display_roundtrip(state->dpy);
}

void wayland_destroy(struct wayws_state *state) {
  if (state->mgr) {
    ext_workspace_manager_v1_destroy(state->mgr);
    state->mgr = NULL;
  }
  if (state->dpy) {
    wl_display_disconnect(state->dpy);
    state->dpy = NULL;
  }
  
  // Clean up any remaining pending events
  cleanup_all_pending_events(state);
}
