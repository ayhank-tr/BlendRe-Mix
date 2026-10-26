# BGE-Remix P1 test double for the RTX Remix runtime: builds d3d9.dll.
# Standalone project (CI builds it next to Blender, not inside Blender's tree):
#   cmake -S tests/bge_remix/p1/mock_remix -B mockbuild -G "Visual Studio 17 2022" -A x64
#   cmake --build mockbuild --config Release

cmake_minimum_required(VERSION 3.15)
project(bge_remix_mock CXX)

add_library(mock_remix SHARED mock_remix.cpp)
set_target_properties(mock_remix PROPERTIES OUTPUT_NAME d3d9)
target_include_directories(mock_remix PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../extern/remix_sdk)
target_link_libraries(mock_remix PRIVATE user32)
if(MSVC)
	target_compile_options(mock_remix PRIVATE /W3 /EHsc)
	# no dependency on a separately installed C runtime
	set_property(TARGET mock_remix PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()
