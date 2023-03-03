conf_data = configuration_data()
conf_data.set10('NWL_HAS_SEAT', xkbc.found())
conf = configure_file(output:'config.h', configuration:conf_data)

if not meson.is_subproject()
	headers = [
		'cairo.h',
		'egl.h',
		'nwl.h',
		'shm.h',
		'surface.h',
		conf
	]
	if xkbc.found()
		headers+= [ 'seat.h' ]
	endif
	install_headers(headers, subdir:'nwl')
endif
