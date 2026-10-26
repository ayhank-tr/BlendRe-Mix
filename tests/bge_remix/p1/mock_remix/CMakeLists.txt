# BGE-Remix P1 test double for the RTX Remix runtime: builds d3d9.dll.
# Standalone project (CI builds it next to Blender, not inside Blender's tree):
#   cmake -S tests/bge_remix/p1/mock_remix -B mockbuild -G "Visual Studio 17 2022" -A x64
#   cmake --build mockbuild --config Release

cmake_minimum_required(VERSION 3.15)
project(bge_remix_mock C CXX)

set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../../../../extern/remix_sdk)

# d3d9.dll: the fake runtime (and, next to a d3d9_x64.dll, a fake shim)
add_library(mock_remix SHARED mock_remix.cpp mock_d3d9.c)
set_target_properties(mock_remix PROPERTIES OUTPUT_NAME d3d9)
target_include_directories(mock_remix PRIVATE ${SDK})
target_link_libraries(mock_remix PRIVATE user32)

# remixdep.dll and d3d9_x64.dll: the real runtime's layout in small
add_library(remixdep SHARED remixdep.cpp)
add_library(mock_impl SHARED mock_remix.cpp mock_d3d9.c)
set_target_properties(mock_impl PROPERTIES OUTPUT_NAME d3d9_x64)
target_compile_definitions(mock_impl PRIVATE MOCK_IMPL)
target_include_directories(mock_impl PRIVATE ${SDK})
target_link_libraries(mock_impl PRIVATE user32 remixdep)

foreach(t mock_remix remixdep mock_impl)
	if(MSVC)
		target_compile_options(${t} PRIVATE /W3 /EHsc)
		# no dependency on a separately installed C runtime
		set_property(TARGET ${t} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
	endif()
endforeach()
