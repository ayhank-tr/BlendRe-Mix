# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# ***** END GPL LICENSE BLOCK *****

# BGE-Remix, Blender side: the "_bge_remix" Python module, which loads
# bge_remix_bridge.dll on first use.

set(INC
	.
	../blenkernel
	../blenlib
	../makesdna
	../../../intern/guardedalloc
	../../../intern/remix_bridge
)

set(INC_SYS
	${PYTHON_INCLUDE_DIRS}
)

set(SRC
	intern/remix_python.c

	BRMX_remix.h
)

blender_add_lib(bf_remix "${SRC}" "${INC}" "${INC_SYS}")
