# BGE-Remix: DirectXTex (MIT, see LICENSE), the DDS writer of
# bge_remix_bridge.dll. Vendored from https://github.com/microsoft/DirectXTex
# tag oct2025 (commit 232bbf2e39a37ea2e9b7b8331653fa69bd4797a2): the library
# sources and the two BC compression shaders, nothing else (README.txt).
# Built with Blender's own flags (static runtime). DirectXTex's OpenMP
# compression (TEX_COMPRESS_PARALLEL) is not used: the bridge splits big
# images over its own threads instead.

# DirectXTex calls Windows 8+ functions (CreateFile2); Blender targets Vista
remove_definitions(-D_WIN32_WINNT=0x600)
add_definitions(-D_WIN32_WINNT=0x0A00)

set(SRC
	DirectXTex/BC.cpp
	DirectXTex/BC4BC5.cpp
	DirectXTex/BC6HBC7.cpp
	DirectXTex/DirectXTexCompress.cpp
	DirectXTex/DirectXTexConvert.cpp
	DirectXTex/DirectXTexDDS.cpp
	DirectXTex/DirectXTexHDR.cpp
	DirectXTex/DirectXTexImage.cpp
	DirectXTex/DirectXTexMipmaps.cpp
	DirectXTex/DirectXTexMisc.cpp
	DirectXTex/DirectXTexNormalMaps.cpp
	DirectXTex/DirectXTexPMAlpha.cpp
	DirectXTex/DirectXTexResize.cpp
	DirectXTex/DirectXTexTGA.cpp
	DirectXTex/DirectXTexUtil.cpp
	DirectXTex/DirectXTexFlipRotate.cpp
	DirectXTex/DirectXTexWIC.cpp

	DirectXTex/BC.h
	DirectXTex/DDS.h
	DirectXTex/DirectXTex.h
	DirectXTex/DirectXTex.inl
	DirectXTex/DirectXTexP.h
	DirectXTex/filters.h
	DirectXTex/scoped.h
)

# GPU (DirectCompute) BC6H/BC7 compression needs the shaders compiled with
# fxc.exe from the Windows SDK; without it the CPU path is used.
set(_fxc "")
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
	set(_fxc_hint "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64")
endif()
find_program(BRB_FXC_TOOL fxc.exe HINTS ${_fxc_hint})
if(BRB_FXC_TOOL)
	set(_fxc ${BRB_FXC_TOOL})
endif()

set(BRB_DIRECTXTEX_GPU OFF)
if(_fxc)
	set(BRB_DIRECTXTEX_GPU ON)
	set(_out ${CMAKE_CURRENT_BINARY_DIR}/Shaders/Compiled)
	file(MAKE_DIRECTORY ${_out})
	set(_shaders)
	foreach(_entry
		"BC7Encode:TryMode456CS" "BC7Encode:TryMode137CS" "BC7Encode:TryMode02CS" "BC7Encode:EncodeBlockCS"
		"BC6HEncode:TryModeG10CS" "BC6HEncode:TryModeLE10CS" "BC6HEncode:EncodeBlockCS")
		string(REPLACE ":" ";" _pair ${_entry})
		list(GET _pair 0 _file)
		list(GET _pair 1 _fn)
		set(_hlsl ${CMAKE_CURRENT_SOURCE_DIR}/DirectXTex/Shaders/${_file}.hlsl)
		add_custom_command(
			OUTPUT ${_out}/${_file}_${_fn}.inc ${_out}/${_file}_${_fn}_cs40.inc
			COMMAND ${_fxc} /nologo /WX /Ges /Zpc /Qstrip_reflect /Qstrip_debug /Tcs_5_0 /E${_fn}
			        /Fh${_out}/${_file}_${_fn}.inc /Vn${_file}_${_fn} ${_hlsl}
			COMMAND ${_fxc} /nologo /WX /Ges /Zpc /Qstrip_reflect /Qstrip_debug /Tcs_4_0 /DEMULATE_F16C /E${_fn}
			        /Fh${_out}/${_file}_${_fn}_cs40.inc /Vn${_file}_${_fn} ${_hlsl}
			DEPENDS ${_hlsl}
			COMMENT "fxc ${_file} ${_fn}"
			VERBATIM)
		list(APPEND _shaders ${_out}/${_file}_${_fn}.inc ${_out}/${_file}_${_fn}_cs40.inc)
	endforeach()
	list(APPEND SRC
		DirectXTex/BCDirectCompute.cpp
		DirectXTex/DirectXTexCompressGPU.cpp
		DirectXTex/DirectXTexD3D11.cpp
		DirectXTex/BCDirectCompute.h
		${_shaders}
	)
	message(STATUS "BGE-Remix: DirectXTex with GPU compression (${_fxc})")
else()
	message(STATUS "BGE-Remix: DirectXTex without GPU compression (fxc.exe not found)")
endif()

add_library(extern_directxtex STATIC ${SRC})
target_include_directories(extern_directxtex PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/DirectXTex)
if(BRB_DIRECTXTEX_GPU)
	target_include_directories(extern_directxtex PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/Shaders/Compiled)
	target_compile_definitions(extern_directxtex PUBLIC BRB_DIRECTXTEX_GPU)
endif()
if(MSVC)
	target_compile_options(extern_directxtex PRIVATE /std:c++17 /permissive- /Zc:__cplusplus /W1 /utf-8)
endif()
target_link_libraries(extern_directxtex PUBLIC windowscodecs ole32)
if(BRB_DIRECTXTEX_GPU)
	target_link_libraries(extern_directxtex PUBLIC d3d11 dxgi)
endif()
