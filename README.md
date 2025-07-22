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

You need a C toolchain plus `wayland-scanner` and Wayland client headers.

```sh
make            # builds wayws
sudo make install  # optional
```

The Makefile uses `zig cc` (drop-in C compiler) by default; set `CC` if you prefer another compiler.

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

`wayws -w` stays running and prints events as they arrive. Example:

```
event=workspace_created
event=state name="1" x=0 y=0 active=1 urgent=0 hidden=0
event=output_enter output="DP-1"
event=workspace_enter workspace="1" output="DP-1"
```

Events emitted:

* `workspace_created`
* `name` (old/new workspace name) – initial empty→name rename suppressed
* `state` (active/urgent/hidden + coordinates)
* `workspace_enter` / `workspace_leave` (association with an output)
* `output_enter` / `output_leave` (group membership changes)
* `workspace_removed`

`--exec CMD` runs *after* each emitted event (and after activations) which makes it easy to trigger bar refreshes, etc.

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