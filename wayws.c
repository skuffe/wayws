/* wayws.c – ext-workspace-v1 helper
 *
 * Talks to compositors implementing the ext-workspace-v1 protocol
 * (e.g. labwc, Hyprland). Features:
 *   - List workspaces (index / output / name)
 *   - Activate by index, name, or relative direction (grid navigation)
 *   - Watch mode printing events (-w)
 *   - Waybar / JSON output
 *
 * Directional movement treats each *output* as its own grid (width --grid N).
 * The order inside a grid is the order we discovered workspaces (stable).
 *
 * “Current workspace” heuristic (because multiple outputs can each have an
 * active workspace):
 *   1. If --output NAME supplied, use the active workspace on that output.
 *   2. Else prefer an active workspace whose output has >1 workspaces
 *      (directional movement possible). If several, take the one most
 *      recently *activated*.
 *   3. Else fallback to the most recently activated active workspace.
 *
 * Why we track activation order ourselves:
 *   ext-workspace-v1 sends workspace_enter only when a workspace becomes
 *   associated with an output (initial mapping / migration). Activating an
 *   already‑mapped workspace does NOT send workspace_enter. We *do* get a
 *   state event each time ACTIVE bit changes, so we attach a monotonically
 *   increasing last_active_seq there. Because main() performs two roundtrips
 *   before acting on CLI switches, one‑shot commands already have the full
 *   initial state and activation ordering.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "ext_workspace_client.h"

/* --------------------------- CLI flags ----------------------------- */

static int flag_list = 0;
static int flag_watch = 0;
static int flag_waybar = 0;
static int flag_json = 0;
static int flag_debug = 0;
static char *opt_exec = NULL;
static char *opt_output_name = NULL;
static char *glyph_active = "\xE2\x97\x8F"; /* ● */
static char *glyph_empty = "\xE2\x97\x8B";  /* ○ */
static int want_idx = -1;
static char *want_name = NULL;

enum dir { DIR_NONE, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };
static enum dir move_dir = DIR_NONE;
static int grid_cols = 3;

/* ------------------------ data structures -------------------------- */

struct output {
  struct wl_output *output;
  char *name;
  int32_t x, y, width, height;
  struct output *next;
};

struct group_output {
  struct output *output;
  struct group_output *next;
};

struct workspace_group {
  struct ext_workspace_group_handle_v1 *h;
  struct group_output *outputs;
  struct workspace_group *next;
};

struct ws {
  struct ext_workspace_handle_v1 *h;
  char *name;
  int active, urgent, hidden;
  size_t index;
  int listed;
  int32_t x, y;
  struct workspace_group *group;
  int pending_enter;
  unsigned long last_active_seq;
};

static unsigned long active_seq = 0;
static struct ws **vec = NULL;
static size_t vlen = 0, vcap = 0;
static struct output *all_outputs = NULL;
static struct workspace_group *workspace_groups = NULL;
static struct wl_display *dpy = NULL;
static struct ext_workspace_manager_v1 *mgr = NULL;

/* ---------------------------- helpers ------------------------------ */

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) {
    perror("realloc");
    exit(1);
  }
  return q;
}
static char *xstrdup(const char *s) {
  if (!s)
    return NULL;
  char *p = strdup(s);
  if (!p) {
    perror("strdup");
    exit(1);
  }
  return p;
}
static int isnum(const char *s) {
  for (; *s; s++)
    if (!isdigit((unsigned char)*s))
      return 0;
  return 1;
}
static void die(const char *msg) {
  fputs(msg, stderr);
  exit(1);
}
static void list_ws(struct ws *w) {
  if (w->listed)
    return;
  if (vlen == vcap) {
    vcap = vcap ? vcap * 2 : 8;
    vec = xrealloc(vec, vcap * sizeof *vec);
  }
  w->index = vlen;
  w->listed = 1;
  vec[vlen++] = w;
}
static struct ws *ctx_of(struct ext_workspace_handle_v1 *h) {
  struct ws *w = wl_proxy_get_user_data((void *)h);
  if (!w) {
    w = calloc(1, sizeof *w);
    w->h = h;
    wl_proxy_set_user_data((void *)h, w);
  }
  return w;
}
static struct workspace_group *
group_ctx_of(struct ext_workspace_group_handle_v1 *h) {
  struct workspace_group *g = wl_proxy_get_user_data((void *)h);
  if (!g) {
    g = calloc(1, sizeof *g);
    g->h = h;
    wl_proxy_set_user_data((void *)h, g);
  }
  return g;
}

/* ------------------------ workspace listeners ---------------------- */

static void cb_name(void *d, struct ext_workspace_handle_v1 *h, const char *n) {
  (void)d;
  struct ws *w = ctx_of(h);
  if (flag_watch && w->name && w->name[0]) {
    printf("event=name old_name=\"%s\" new_name=\"%s\"\n", w->name, n);
    fflush(stdout);
  }
  free(w->name);
  w->name = xstrdup(n);
}
static void cb_coordinates(void *d, struct ext_workspace_handle_v1 *h,
                           struct wl_array *coords) {
  (void)d;
  struct ws *w = ctx_of(h);
  if (coords->size >= (sizeof(int32_t) * 2)) {
    int32_t *data = coords->data;
    w->x = data[0];
    w->y = data[1];
  }
}
static void cb_state(void *d, struct ext_workspace_handle_v1 *h,
                     uint32_t bits) {
  (void)d;
  struct ws *w = ctx_of(h);
  int was_active = w->active;
  w->active = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE);
  w->urgent = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_URGENT);
  w->hidden = !!(bits & EXT_WORKSPACE_HANDLE_V1_STATE_HIDDEN);
  if (w->active && !was_active)
    w->last_active_seq = ++active_seq;
  list_ws(w);
  if (flag_watch) {
    printf("event=state name=\"%s\" x=%d y=%d active=%d urgent=%d hidden=%d\n",
           w->name ? w->name : "", w->x, w->y, w->active, w->urgent, w->hidden);
    fflush(stdout);
    if (opt_exec)
      system(opt_exec);
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
  (void)d;
  struct ws *w = ctx_of(h);
  if (flag_watch) {
    printf("event=workspace_removed name=\"%s\"\n", w->name ? w->name : "");
    fflush(stdout);
  }
  for (size_t i = 0; i < vlen; i++)
    if (vec[i] == w) {
      vec[i] = vec[vlen - 1];
      vlen--;
      break;
    }
  free(w->name);
  ext_workspace_handle_v1_destroy(h);
  free(w);
}
static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id = stub_id,
    .name = cb_name,
    .coordinates = cb_coordinates,
    .state = cb_state,
    .capabilities = stub_u32,
    .removed = cb_ws_removed,
};

/* ---------------------------- wl_output ---------------------------- */

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

/* ------------------------- workspace group ------------------------- */

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
  struct group_output *node = calloc(1, sizeof *node);
  for (struct output *o = all_outputs; o; o = o->next)
    if (o->output == output) {
      node->output = o;
      break;
    }
  node->next = g->outputs;
  g->outputs = node;
  if (flag_watch && node->output) {
    printf("event=output_enter output=\"%s\"\n",
           node->output->name ? node->output->name : "(unknown)");
    fflush(stdout);
    for (size_t i = 0; i < vlen; ++i) {
      if (vec[i]->group == g && vec[i]->pending_enter) {
        const char *out_name =
            node->output->name ? node->output->name : "(unknown)";
        printf("event=workspace_enter workspace=\"%s\" output=\"%s\"\n",
               vec[i]->name ? vec[i]->name : "", out_name);
        vec[i]->pending_enter = 0;
      }
    }
    fflush(stdout);
  }
}
static void group_output_leave(void *d, struct ext_workspace_group_handle_v1 *h,
                               struct wl_output *output) {
  (void)h;
  struct workspace_group *g = d;
  struct group_output **pp = &g->outputs;
  while (*pp) {
    if ((*pp)->output && (*pp)->output->output == output) {
      struct group_output *tmp = *pp;
      if (flag_watch)
        printf("event=output_leave output=\"%s\"\n",
               tmp->output && tmp->output->name ? tmp->output->name
                                                : "(unknown)");
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
  struct ws *w = ctx_of(workspace);
  w->group = g;
  if (flag_watch) {
    if (g->outputs && g->outputs->output && g->outputs->output->name) {
      printf("event=workspace_enter workspace=\"%s\" output=\"%s\"\n",
             w->name ? w->name : "", g->outputs->output->name);
      fflush(stdout);
    } else {
      w->pending_enter = 1;
    }
  }
}
static void group_workspace_leave(void *d,
                                  struct ext_workspace_group_handle_v1 *h,
                                  struct ext_workspace_handle_v1 *workspace) {
  (void)h;
  struct workspace_group *g = d;
  struct ws *w = ctx_of(workspace);
  if (flag_watch) {
    const char *out_name = "(unknown)";
    if (g->outputs && g->outputs->output && g->outputs->output->name)
      out_name = g->outputs->output->name;
    printf("event=workspace_leave workspace=\"%s\" output=\"%s\"\n",
           w->name ? w->name : "", out_name);
    fflush(stdout);
  }
  w->group = NULL;
  w->pending_enter = 0;
}
static void group_removed(void *d, struct ext_workspace_group_handle_v1 *h) {
  (void)h;
  struct workspace_group *g = d;
  struct workspace_group **pp = &workspace_groups;
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
  for (size_t i = 0; i < vlen; ++i)
    if (vec[i]->group == g)
      vec[i]->group = NULL;
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

/* ------------------------- manager / registry ---------------------- */

static void mgr_workspace(void *d, struct ext_workspace_manager_v1 *m,
                          struct ext_workspace_handle_v1 *h) {
  (void)d;
  (void)m;
  struct ws *w = ctx_of(h);
  ext_workspace_handle_v1_add_listener(h, &ws_listener, w);
  if (flag_watch) {
    puts("event=workspace_created");
    fflush(stdout);
  }
}
static void mgr_workspace_group(void *d, struct ext_workspace_manager_v1 *m,
                                struct ext_workspace_group_handle_v1 *h) {
  (void)d;
  (void)m;
  struct workspace_group *g = group_ctx_of(h);
  ext_workspace_group_handle_v1_add_listener(h, &group_listener, g);
  g->next = workspace_groups;
  workspace_groups = g;
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
  (void)d;
  (void)ver;
  if (strcmp(iface, "ext_workspace_manager_v1") == 0) {
    mgr = wl_registry_bind(r, name, &ext_workspace_manager_v1_interface, 1);
  } else if (strcmp(iface, "wl_output") == 0) {
    struct output *out = calloc(1, sizeof *out);
    out->output = wl_registry_bind(r, name, &wl_output_interface, 4);
    wl_output_add_listener(out->output, &out_listener, out);
    out->next = all_outputs;
    all_outputs = out;
  }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) {
  (void)d;
  (void)r;
  (void)n;
}
static const struct wl_registry_listener reg_listener = {
    .global = reg_global, .global_remove = reg_remove};

/* ---------------------- active workspace logic -------------------- */

static size_t group_size(struct workspace_group *g) {
  size_t n = 0;
  for (size_t i = 0; i < vlen; ++i)
    if (vec[i]->group == g)
      ++n;
  return n;
}
static struct ws *current_ws(size_t *out) {
  if (opt_output_name) {
    struct ws *best = NULL;
    for (size_t i = 0; i < vlen; ++i) {
      struct ws *w = vec[i];
      if (!w->active || !w->group)
        continue;
      for (struct group_output *go = w->group->outputs; go; go = go->next) {
        if (go->output && go->output->name &&
            strcmp(go->output->name, opt_output_name) == 0) {
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
  for (size_t i = 0; i < vlen; ++i) {
    struct ws *w = vec[i];
    if (!w->active || !w->group)
      continue;
    if (group_size(w->group) <= 1)
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
  for (size_t i = 0; i < vlen; ++i) {
    struct ws *w = vec[i];
    if (!w->active)
      continue;
    if (!best || w->last_active_seq > best->last_active_seq)
      best = w;
  }
  if (best && out)
    *out = best->index;
  return best;
}
static struct ws *neighbor(enum dir d) {
  struct ws *cur = current_ws(NULL);
  if (!cur || !cur->group)
    return NULL;
  struct ws *group_ws[vlen];
  size_t count = 0;
  for (size_t i = 0; i < vlen; ++i)
    if (vec[i]->group == cur->group)
      group_ws[count++] = vec[i];
  if (!count)
    return NULL;
  for (size_t i = 0; i + 1 < count; ++i)
    for (size_t j = i + 1; j < count; ++j)
      if (group_ws[i]->index > group_ws[j]->index) {
        struct ws *t = group_ws[i];
        group_ws[i] = group_ws[j];
        group_ws[j] = t;
      }
  size_t cur_pos = 0;
  while (cur_pos < count && group_ws[cur_pos] != cur)
    cur_pos++;
  if (cur_pos == count)
    return NULL;
  int x = (int)(cur_pos % grid_cols), y = (int)(cur_pos / grid_cols);
  int rows = (int)((count + grid_cols - 1) / grid_cols);
  switch (d) {
  case DIR_UP:
    if (y == 0)
      return NULL;
    --y;
    break;
  case DIR_DOWN:
    if (y >= rows - 1)
      return NULL;
    ++y;
    break;
  case DIR_LEFT:
    if (x == 0)
      return NULL;
    --x;
    break;
  case DIR_RIGHT:
    if (x >= grid_cols - 1)
      return NULL;
    ++x;
    break;
  default:
    return NULL;
  }
  size_t new_pos = (size_t)(y * grid_cols + x);
  if (new_pos >= count)
    return NULL;
  return group_ws[new_pos];
}

/* ----------------------- Waybar / JSON output --------------------- */

static void print_waybar_output(void) {
  printf("{\"text\":\"");
  int first_monitor_printed = 1;
  for (struct output *o = all_outputs; o; o = o->next) {
    if (opt_output_name && strcmp(o->name, opt_output_name) != 0)
      continue;
    struct workspace_group *current_group = NULL;
    for (struct workspace_group *g = workspace_groups; g && !current_group;
         g = g->next)
      for (struct group_output *go = g->outputs; go; go = go->next)
        if (go->output == o) {
          current_group = g;
          break;
        }
    if (!current_group)
      continue;
    if (!first_monitor_printed)
      printf("\\n");
    first_monitor_printed = 0;
    printf("%s:", o->name ? o->name : "(unknown)");
    struct ws *monitor_workspaces[vlen];
    size_t monitor_ws_count = 0;
    for (size_t i = 0; i < vlen; i++)
      if (vec[i]->group == current_group)
        monitor_workspaces[monitor_ws_count++] = vec[i];
    for (size_t i = 0; i + 1 < monitor_ws_count; i++)
      for (size_t j = i + 1; j < monitor_ws_count; j++)
        if (monitor_workspaces[i]->index > monitor_workspaces[j]->index) {
          struct ws *tmp = monitor_workspaces[i];
          monitor_workspaces[i] = monitor_workspaces[j];
          monitor_workspaces[j] = tmp;
        }
    for (size_t i = 0; i < monitor_ws_count; i++) {
      printf("%s", monitor_workspaces[i]->active ? glyph_active : glyph_empty);
      if ((i + 1) % grid_cols == 0 && i < monitor_ws_count - 1)
        printf("\\n");
      else if (i < monitor_ws_count - 1)
        printf(" ");
    }
  }
  printf("\"}\n");
  fflush(stdout);
}
static void print_json_output(void) {
  printf("[");
  int first = 1;
  for (size_t i = 0; i < vlen; i++) {
    if (!first)
      printf(",");
    first = 0;
    const char *mon = "(unknown)";
    if (vec[i]->group && vec[i]->group->outputs &&
        vec[i]->group->outputs->output && vec[i]->group->outputs->output->name)
      mon = vec[i]->group->outputs->output->name;
    printf("{\"index\":%zu,\"name\":\"%s\",\"active\":%s,\"urgent\":%s,"
           "\"hidden\":%s,"
           "\"x\":%d,\"y\":%d,\"monitor\":\"%s\",\"group_handle\":\"%p\"}",
           vec[i]->index + 1, vec[i]->name ? vec[i]->name : "",
           vec[i]->active ? "true" : "false", vec[i]->urgent ? "true" : "false",
           vec[i]->hidden ? "true" : "false", vec[i]->x, vec[i]->y, mon,
           (void *)vec[i]->group);
  }
  printf("]\n");
  fflush(stdout);
}

/* -------------------------- CLI parsing --------------------------- */

static void usage(const char *prg) {
  printf(
      "Usage: %s [options] [<index>|<name>]\n"
      "Options:\n"
      "  -l, --list           List workspaces\n"
      "  -w, --watch          Stay running and print events\n"
      "  -g, --grid N         Set grid width (default: 3)\n"
      "  -e, --exec CMD       Execute command after an event or switch\n"
      "      --waybar         Output in Waybar JSON format for a custom "
      "module\n"
      "      --json           Output in JSON format\n"
      "      --output NAME    Filter Waybar/JSON output by output name\n"
      "      --glyph-active G   Set active workspace glyph (default: \"%s\")\n"
      "      --glyph-empty G    Set empty workspace glyph (default: \"%s\")\n"
      "      --up, --down, --left, --right  Navigate workspaces relative to "
      "the active one\n"
      "      --debug-info     Print debugging information\n",
      prg, glyph_active, glyph_empty);
  exit(1);
}
static void parse_cli(int ac, char **av) {
  static struct option longopts[] = {{"list", 0, 0, 'l'},
                                     {"watch", 0, 0, 'w'},
                                     {"grid", 1, 0, 'g'},
                                     {"exec", 1, 0, 'e'},
                                     {"waybar", 0, 0, 1004},
                                     {"json", 0, 0, 1009},
                                     {"output", 1, 0, 1010},
                                     {"glyph-active", 1, 0, 1005},
                                     {"glyph-empty", 1, 0, 1006},
                                     {"up", 0, 0, 1000},
                                     {"down", 0, 0, 1001},
                                     {"left", 0, 0, 1002},
                                     {"right", 0, 0, 1003},
                                     {"debug-info", 0, 0, 1008},
                                     {0, 0, 0, 0}};
  int ch;
  while ((ch = getopt_long(ac, av, "lwg:e:", longopts, NULL)) != -1) {
    switch (ch) {
    case 'l':
      flag_list = 1;
      break;
    case 'w':
      flag_watch = 1;
      break;
    case 'g':
      grid_cols = atoi(optarg);
      if (grid_cols <= 0)
        usage(av[0]);
      break;
    case 'e':
      opt_exec = optarg;
      break;
    case 1000:
      move_dir = DIR_UP;
      break;
    case 1001:
      move_dir = DIR_DOWN;
      break;
    case 1002:
      move_dir = DIR_LEFT;
      break;
    case 1003:
      move_dir = DIR_RIGHT;
      break;
    case 1004:
      flag_waybar = 1;
      break;
    case 1009:
      flag_json = 1;
      break;
    case 1010:
      opt_output_name = optarg;
      break;
    case 1005:
      glyph_active = optarg;
      break;
    case 1006:
      glyph_empty = optarg;
      break;
    case 1008:
      flag_debug = 1;
      break;
    default:
      usage(av[0]);
    }
  }
  if (optind < ac) {
    if (move_dir != DIR_NONE)
      die("Error: Cannot combine a directional move with an index or name.\n");
    if (isnum(av[optind]))
      want_idx = atoi(av[optind]);
    else
      want_name = av[optind];
    ++optind;
  }
  if (optind != ac)
    usage(av[0]);
  int switching = (want_idx > 0) || want_name || move_dir != DIR_NONE;
  if (!flag_list && !switching && !flag_watch && !flag_waybar && !flag_json &&
      !flag_debug)
    usage(av[0]);
}

/* ------------------------------ cleanup --------------------------- */

static void cleanup(void) {
  while (all_outputs) {
    struct output *next = all_outputs->next;
    free(all_outputs->name);
    wl_output_destroy(all_outputs->output);
    free(all_outputs);
    all_outputs = next;
  }
  while (workspace_groups) {
    struct workspace_group *next_group = workspace_groups->next;
    for (struct group_output *n = workspace_groups->outputs, *next; n;
         n = next) {
      next = n->next;
      free(n);
    }
    free(workspace_groups);
    workspace_groups = next_group;
  }
  for (size_t i = 0; i < vlen; i++) {
    free(vec[i]->name);
    ext_workspace_handle_v1_destroy(vec[i]->h);
    free(vec[i]);
  }
  free(vec);
  if (mgr)
    ext_workspace_manager_v1_destroy(mgr);
  if (dpy)
    wl_display_disconnect(dpy);
}

/* --- debug printing helpers (avoid fprintf/snprintf for clang-tidy) --- */

static void dbg_append(char *buf, size_t *len, size_t cap, const char *s) {
  while (*s && *len + 1 < cap)
    buf[(*len)++] = *s++;
}
static void dbg_append_int(char *buf, size_t *len, size_t cap, int v) {
  char tmp[32];
  int tlen = 0;
  if (v == 0)
    tmp[tlen++] = '0';
  else {
    unsigned int u = (v < 0) ? (unsigned int)(-v) : (unsigned int)v;
    char rev[32];
    int rlen = 0;
    while (u) {
      rev[rlen++] = (char)('0' + (u % 10));
      u /= 10;
    }
    if (v < 0)
      tmp[tlen++] = '-';
    while (rlen--)
      tmp[tlen++] = rev[rlen];
  }
  for (int i = 0; i < tlen && *len + 1 < cap; ++i)
    buf[(*len)++] = tmp[i];
}
static void dbg_append_hexptr(char *buf, size_t *len, size_t cap, uintptr_t p) {
  dbg_append(buf, len, cap, "0x");
  char hex[2 * sizeof p];
  int hlen = 0;
  for (int i = (int)(sizeof p * 2) - 1; i >= 0; --i) {
    int nib = (int)((p >> (i * 4)) & 0xF);
    hex[hlen++] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
  }
  for (int i = 0; i < hlen && *len + 1 < cap; ++i)
    buf[(*len)++] = hex[i];
}

/* ------------------------------- main ----------------------------- */

int main(int argc, char **argv) {
  atexit(cleanup);
  parse_cli(argc, argv);

  dpy = wl_display_connect(NULL);
  if (!dpy) {
    perror("wl_display_connect");
    return 1;
  }

  struct wl_registry *reg = wl_display_get_registry(dpy);
  wl_registry_add_listener(reg, &reg_listener, NULL);
  wl_display_roundtrip(dpy);

  if (!mgr)
    die("ext-workspace-v1 unsupported\n");

  ext_workspace_manager_v1_add_listener(mgr, &mgr_listener, NULL);
  wl_display_roundtrip(dpy);

  if (flag_debug) {
    char buf[256];
    size_t len = 0;
    dbg_append(buf, &len, sizeof buf, "--- DEBUG INFO ---\nOutputs found:\n");
    buf[len] = 0;
    fputs(buf, stderr);
    len = 0;

    for (struct output *o = all_outputs; o; o = o->next) {
      dbg_append(buf, &len, sizeof buf, "  - Name: ");
      dbg_append(buf, &len, sizeof buf, o->name ? o->name : "(null)");
      dbg_append(buf, &len, sizeof buf, ", Geo: x=");
      dbg_append_int(buf, &len, sizeof buf, o->x);
      dbg_append(buf, &len, sizeof buf, ", y=");
      dbg_append_int(buf, &len, sizeof buf, o->y);
      dbg_append(buf, &len, sizeof buf, ", w=");
      dbg_append_int(buf, &len, sizeof buf, o->width);
      dbg_append(buf, &len, sizeof buf, ", h=");
      dbg_append_int(buf, &len, sizeof buf, o->height);
      dbg_append(buf, &len, sizeof buf, "\n");
      buf[len] = 0;
      fputs(buf, stderr);
      len = 0;
    }

    dbg_append(buf, &len, sizeof buf, "Workspace Groups found:\n");
    buf[len] = 0;
    fputs(buf, stderr);
    len = 0;
    for (struct workspace_group *g = workspace_groups; g; g = g->next) {
      dbg_append(buf, &len, sizeof buf, "  - Group Handle: ");
      dbg_append_hexptr(buf, &len, sizeof buf, (uintptr_t)g->h);
      dbg_append(buf, &len, sizeof buf, "\n");
      buf[len] = 0;
      fputs(buf, stderr);
      len = 0;
      for (struct group_output *go = g->outputs; go; go = go->next) {
        dbg_append(buf, &len, sizeof buf, "    - Output: ");
        dbg_append(buf, &len, sizeof buf,
                   go->output && go->output->name ? go->output->name
                                                  : "(null)");
        dbg_append(buf, &len, sizeof buf, "\n");
        buf[len] = 0;
        fputs(buf, stderr);
        len = 0;
      }
    }

    dbg_append(buf, &len, sizeof buf, "Workspaces found:\n");
    buf[len] = 0;
    fputs(buf, stderr);
    len = 0;
    for (size_t i = 0; i < vlen; i++) {
      dbg_append(buf, &len, sizeof buf, "  - Name: ");
      dbg_append(buf, &len, sizeof buf,
                 (vec[i]->name && vec[i]->name[0]) ? vec[i]->name
                                                   : "(unnamed)");
      dbg_append(buf, &len, sizeof buf, ", Index: ");
      dbg_append_int(buf, &len, sizeof buf, (int)i);
      dbg_append(buf, &len, sizeof buf, ", Coords: x=");
      dbg_append_int(buf, &len, sizeof buf, vec[i]->x);
      dbg_append(buf, &len, sizeof buf, ", y=");
      dbg_append_int(buf, &len, sizeof buf, vec[i]->y);
      dbg_append(buf, &len, sizeof buf, ", Group: ");
      dbg_append_hexptr(buf, &len, sizeof buf, (uintptr_t)vec[i]->group);
      dbg_append(buf, &len, sizeof buf, "\n");
      buf[len] = 0;
      fputs(buf, stderr);
      len = 0;
    }
    dbg_append(buf, &len, sizeof buf, "------------------\n");
    buf[len] = 0;
    fputs(buf, stderr);
    len = 0;
  }

  if (flag_list) {
    for (size_t i = 0; i < vlen; i++) {
      const char *out_name = "(unknown)";
      if (vec[i]->group && vec[i]->group->outputs &&
          vec[i]->group->outputs->output &&
          vec[i]->group->outputs->output->name)
        out_name = vec[i]->group->outputs->output->name;
      printf("%2zu  %-15s %-10s %s\n", i + 1, out_name,
             (vec[i]->name && vec[i]->name[0]) ? vec[i]->name : "(unnamed)",
             vec[i]->active ? "*" : "");
    }
    if (!flag_watch)
      return 0;
  }
  if (flag_waybar) {
    print_waybar_output();
    if (!flag_watch)
      return 0;
  }
  if (flag_json) {
    print_json_output();
    if (!flag_watch)
      return 0;
  }

  struct ws *target = NULL;
  if (move_dir != DIR_NONE) {
    target = neighbor(move_dir);
  } else if (want_name) {
    for (size_t i = 0; i < vlen; i++)
      if (vec[i]->name && strcmp(vec[i]->name, want_name) == 0) {
        target = vec[i];
        break;
      }
  } else if (want_idx > 0) {
    if ((size_t)want_idx <= vlen)
      target = vec[want_idx - 1];
  }

  if ((want_idx > 0 || want_name || move_dir != DIR_NONE) && !target)
    die("workspace not found / edge\n");

  if (target) {
    ext_workspace_handle_v1_activate(target->h);
    ext_workspace_manager_v1_commit(mgr);
    wl_display_flush(dpy);
    if (opt_exec)
      system(opt_exec);
  }

  if (flag_watch)
    while (wl_display_dispatch(dpy) != -1)
      ;

  return 0;
}
