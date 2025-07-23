#include "output.h"
#include "types.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_waybar_output(struct wayws_state *state) {
  // Check if we have multiple outputs and no specific output is specified
  if (!state->opt_output_name) {
    int output_count = 0;
    for (struct output *o = state->all_outputs; o; o = o->next) {
      output_count++;
    }
    if (output_count > 1) {
      die("Error: Multiple outputs detected. Use --output to specify which output to use with --waybar.\n");
    }
  }

  printf("{\"text\":\"");
  int first_monitor_printed = 1;
  for (struct output *o = state->all_outputs; o; o = o->next) {
    if (state->opt_output_name && strcmp(o->name, state->opt_output_name) != 0)
      continue;
    struct workspace_group *current_group = NULL;
    for (struct workspace_group *g = state->workspace_groups; g && !current_group;
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
    
    // Count workspaces for this monitor first
    size_t monitor_ws_count = 0;
    for (size_t i = 0; i < state->vlen; i++) {
      if (state->vec[i]->group == current_group)
        monitor_ws_count++;
    }
    
    // Allocate array on heap to avoid stack overflow
    if (monitor_ws_count == 0) continue;
    struct ws **monitor_workspaces = malloc(monitor_ws_count * sizeof(struct ws *));
    if (!monitor_workspaces) continue;
    
    // Fill the array
    size_t idx = 0;
    for (size_t i = 0; i < state->vlen; i++) {
      if (state->vec[i]->group == current_group)
        monitor_workspaces[idx++] = state->vec[i];
    }
    for (size_t i = 0; i + 1 < monitor_ws_count; i++)
      for (size_t j = i + 1; j < monitor_ws_count; j++)
        if (monitor_workspaces[i]->index > monitor_workspaces[j]->index) {
          struct ws *tmp = monitor_workspaces[i];
          monitor_workspaces[i] = monitor_workspaces[j];
          monitor_workspaces[j] = tmp;
        }
    for (size_t i = 0; i < monitor_ws_count; i++) {
      printf("%s", monitor_workspaces[i]->active ? state->glyph_active : state->glyph_empty);
      if ((i + 1) % state->grid_cols == 0 && i < monitor_ws_count - 1)
        printf("\\n");
      else if (i < monitor_ws_count - 1)
        printf(" ");
    }
    free(monitor_workspaces);
  }
  printf("\"}\n");
  fflush(stdout);
}
void print_json_output(struct wayws_state *state) {
  printf("[");
  int first = 1;
  for (size_t i = 0; i < state->vlen; i++) {
    if (!first)
      printf(",");
    first = 0;
    const char *mon = "(unknown)";
    if (state->vec[i]->group && state->vec[i]->group->outputs &&
        state->vec[i]->group->outputs->output && state->vec[i]->group->outputs->output->name)
      mon = state->vec[i]->group->outputs->output->name;
    printf("{\"index\":%zu,\"name\":\"%s\",\"active\":%s,\"urgent\":%s,"
           "\"hidden\":%s,"
           "\"x\":%d,\"y\":%d,\"monitor\":\"%s\",\"group_handle\":\"%p\"}",
           state->vec[i]->index + 1, state->vec[i]->name ? state->vec[i]->name : "",
           state->vec[i]->active ? "true" : "false", state->vec[i]->urgent ? "true" : "false",
           state->vec[i]->hidden ? "true" : "false", state->vec[i]->x, state->vec[i]->y, mon,
           (void *)state->vec[i]->group);
  }
  printf("]\n");
  fflush(stdout);
}