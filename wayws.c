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
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "ext_workspace_client.h"
#include "output.h"
#include "types.h"
#include "util.h"
#include "wayland.h"
#include "workspace.h"
#include "event.h"

static void usage(const struct wayws_state *state, const char *prg) {
  printf("Usage: %s [options] [<index>|<name>]\n\n"
         "Options:\n"
         "  -l, --list           List workspaces\n"
         "  -w, --watch          Stay running and print JSON events\n"
         "  -g, --grid N         Set grid width (default: 3)\n"
         "  -e, --exec CMD       Execute command after an event or switch\n"
         "      --waybar         Output in Waybar JSON format\n"
         "      --json           Output in raw JSON format\n"
         "      --output NAME    Filter output by output name\n"
         "      --glyph-active G Set active workspace glyph (default: %s)\n"
         "      --glyph-empty G  Set empty workspace glyph (default: %s)\n"
         "      --up, --down, --left, --right  Navigate workspaces\n"
         "      --debug-info     Print debugging information\n",
         prg, state->glyph_active, state->glyph_empty);
  exit(1);
}

static void parse_cli(struct wayws_state *state, int ac, char **av) {
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
      state->flag_list = 1;
      break;
    case 'w':
      state->flag_watch = 1;
      state->event_enabled = 1;  // Enable events when watch mode is on
      break;
    case 'g':
      state->grid_cols = atoi(optarg);
      if (state->grid_cols <= 0)
        usage(state, av[0]);
      break;
    case 'e':
      state->opt_exec = optarg;
      break;
    case 1000:
      state->move_dir = DIR_UP;
      break;
    case 1001:
      state->move_dir = DIR_DOWN;
      break;
    case 1002:
      state->move_dir = DIR_LEFT;
      break;
    case 1003:
      state->move_dir = DIR_RIGHT;
      break;
    case 1004:
      state->flag_waybar = 1;
      break;
    case 1009:
      state->flag_json = 1;
      break;
    case 1010:
      state->opt_output_name = optarg;
      break;
    case 1005:
      state->glyph_active = optarg;
      break;
    case 1006:
      state->glyph_empty = optarg;
      break;
    case 1008:
      state->flag_debug = 1;
      break;
    default:
      usage(state, av[0]);
    }
  }
  if (optind < ac) {
    if (state->move_dir != DIR_NONE)
      die("Error: Cannot combine a directional move with an index or name.\n");
    if (isnum(av[optind]))
      state->want_idx = atoi(av[optind]);
    else
      state->want_name = av[optind];
    ++optind;
  }
  if (optind != ac)
    usage(state, av[0]);
  int switching =
      (state->want_idx > 0) || state->want_name || state->move_dir != DIR_NONE;
  if (!state->flag_list && !switching && !state->flag_watch &&
      !state->flag_waybar && !state->flag_json && !state->flag_debug)
    usage(state, av[0]);
}

static void cleanup(struct wayws_state *state) {
  // Clean up workspaces - only free our wrapper objects, not the wayland objects
  if (state->vec) {
    for (size_t i = 0; i < state->vlen; i++) {
      if (state->vec[i]) {
        free(state->vec[i]->name);
        // Don't free the wayland object, let wayland handle it
        free(state->vec[i]);
      }
    }
    free(state->vec);
    state->vec = NULL;
    state->vlen = 0;
    state->vcap = 0;
  }

  // Clean up outputs - only free our wrapper objects
  while (state->all_outputs) {
    struct output *next = state->all_outputs->next;
    free(state->all_outputs->name);
    // Don't destroy the wayland output, let wayland handle it
    free(state->all_outputs);
    state->all_outputs = next;
  }

  // Clean up workspace groups - only free our wrapper objects
  while (state->workspace_groups) {
    struct workspace_group *next_group = state->workspace_groups->next;
    for (struct group_output *n = state->workspace_groups->outputs, *next;
         n;
         n = next) {
      next = n->next;
      free(n);
    }
    // Don't destroy the wayland group, let wayland handle it
    free(state->workspace_groups);
    state->workspace_groups = next_group;
  }

  wayland_destroy(state);
}

static struct wayws_state *g_state;
static volatile sig_atomic_t g_interrupted = 0;

static void signal_handler(int sig) {
  (void)sig;
  g_interrupted = 1;
}

// Don't use atexit cleanup - let wayland handle its own cleanup
static void global_cleanup(void) {
  // Disabled to avoid segfaults
  // if (g_state)
  //   cleanup(g_state);
}

static void print_debug_info(struct wayws_state *state) {
  printf("--- DEBUG INFO ---\nOutputs found:\n");
  for (struct output *o = state->all_outputs; o; o = o->next) {
    printf("  - Name: %s, Geo: x=%d, y=%d, w=%d, h=%d\n",
           o->name ? o->name : "(null)", o->x, o->y, o->width, o->height);
  }

  printf("Workspace Groups found:\n");
  for (struct workspace_group *g = state->workspace_groups; g; g = g->next) {
    printf("  - Group: %p (Wayland Handle: %p)\n", (void *)g, (void *)g->h);
    for (struct group_output *go = g->outputs; go; go = go->next) {
      printf("    - Output: %s\n",
             go->output && go->output->name ? go->output->name : "(null)");
    }
  }

  printf("Workspaces found:\n");
  if (!state->vec) {
    printf("  (vec is NULL)\n");
    return;
  }
  for (size_t i = 0; i < state->vlen; i++) {
    printf("  - Name: %s, Index: %zu, Coords: x=%d, y=%d, Group: %p, Active: %d, LastSeq: %lu\n",
           (state->vec[i]->name && state->vec[i]->name[0]) ? state->vec[i]->name
                                                           : "(unnamed)",
           i, state->vec[i]->x, state->vec[i]->y, (void *)state->vec[i]->group, 
           state->vec[i]->active, state->vec[i]->last_active_seq);
  }
  
  // Test current workspace detection
  struct ws *current = current_ws(state, NULL);
  if (current) {
    printf("Current workspace: %s (index %zu, group %p)\n", 
           current->name ? current->name : "(unnamed)", current->index, (void *)current->group);
  } else {
    printf("No current workspace found!\n");
  }
  
  printf("------------------\n");
}

static struct ws *find_target_workspace(struct wayws_state *state) {
  if (state->move_dir != DIR_NONE) {
    return neighbor(state, state->move_dir);
  }
  if (state->want_name) {
    if (!state->vec || state->vlen == 0) {
      return NULL;
    }
    for (size_t i = 0; i < state->vlen; i++)
      if (state->vec[i]->name &&
          strcmp(state->vec[i]->name, state->want_name) == 0)
        return state->vec[i];
  }
  if (state->want_idx > 0) {
    if (!state->vec || state->vlen == 0) {
      return NULL;
    }
    if ((size_t)state->want_idx <= state->vlen)
      return state->vec[state->want_idx - 1];
  }
  return NULL;
}

static void activate_workspace(struct wayws_state *state, struct ws *target) {
  // Get current workspace before activation
  struct ws *current = current_ws(state, NULL);
  
  ext_workspace_handle_v1_activate(target->h);
  ext_workspace_manager_v1_commit(state->mgr);
  wl_display_flush(state->dpy);
  
  // Emit grid movement event if we moved from one workspace to another
  if (current && current != target && state->move_dir != DIR_NONE) {
    const char *output_name = get_output_name_for_workspace(target);
    emit_event(state, EVENT_GRID_MOVEMENT, 
               target->name, output_name, target->index + 1,
               target->x, target->y, target->active, target->urgent, target->hidden,
               state->move_dir, NULL);
  } else if (state->opt_exec) {
    // Execute command for regular workspace activations (when no event is emitted)
    system(state->opt_exec);
  }
}

int main(int argc, char **argv) {
  struct wayws_state state = {
      .glyph_active = "●",
      .glyph_empty = "○",
      .grid_cols = 3,
      .want_idx = -1,
      .event_enabled = 0,  // Events disabled by default
  };
  g_state = &state;
  atexit(global_cleanup);
  
  // Set up signal handling for clean exit
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  parse_cli(&state, argc, argv);
  
  wayland_set_global_state(&state);
  wayland_init(&state);

  if (state.flag_debug) {
    print_debug_info(&state);
  }

  if (state.flag_list) {
    if (!state.vec || state.vlen == 0)
      die("No workspaces found to list.\n");
    else {
      for (size_t i = 0; i < state.vlen; i++) {
        const char *out_name = "(unknown)";
        if (state.vec[i]->group && state.vec[i]->group->outputs &&
            state.vec[i]->group->outputs->output &&
            state.vec[i]->group->outputs->output->name)
          out_name = state.vec[i]->group->outputs->output->name;
        printf("%2zu  %-15s %-10s %s\n", i + 1, out_name,
               (state.vec[i]->name && state.vec[i]->name[0]) ? state.vec[i]->name
                                                             : "(unnamed)",
               state.vec[i]->active ? "*" : "");
      }
    }
  }

  if (state.flag_waybar) {
    if (!state.vec || state.vlen == 0)
      die("No workspaces found for Waybar output.\n");
    print_waybar_output(&state);
  }

  if (state.flag_json) {
    if (!state.vec || state.vlen == 0)
      die("No workspaces found for JSON output.\n");
    print_json_output(&state);
  }

  struct ws *target = find_target_workspace(&state);
  if (target) {
    activate_workspace(&state, target);
  } else if (state.want_idx > 0 || state.want_name ||
             state.move_dir != DIR_NONE) {
    die("workspace not found / edge\n");
  }

  if (state.flag_watch) {
    while (!g_interrupted) {
      // Prepare to read events
      if (wl_display_prepare_read(state.dpy) != 0) {
        // Events are pending, dispatch them
        wl_display_dispatch_pending(state.dpy);
        continue;
      }
      
      // Set up poll to wait for events with timeout
      struct pollfd pfd = {
        .fd = wl_display_get_fd(state.dpy),
        .events = POLLIN,
      };
      
      // Poll with 100ms timeout to allow signal checking
      int poll_ret = poll(&pfd, 1, 100);
      if (poll_ret == -1) {
        // Poll error (likely interrupted by signal)
        wl_display_cancel_read(state.dpy);
        break;
      } else if (poll_ret == 0) {
        // Timeout - check if we should exit
        wl_display_cancel_read(state.dpy);
        continue;
      }
      
      // Read events
      int ret = wl_display_read_events(state.dpy);
      if (ret != 0) {
        // Error occurred, break
        break;
      }
      
      // Dispatch any pending events
      wl_display_dispatch_pending(state.dpy);
    }
  }

  return 0;
}
