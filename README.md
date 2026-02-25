# flick

A keyboard-driven cursor with physics-based animations for macOS.

Built on [warpd](https://github.com/rvaiya/warpd) by Raheman Vaiya.

## Visual Effects

flick adds 15 visual effects on top of warpd's modal cursor system:

| Effect | Description |
|--------|-------------|
| Radial gradient glow | Soft halo around the cursor with a gentle pulse |
| Motion stretch | Cursor deforms into an ellipse along the velocity vector |
| Spring overshoot | Elastic bounce when teleporting via hints or grid |
| Comet trail | Ring buffer of 5 trailing dots behind the cursor |
| Speed lines | Directional streaks on fast acceleration |
| Click squish | Cursor inflates then deflates on mouse click |
| Gravity wave ripple | Concentric rings expand outward from click point |
| Mode flash | Expanding ring burst on state transitions |
| Drag visual | Color shift + sentinel ring + faster pulse while dragging |
| Scroll visual | Color change to teal, cursor stays visible during scroll |
| Adaptive contrast | Auto-switches light/dark cursor based on background luminance |
| Hint cascade | Hints ripple outward from cursor with staggered pop-in |
| Hint border glow | 1px stroke at 25% alpha around each hint label |
| History ghost dots | Translucent circles at past cursor positions |
| Screen edge pulse | Visual feedback when the cursor reaches a screen boundary |

## Demo

<p align="center">
<img src="demo/overall.gif" width="720px"/>
</p>

<p align="center">
<img src="demo/cursor_physics.gif" width="720px"/>
</p>

## Installation (macOS)

Build from source:

```sh
xcode-select --install   # if you don't already have Xcode CLI tools
git clone https://github.com/albert-ying/flick.git
cd flick
make && sudo make install
launchctl load /Library/LaunchAgents/com.warpd.warpd.plist
```

On first launch, macOS will prompt you to grant Accessibility permissions to the `flick` binary.

Uninstall:

```sh
sudo make uninstall
```

## Configuration

Place options in `~/.config/warpd/config`, one per line as `option: value`.

Key flick-specific options:

```
# Cursor appearance
cursor_color:        #FF4500       # normal mode cursor (rgba hex)
cursor_color_dark:   #333333       # cursor on light backgrounds (adaptive contrast)
cursor_size:         7             # cursor radius in pixels

# Drag & scroll feedback
drag_cursor_color:     #D4831E     # cursor color while dragging
drag_indicator_color:  #D4831E66   # ring indicator while dragging
scroll_cursor_color:   #4E9A82     # cursor color while scrolling

# Click effect
click_effect_color:  #FF4500       # color of the expanding ring on click

# Hint styling
hint_bgcolor:        #1c1c1e       # hint background
hint_fgcolor:        #a1aba7       # hint text color
hint_font:           Arial         # hint font (PostScript name on macOS)
hint_size:           20            # hint label size
hint_border_radius:  3             # hint corner radius
```

Run `flick --list-options` for the full list, or see the [man page](warpd.1.md).

## Quickstart

1. Run `flick` (or let the LaunchAgent start it automatically).

### Hint Mode

- Press `Alt-Meta-x` to scatter hints across the screen.
- Type the hint's key sequence to warp there, then enter normal mode.

### Grid Mode

- Press `Alt-Meta-g` to overlay a grid.
- Press `u` `i` `j` `k` to drill into quadrants.
- Press `m` to left-click, `,` to middle-click, `.` to right-click.

### Normal Mode

- Press `Alt-Meta-c` to enter normal mode.
- Move with `h` `j` `k` `l`. Press `m` / `,` / `.` to click.
- Press `v` to start a drag, then navigate to the target.
- Press `Escape` to exit.

## Differences from warpd

- Binary renamed to `flick`
- 15 visual effects layered onto the cursor and hint system
- Adaptive contrast: cursor auto-switches color based on background brightness
- Physics-based motion: spring overshoot, motion stretch, comet trail
- Click feedback: squish animation + gravity wave ripple
- Drag/scroll visual modes with distinct colors and indicators
- Hint cascade animation with border glow
- History ghost dots for spatial memory

Core keybindings, config format, and modes are unchanged from warpd.

## Credits

[warpd](https://github.com/rvaiya/warpd) was written and maintained by Raheman Vaiya. flick is a fork that adds visual polish while preserving the original modal design.

## License

See [LICENSE](LICENSE).
