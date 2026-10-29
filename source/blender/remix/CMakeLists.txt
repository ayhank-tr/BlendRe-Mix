# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# ***** END GPL LICENSE BLOCK *****

# BGE-Remix, Blender side.
#   bf_remix_loader: loads bge_remix_bridge.dll on first use and finds the
#                    Remix runtime; the material translator, the texture
#                    cache, the lamps and world, the shared presenter and the
#                    session (lock, owner, quality settings) (P5); no Python,
#                    linked into blender and blenderplayer (the game engine
#                    uses it).
#   bf_remix:        the "_bge_remix" Python module and the RTX Remix render
#                    engine's 3D view and F12 (blender only).

set(INC
	.
	../blenkernel
	../blenlib
	../bmesh
	../gpu
	../imbuf
	../makesdna
	../makesrna
	../render/extern/include
	../windowmanager
	../../../intern/glew-mx
	../../../intern/guardedalloc
	../../../intern/remix_bridge
)

set(INC_SYS
	${GLEW_INCLUDE_PATH}
)

set(SRC
	intern/remix_light.cc
	intern/remix_loader.c
	intern/remix_material.cc
	intern/remix_present.c
	intern/remix_session.c
	intern/remix_texcache.cc
	intern/remix_bake.cc

	BRMX_bake.h
	BRMX_light.h
	BRMX_loader.h
	BRMX_material.h
	BRMX_present.h
	BRMX_session.h
)

add_definitions(${GL_DEFINITIONS})

blender_add_lib(bf_remix_loader "${SRC}" "${INC}" "${INC_SYS}")

if(WITH_PYTHON)
	list(APPEND INC_SYS
		${PYTHON_INCLUDE_DIRS}
	)

	set(SRC
		intern/remix_python.c
		intern/remix_view.cc

		BRMX_remix.h
		BRMX_view.h
	)

	blender_add_lib(bf_remix "${SRC}" "${INC}" "${INC_SYS}")
endif()
