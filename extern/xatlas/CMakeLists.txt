# BGE-Remix P10.4: xatlas (MIT licence, github.com/jpcy/xatlas, its master
# as of 5 Nov 2026), for packing small images into atlas pages

set(INC
	.
)

set(INC_SYS

)

set(SRC
	xatlas.cpp

	xatlas.h
)

blender_add_lib(extern_xatlas "${SRC}" "${INC}" "${INC_SYS}")
