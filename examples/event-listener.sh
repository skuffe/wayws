#!/bin/bash

# Example event listener for wayws enhanced event system
# This script demonstrates how to integrate with wayws events

echo "Starting wayws event listener..."
echo "Listening for workspace events..."

# Start wayws in watch mode
./wayws -w | while read -r event; do
    # Parse the JSON event
    event_type=$(echo "$event" | jq -r '.type' 2>/dev/null)
    
    case $event_type in
        "workspace_created")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            output=$(echo "$event" | jq -r '.workspace.output')
            echo "Created workspace '$workspace' on output '$output'"
            ;;
            
        "workspace_enter")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            output=$(echo "$event" | jq -r '.workspace.output')
            active=$(echo "$event" | jq -r '.workspace.active')
            echo "Workspace '$workspace' entered output '$output' (active: $active)"
            ;;
            
        "workspace_leave")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            output=$(echo "$event" | jq -r '.workspace.output')
            echo "Workspace '$workspace' left output '$output'"
            ;;
            
        "workspace_activated")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            output=$(echo "$event" | jq -r '.workspace.output')
            echo "Activated workspace '$workspace' on output '$output'"
            
            # Example: Change wallpaper based on workspace
            if [ -f "$HOME/wallpapers/$workspace.jpg" ]; then
                feh --bg-fill "$HOME/wallpapers/$workspace.jpg" &
                echo "   Changed wallpaper to $workspace.jpg"
            fi
            ;;
            
        "workspace_name")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            echo "Workspace renamed to: '$workspace'"
            ;;
            
        "workspace_coordinates")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            x=$(echo "$event" | jq -r '.workspace.x')
            y=$(echo "$event" | jq -r '.workspace.y')
            echo "Workspace '$workspace' moved to coordinates ($x, $y)"
            ;;
            
        "output_enter")
            output=$(echo "$event" | jq -r '.workspace.output')
            echo "Output '$output' connected"
            ;;
            
        "output_leave")
            output=$(echo "$event" | jq -r '.workspace.output')
            echo "Output '$output' disconnected"
            ;;
            
        "workspace_state")
            workspace=$(echo "$event" | jq -r '.workspace.name')
            active=$(echo "$event" | jq -r '.workspace.active')
            urgent=$(echo "$event" | jq -r '.workspace.urgent')
            
            if [ "$urgent" = "true" ]; then
                echo "Workspace '$workspace' is urgent!"
                # Flash notification
                dunstify -u critical "Urgent workspace: $workspace"
            fi
            
            if [ "$active" = "true" ]; then
                echo "   Workspace '$workspace' is now active"
            else
                echo "   Workspace '$workspace' is now inactive"
            fi
            ;;
            
        "grid_movement")
            direction=$(echo "$event" | jq -r '.direction')
            workspace=$(echo "$event" | jq -r '.workspace.name')
            echo "Grid movement: $direction -> workspace '$workspace'"
            
            # Example: Play sound effect for movement
            if command -v paplay >/dev/null 2>&1; then
                paplay /usr/share/sounds/freedesktop/stereo/complete.oga &
            fi
            ;;
            
        *)
            echo "Unknown event: $event"
            ;;
    esac
done 