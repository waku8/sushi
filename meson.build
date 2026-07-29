project('sushi', 'c',
  version: '0.1.0',
  default_options: ['c_std=c99', 'warning_level=2'],
)

cc = meson.get_compiler('c')
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')

swc = dependency('swc')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')

sushi_sources = files(
  'src/main.c',
  'src/config.c',
  'src/window.c',
  'src/decor.c',
  'src/cursor.c',
)

executable('sushi', sushi_sources,
  dependencies: [swc, wayland_server, xkbcommon],
  install: true,
)
