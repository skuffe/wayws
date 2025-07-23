#ifndef EVENT_H
#define EVENT_H

#include "types.h"

// Event emission function
void emit_event(struct wayws_state *state, wayws_event_type_t type,
                const char *workspace_name, const char *output_name,
                int workspace_index, int x, int y, int active, int urgent, int hidden,
                enum dir direction, void *additional_data);

// Helper function to get output name for a workspace
const char *get_output_name_for_workspace(struct ws *w);

// Pending event management functions
void add_pending_event(struct wayws_state *state, wayws_event_type_t type,
                      struct ws *workspace, int x, int y, int active, int urgent, int hidden,
                      enum dir direction);
void emit_pending_events_for_workspace(struct wayws_state *state, struct ws *workspace);
void cleanup_pending_events_for_workspace(struct wayws_state *state, struct ws *workspace);
void cleanup_all_pending_events(struct wayws_state *state);

#endif // EVENT_H 