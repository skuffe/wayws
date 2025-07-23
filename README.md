# wayws – ext-workspace-v1 helper

`wayws` is a small command-line utility for Wayland compositors that implement the **ext-workspace-v1** protocol (e.g. *labwc*, *Hyprland*). It can list and activate workspaces, stream events for scripting, and output status-bar friendly JSON (Waybar/custom).

---

## Features

* **List** workspaces with index, output name and active flag.
* **Activate** a workspace by global numeric index, by name, or **directionally** (grid navigation) relative to the current one.
* **Watch** and print live protocol events (`-w`).
* **Waybar / JSON** output for custom modules.
* Per-output **grid width** configuration (`--grid N`).
* Optional `--exec <CMD>` hook run after each event / activation.

---

## Build

You need a C compiler like `gcc` or `clang`, plus `wayland-scanner` and the Wayland client headers.

```sh
make            # builds wayws
sudo make install  # optional
```

---

## Testing

The test suite uses the `cmocka` library and includes both unit tests and integration tests.

```sh
make test           # runs all tests (unit + integration)
make test-unit      # runs only unit tests
make test-integration # runs only integration tests
```

### Test Coverage

- **Unit Tests**: Test individual functions and modules
  - `test_util`: Utility functions (`isnum`, `xstrdup`, `xrealloc`)
  - `test_workspace`: Workspace management logic
  - `test_event`: Event system functionality
  - `test_cli`: CLI parsing and utility functions

- **Integration Tests**: Test the complete application behavior
  - CLI argument parsing and validation
  - Output format generation
  - Error handling
  - Event system integration

### TODO: Missing Tests

The following modules currently lack unit tests:
- `output.c`: Output formatting functions (requires stdout capture)
- `wayland.c`: Wayland protocol handling (requires Wayland connection mocking)
- `wayws.c`: Main application logic (requires integration testing approach)

---

## Dependencies

On Arch Linux and derivatives, you can install the necessary packages using `pacman`:

```sh
sudo pacman -S gcc wayland wayland-protocols cmocka
```

---

## Usage

```
Usage: wayws [options] [<index>|<name>]
Options:
  -l, --list           List workspaces
  -w, --watch          Stay running and print events
  -g, --grid N         Set grid width (default: 3)
  -e, --exec CMD       Execute command after an event or switch
      --waybar         Output in Waybar JSON format for a custom module
      --json           Output in JSON format
      --output NAME    Filter Waybar/JSON output by output name
      --glyph-active G   Set active workspace glyph (default: "●")
      --glyph-empty G    Set empty workspace glyph (default: "○")
      --up, --down, --left, --right  Navigate workspaces relative to the active one
      --debug-info     Print debugging information
```

### Listing

```sh
wayws -l
# Example output:
#  1  DP-1            code       *
#  2  DP-2            web        *
#  3  DP-1            3
```

Indexes are **global discovery order**. The output column shows the first output in each workspace’s group.

### Activating

* By index: `wayws 3`
* By name: `wayws code`
* By direction: `wayws --right` (also `--left`, `--up`, `--down`)

### Waybar / JSON

Plain Waybar text JSON:

```sh
wayws --waybar
```

This prints a multi-line grid per output (separated by `\n` inside the JSON string). Use `--output NAME` to restrict to a single monitor.

Raw JSON suitable for ad-hoc parsing:

```sh
wayws --json
```

Each object includes `index`, `name`, `active`, `urgent`, `hidden`, `x`, `y`, `monitor`, and `group_handle` fields.

### Watch mode / Events

`wayws -w` stays running and prints JSON events as they arrive. The event system provides structured, machine-readable JSON events that can be easily integrated with other tools and scripts.

#### Event Format

All events are emitted in clean JSON format:

```json
{"type":"workspace_created","workspace":{"name":"1","index":1,"output":"DP-1","x":0,"y":0,"active":true,"urgent":false,"hidden":false},"timestamp":1703123456}
{"type":"workspace_state","workspace":{"name":"1","index":1,"output":"DP-1","x":0,"y":0,"active":true,"urgent":false,"hidden":false},"timestamp":1703123457}
{"type":"workspace_enter","workspace":{"name":"1","index":1,"output":"DP-1","x":0,"y":0,"active":true,"urgent":false,"hidden":false},"timestamp":1703123458}
```

#### Event Types

**Protocol Events** (from ext-workspace-v1):
* `workspace_created` - When a workspace is created
* `workspace_destroyed` - When a workspace is removed
* `workspace_name` - When workspace name changes
* `workspace_coordinates` - When workspace coordinates change
* `workspace_state` - When workspace state changes (active/urgent/hidden)
* `workspace_enter` / `workspace_leave` - When workspaces enter/leave groups
* `output_enter` / `output_leave` - When outputs enter/leave groups



#### Event Integration Examples

**Status Bar Updates**:
```bash
./wayws -w | while read -r event; do
    if echo "$event" | jq -e '.type == "workspace_state" and .workspace.active == true' >/dev/null; then
        pkill -RTMIN+1 waybar
    fi
done
```

**Notification System**:
```bash
./wayws -w | while read -r event; do
    if echo "$event" | jq -e '.workspace.urgent == true' >/dev/null; then
        workspace=$(echo "$event" | jq -r '.workspace.name')
        dunstify -u critical "Urgent workspace: $workspace"
    fi
done
```

**Sound Effects**:
```bash
./wayws -w | while read -r event; do
    if echo "$event" | jq -e '.type == "workspace_state" and .workspace.active == true' >/dev/null; then
        paplay /usr/share/sounds/freedesktop/stereo/complete.oga
    fi
done
```

**Custom Event Handler**:
```bash
#!/bin/bash
./wayws -w | while read -r event; do
    case $(echo "$event" | jq -r '.type') in
        "workspace_state")
            if [ "$(echo "$event" | jq -r '.workspace.active')" = "true" ]; then
                workspace=$(echo "$event" | jq -r '.workspace.name')
                output=$(echo "$event" | jq -r '.workspace.output')
                echo "Switched to workspace $workspace on $output"
            fi
            ;;

    esac
done
```

`--exec CMD` runs *after* each emitted event (and after activations) which makes it easy to trigger bar refreshes, etc.

See `examples/event-listener.sh` for a complete example.



---

## Directional Movement

Each output has its own grid (width `--grid N`). To navigate, `wayws` needs to determine the current workspace. It does so by:

1.  Using the active workspace on the output specified with `--output NAME`.
2.  If no output is specified, it prefers an active workspace on an output with more than one workspace.
3.  Otherwise, it falls back to the most recently activated workspace.

---

## Initial State

At startup, `wayws` performs two Wayland roundtrips to ensure it has a complete and up-to-date model of the workspace layout before executing any commands. This guarantees that one-shot commands work correctly without needing to use the watch mode.

---

## Encoding / Glyphs

The default glyphs are UTF-8 “●” and “○”. You can override them with `--glyph-active` and `--glyph-empty` if your font lacks the symbols.

---

## Exit Codes

* `0` on success.
* Non-zero on errors.

---

## License

MIT. See the source code for details.

---

## Contributing

Pull requests and issues are welcome.
