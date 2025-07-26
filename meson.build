project('nwl', 'c',
	default_options: [
		'c_std=c11'
	],
	version: '0.5 do-not-use'
)
cc = meson.get_compiler('c')
seat_support = get_option('seat')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor', required: seat_support)
cairo = dependency('cairo')
gl = dependency('gl')
rt = cc.find_library('rt')
xkbc = dependency('xkbcommon', required: seat_support)
subdir('protocol')
subdir('nwl')
nwl_src = [
	'src/cairo.c',
	'src/shm.c',
	'src/shell.c',
	'src/surface.c',
	'src/wayland.c',
	wlprotos
]

nwl_deps = [
	wayland_client,
	cairo,
	gl,
	rt,
]
if xkbc.found()
	nwl_src += [ 'src/seat.c' ]
	nwl_deps += [
		xkbc,
		wayland_cursor
	]
endif
if meson.is_subproject()
	nwl_lib = static_library('nwl', nwl_src, dependencies:nwl_deps)
	nwl_dep = declare_dependency(link_with: nwl_lib, sources:[wlproto_h], include_directories:'.')
else
	pkgc = import('pkgconfig')
	nwl_lib = library('nwl', nwl_src, dependencies:nwl_deps, install:true)
	pkgc.generate(nwl_lib)
endif
