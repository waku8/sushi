# sushi 

An itsy bitsy floating Wayland compositor (*4k~ sloc*), built on
[neuswc](https://git.sr.ht/~shrub900/neuswc) and
[neuwld](https://git.sr.ht/~shrub900/neuwld).

- Floating only.
- Fullscreen toggle.
- Window centering (*new windows land in the middle*).
- Ten workspaces.
- Mix of mouse and keyboard workflow.
- Alt-Tab window focusing.
- Server-side decorations, five themes (*flat, classic, simple, love, win95*).
- Title bar font is any fontconfig pattern.
- Per-`app_id` window rules (*workspace, border, title bar*).
- Autostart 
- Built-in nein cursor, or fall back to client-drawn ones.
- Panels get their space respected (*layer shell, through swc*).
- Config hot-reloaded on save.
- `sushi validate` tells you what you got wrong.

## Default Keybindings

**Window Management**

| combo                     | action                 |
| ------------------------- | ---------------------- |
| `mod` + `Left Mouse`     | move window            |
| `mod` + `Right Mouse`    | resize window          |
| `mod` + `f`              | fullscreen toggle      |
| `mod` + `c`              | center window          |
| `mod` + `q`              | kill window            |
| `mod` + `Shift` + `q`    | quit sushi             |
| `mod` + `1-9,0`          | workspace swap         |
| `mod` + `Shift` + `1-9,0`| send window to workspace |
| `alt` + `TAB` | focus cycle            |
| `mod` + `Return` | terminal  | `foot`   |
| `mod` + `d`      | launcher  | `fuzzel` |

## Dependencies
- `swc` (*the [neuswc](https://git.sr.ht/~shrub900/neuswc) fork*).
- `wld` (*the [neuwld](https://git.sr.ht/~shrub900/neuwld) fork, pulled in by swc*).
- `wayland-server`.
- `xkbcommon`.
- `libinput`.

## Building

1) Run `meson setup build` to configure.
2) Run `ninja -C build` to build `sushi`.
3) Run `ninja -C build install` to install it.

There is nothing to edit before building: the config lives at
`~/.config/sushi/config`. See [doc/config](doc/config) for the full grammar and every default.

## Checking your config
```
sushi validate            # checks the file sushi would load
sushi validate ./config   # checks a specific file
```

Saving the config reloads on the fly. The keyboard section is the exception: swc
reads the layout and key repeat once at startup, so those need a restart.

## Thanks
- [lola](https://shithub.us/aap/lola/)
- [mot](https://codeberg.org/chld/mot)
- [swc](https://github.com/michaelforney/swc)
- [tohu](https://git.sr.ht/~shrub900/tohu/)
- [wld](https://github.com/michaelforney/wld)
