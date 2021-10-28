project('nwl', 'c',
	default_options: [
		'c_std=c11'
	],
	version: '0.4 do-not-use'
)
cc = meson.get_compiler('c')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
cairo = dependency('cairo')
egl = dependency('egl')
gl = dependency('gl')
rt = cc.find_library('rt')
wlegl = dependency('wayland-egl')
epoxy = dependency('epoxy')
xkbc = dependency('xkbcommon')
subdir('protocol')
subdir('nwl')
nwl_src = [
	'cairo.c',
	'egl.c',
	'seat.c',
	'shm.c',
	'shell.c',
	'surface.c',
	'wayland.c',
	wlprotos
]
nwl_deps = [
	wayland_client,
	wayland_cursor,
	cairo,
	egl,
	gl,
	rt,
	wlegl,
	epoxy,
	xkbc
]
if meson.is_subproject()
	nwl_lib = static_library('nwl', nwl_src, dependencies:nwl_deps)
	nwl_dep = declare_dependency(link_with: nwl_lib, sources:[wlproto_h], include_directories:'.')
else
	pkgc = import('pkgconfig')
	nwl_lib = library('nwl', nwl_src, dependencies:nwl_deps, install:true)
	pkgc.generate(nwl_lib)
endif
