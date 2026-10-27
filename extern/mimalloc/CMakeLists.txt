# BGE-Remix, from BGE NEXT (CPU performance, 24 October 2026): mimalloc as one static library (src/static.c holds
# all of it). No malloc override: only what BGE-Next routes to it (C++ new/delete in the
# executables, MEM_ allocations) uses it.
set(INC
	include
)
set(INC_SYS
)
set(SRC
	src/static.c
	include/mimalloc.h
)
if(MSVC)
	# MSVC's C compiler lacks C11 atomics: mimalloc's own build compiles this file as C++
	set_source_files_properties(src/static.c PROPERTIES LANGUAGE CXX)
	add_definitions(-D_WIN32_WINNT=0x600)
endif()
add_definitions(-DMI_STATIC_LIB)
# the bge_ symbol prefix before anything else in the library
if(MSVC)
	set_property(SOURCE src/static.c APPEND_STRING PROPERTY COMPILE_FLAGS " /FI\"${CMAKE_CURRENT_SOURCE_DIR}/include/mimalloc_bge_prefix.h\"")
else()
	set_property(SOURCE src/static.c APPEND_STRING PROPERTY COMPILE_FLAGS " -include \"${CMAKE_CURRENT_SOURCE_DIR}/include/mimalloc_bge_prefix.h\"")
endif()
blender_add_lib(extern_mimalloc "${SRC}" "${INC}" "${INC_SYS}")
