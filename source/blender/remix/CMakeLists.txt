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
#                    Remix runtime; no Python, linked into blender and
#                    blenderplayer (the game engine uses it).
#   bf_remix:        the "_bge_remix" Python module (blender only).

set(INC
	.
	../blenkernel
	../blenlib
	../makesdna
	../../../intern/guardedalloc
	../../../intern/remix_bridge
)

set(INC_SYS
)

set(SRC
	intern/remix_loader.c

	BRMX_loader.h
)

blender_add_lib(bf_remix_loader "${SRC}" "${INC}" "${INC_SYS}")

if(WITH_PYTHON)
	set(INC_SYS
		${PYTHON_INCLUDE_DIRS}
	)

	set(SRC
		intern/remix_python.c

		BRMX_remix.h
	)

	blender_add_lib(bf_remix "${SRC}" "${INC}" "${INC_SYS}")
endif()
