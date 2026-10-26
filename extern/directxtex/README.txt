DirectXTex (BGE-Remix)
======================

The library sources (DirectXTex/*.cpp, *.h, *.inl) and the BC6H/BC7
compression shaders (DirectXTex/Shaders/*.hlsl), unchanged, from
https://github.com/microsoft/DirectXTex at tag oct2025
(commit 232bbf2e39a37ea2e9b7b8331653fa69bd4797a2). License: MIT (LICENSE).
Left out: the Direct3D 12 functions, the tools, samples and project files.

Used only by bge_remix_bridge.dll to write the texture cache's DDS files
(RTX Remix loads material textures only as DDS). CMakeLists.txt is ours: it
compiles the shaders with the Windows SDK's fxc.exe for GPU compression and
builds without OpenMP.
