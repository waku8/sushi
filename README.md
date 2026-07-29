# sushi

A minimal floating Wayland window manager, built on [swc](https://git.sr.ht/~shrub900/neuswc) (the `neuswc` fork) and [wld](https://git.sr.ht/~shrub900/neuwld) (`neuwld`, linked transitively
through swc).

- Floating.
- Hot-reloaded on save.
- Per-`app_id` window rules (workspace, title bar, border).
- Decoration themes in the spirit of [lola](https://shithub.us/aap/lola/HEAD/info.html):
`flat` (title bar, no buttons), 
`classic` (`flat` plus minimize/maximize/close buttons), 
`simple` (no title bar at all, just a border, if enabled by user), 
`love` (like `classic`, but every button is a heart),
`win95` (Chicago-style0 
- nein curesor. Disable to fall back to client-drawn cursor.

## Building
Requires swc and wld already installed (`pkg-config swc wld --modversion`), plus wayland-server and xkbcommon.
```
meson setup build
ninja -C build
ninja -C build install
```

## Config

`~/.config/sushi/config`, created on first run if missing. See [doc/config](doc/config) for the full grammar and default bindings.
Saving the file reloads it live.

## Default bindings

| Binding | Action |
| --- | --- |
| mod+Return | spawn terminal(foot) |
| mod+d | spawn launcher(fuzzel) |
| mod+q | close window |
| mod+shift+q | quit sushi |
| mod+f | toggle fullscreen |
| mod+c | center window |
| mod+drag (button1) | move the window under the cursor |
| mod+drag (button3) | resize the window under the cursor |
| mod+\[1-9,0\] | switch workspace |
| mod+shift+\[1-9,0\] | move window to workspace |
| alt+Tab | cycle focus to the next window |

## Credits
sushi's design and decor themes were shaped by ideas from a few other window managers/compositors, 
thanks to their authors:
[tohu](https://git.sr.ht/~shrub900/tohu/), 
[mot](https://codeberg.org/chld/mot),
[lola](https://shithub.us/aap/lola/).
