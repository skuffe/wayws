#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "event.h"
#include "util.h"

// Event emission function
void emit_event(struct wayws_state *state, wayws_event_type_t type,
                const char *workspace_name, const char *output_name,
                int workspace_index, int x, int y, int active, int urgent, int hidden,
                enum dir direction, void *additional_data) {
    // Execute command if specified (only for grid movement events)
    if (state->opt_exec && type == EVENT_GRID_MOVEMENT) {
        system(state->opt_exec);
    }
    
    if (!state->event_enabled && !state->event_callback)
        return;
    
    wayws_event_t event = {
        .type = type,
        .workspace_name = workspace_name ? workspace_name : "",
        .output_name = output_name ? output_name : "",
        .workspace_index = workspace_index,
        .x = x,
        .y = y,
        .active = active,
        .urgent = urgent,
        .hidden = hidden,
        .direction = direction,
        .timestamp = (unsigned long)time(NULL),
        .additional_data = additional_data
    };
    
    // JSON format output
    if (state->event_enabled) {
        printf("{\"type\":\"%s\",\"workspace\":{\"name\":\"%s\",\"index\":%d,\"output\":\"%s\",\"x\":%d,\"y\":%d,\"active\":%s,\"urgent\":%s,\"hidden\":%s},\"timestamp\":%lu",
               type == EVENT_WORKSPACE_CREATED ? "workspace_created" :
               type == EVENT_WORKSPACE_DESTROYED ? "workspace_destroyed" :
               type == EVENT_WORKSPACE_ID ? "workspace_id" :
               type == EVENT_WORKSPACE_NAME ? "workspace_name" :
               type == EVENT_WORKSPACE_COORDINATES ? "workspace_coordinates" :
               type == EVENT_WORKSPACE_CAPABILITIES ? "workspace_capabilities" :
               type == EVENT_WORKSPACE_STATE ? "workspace_state" :
               type == EVENT_GROUP_CAPABILITIES ? "group_capabilities" :
               type == EVENT_GROUP_REMOVED ? "group_removed" :
               type == EVENT_WORKSPACE_ENTER ? "workspace_enter" :
               type == EVENT_WORKSPACE_LEAVE ? "workspace_leave" :
               type == EVENT_OUTPUT_ENTER ? "output_enter" :
               type == EVENT_OUTPUT_LEAVE ? "output_leave" :
               type == EVENT_WORKSPACE_ACTIVATED ? "workspace_activated" :
               type == EVENT_WORKSPACE_DEACTIVATED ? "workspace_deactivated" :
               type == EVENT_GRID_MOVEMENT ? "grid_movement" : "unknown",
               workspace_name ? workspace_name : "", workspace_index, 
               output_name ? output_name : "", x, y,
               active ? "true" : "false", urgent ? "true" : "false", hidden ? "true" : "false",
               event.timestamp);
        
        if (type == EVENT_GRID_MOVEMENT) {
            printf(",\"direction\":\"%s\"",
                   direction == DIR_UP ? "up" : direction == DIR_DOWN ? "down" : 
                   direction == DIR_LEFT ? "left" : "right");
        }
        printf("}\n");
        fflush(stdout);
    }
    
    // Call custom event callback if provided
    if (state->event_callback) {
        state->event_callback(&event, state->event_user_data);
    }
}

// Helper function to get output name for a workspace
const char *get_output_name_for_workspace(struct ws *w) {
    if (!w || !w->group || !w->group->outputs || !w->group->outputs->output)
        return "(unknown)";
    return w->group->outputs->output->name ? w->group->outputs->output->name : "(unknown)";
}

// Add a pending event to the queue
void add_pending_event(struct wayws_state *state, wayws_event_type_t type,
                      struct ws *workspace, int x, int y, int active, int urgent, int hidden,
                      enum dir direction) {
    struct pending_event *pending = calloc(1, sizeof(*pending));
    if (!pending) return;
    
    pending->type = type;
    pending->workspace = workspace;
    pending->x = x;
    pending->y = y;
    pending->active = active;
    pending->urgent = urgent;
    pending->hidden = hidden;
    pending->direction = direction;
    
    // Add to front of list
    pending->next = state->pending_events;
    state->pending_events = pending;
}

// Emit all pending events for a workspace when output becomes available
void emit_pending_events_for_workspace(struct wayws_state *state, struct ws *workspace) {
    if (!workspace || !workspace->group || !workspace->group->outputs || 
        !workspace->group->outputs->output || !workspace->group->outputs->output->name) {
        return;
    }
    
    const char *output_name = workspace->group->outputs->output->name;
    struct pending_event **pp = &state->pending_events;
    
    while (*pp) {
        struct pending_event *pending = *pp;
        if (pending->workspace == workspace) {
            // Emit the pending event with correct output name
            emit_event(state, pending->type, 
                      pending->workspace->name ? pending->workspace->name : "",
                      output_name, pending->workspace->index + 1,
                      pending->x, pending->y, pending->active, pending->urgent, pending->hidden,
                      pending->direction, NULL);
            
            // Remove from list
            *pp = pending->next;
            free(pending);
        } else {
            pp = &pending->next;
        }
    }
}

// Clean up pending events for a workspace when it's destroyed
void cleanup_pending_events_for_workspace(struct wayws_state *state, struct ws *workspace) {
    struct pending_event **pp = &state->pending_events;
    
    while (*pp) {
        struct pending_event *pending = *pp;
        if (pending->workspace == workspace) {
            // Remove from list
            *pp = pending->next;
            free(pending);
        } else {
            pp = &pending->next;
        }
    }
}

// Clean up all pending events (for program shutdown)
void cleanup_all_pending_events(struct wayws_state *state) {
    struct pending_event *pending = state->pending_events;
    while (pending) {
        struct pending_event *next = pending->next;
        free(pending);
        pending = next;
    }
    state->pending_events = NULL;
} 