#ifndef TYPES_H
#define TYPES_H

#include "ext_workspace_client.h"
#include <stdbool.h>
#include <wayland-client.h>

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

enum dir { DIR_NONE, DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT };

// Event types for enhanced event system
typedef enum {
    // Core workspace events (from protocol)
    EVENT_WORKSPACE_CREATED,      // workspace event from manager
    EVENT_WORKSPACE_DESTROYED,    // removed event from workspace
    EVENT_WORKSPACE_ID,           // id event from workspace
    EVENT_WORKSPACE_NAME,         // name event from workspace
    EVENT_WORKSPACE_COORDINATES,  // coordinates event from workspace
    EVENT_WORKSPACE_CAPABILITIES, // capabilities event from workspace
    EVENT_WORKSPACE_STATE,        // state event from workspace
    
    // Workspace group events (from protocol)
    EVENT_GROUP_CAPABILITIES,     // capabilities event from group
    EVENT_GROUP_REMOVED,          // removed event from group
    EVENT_WORKSPACE_ENTER,        // workspace_enter event from group
    EVENT_WORKSPACE_LEAVE,        // workspace_leave event from group
    EVENT_OUTPUT_ENTER,           // output_enter event from group
    EVENT_OUTPUT_LEAVE,           // output_leave event from group
    

} wayws_event_type_t;

// Event structure
typedef struct {
    wayws_event_type_t type;
    const char *workspace_name;
    const char *output_name;
    int workspace_index;
    int x, y;
    int active, urgent, hidden;
    enum dir direction;
    unsigned long timestamp;
    void *additional_data;
} wayws_event_t;

// Event callback function type
typedef void (*wayws_event_callback)(const wayws_event_t *event, void *user_data);

struct wayws_state {
  // Wayland objects
  struct wl_display *dpy;
  struct ext_workspace_manager_v1 *mgr;

  // Application state
  unsigned long active_seq;
  struct ws **vec;
  size_t vlen, vcap;
  struct output *all_outputs;
  struct workspace_group *workspace_groups;

  // CLI flags
  int flag_list;
  int flag_watch;
  int flag_waybar;
  int flag_json;
  int flag_debug;
  char *opt_exec;
  char *opt_output_name;
  char *glyph_active;
  char *glyph_empty;
  int want_idx;
  char *want_name;
  enum dir move_dir;
  int grid_cols;
  
  // Enhanced event system
  wayws_event_callback event_callback;
  void *event_user_data;
  int event_enabled;  // 0=disabled, 1=enabled
  
  // Pending events for deferred emission
  struct pending_event {
    wayws_event_type_t type;
    struct ws *workspace;
    int x, y;
    int active, urgent, hidden;
    enum dir direction;
    struct pending_event *next;
  } *pending_events;
};

#endif // TYPES_H
