project('gegl-control-points', 'c',
  version: '0.1',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: ['warning_level=1', 'buildtype=debugoptimized'],
)

gegl_dep = dependency('gegl-0.4', version: '>= 0.4.62')
babl_dep = dependency('babl-0.1', version: '>= 0.1.100')
math_dep = meson.get_compiler('c').find_library('m', required: false)

# GEGL loads user operations from $XDG_DATA_HOME/gegl-0.4/plug-ins, which is
# ~/.local/share/gegl-0.4/plug-ins, or for the Flatpak version of GIMP
# ~/.var/app/org.gimp.GIMP/data/gegl-0.4/plug-ins.
moduledir = get_option('moduledir')
if moduledir == ''
  moduledir = gegl_dep.get_variable(pkgconfig: 'pluginsdir')
endif

shared_module('control-points',
  'operations/control-points.c',
  name_prefix: '',
  include_directories: 'operations',
  dependencies: [gegl_dep, babl_dep, math_dep],
  c_args: ['-DGETTEXT_PACKAGE="gegl-control-points"'],
  install: true,
  install_dir: moduledir,
)

test_c_args = ['-DGEGL_PLUGINSDIR="@0@"'.format(
  gegl_dep.get_variable(pkgconfig: 'pluginsdir'))]

# numerical checks of the operation: tests/check.sh, or meson test -C build
check = executable('check', 'tests/check.c',
  dependencies: [gegl_dep, babl_dep, math_dep],
  c_args: test_c_args,
  install: false,
)
test('operation', check, args: [meson.current_build_dir()], timeout: 1800)

# how long the operation takes on a 24 megapixel image: tests/bench.sh
executable('bench', 'tests/bench.c',
  dependencies: [gegl_dep, babl_dep, math_dep],
  c_args: test_c_args,
  install: false,
)
