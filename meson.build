project('sushi', 'c',
  version: '0.1.0',
  default_options: ['c_std=c99', 'warning_level=2'],
)

cc = meson.get_compiler('c')
add_project_arguments('-D_GNU_SOURCE', language: 'c')

# The shipped defaults are doc/config itself rather than a second copy of them
# in config_defaults(); sushi reads this when the user has no config of their
# own.
sushi_datadir = get_option('prefix') / get_option('datadir') / 'sushi'
add_project_arguments(
  '-DSUSHI_DEFAULT_CONFIG="@0@"'.format(sushi_datadir / 'config'),
  language: 'c',
)
install_data('doc/config', install_dir: sushi_datadir)

swc = dependency('swc')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
# Device settings are applied directly: swc passes the libinput_device through.
libinput = dependency('libinput')

sushi_sources = files(
  'src/main.c',
  'src/config.c',
  'src/window.c',
  'src/decor.c',
  'src/cursor.c',
  'src/input.c',
)

executable('sushi', sushi_sources,
  dependencies: [swc, wayland_server, xkbcommon, libinput],
  # the crash handler walks the frame pointer chain by hand
  c_args: ['-fno-omit-frame-pointer'],
  install: true,
)
