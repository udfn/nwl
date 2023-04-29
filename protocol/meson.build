wayland_protocols = dependency('wayland-protocols')
proto_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')
wlscanner = find_program('wayland-scanner')
# This needs to be done better..
protos = [
	proto_dir + '/stable/xdg-shell/xdg-shell.xml',
	proto_dir + '/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
	proto_dir + '/unstable/xdg-output/xdg-output-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
]
wl_gen_c = generator(wlscanner,
	output: '@BASENAME@.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'])
wl_gen_h = generator(wlscanner,
	output: '@BASENAME@.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'])
wlproto_c = wl_gen_c.process(protos)
wlproto_h = wl_gen_h.process(protos)
wlprotos = [ wlproto_c, wlproto_h ]
