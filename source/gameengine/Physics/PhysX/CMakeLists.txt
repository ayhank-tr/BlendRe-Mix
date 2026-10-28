# ***** BEGIN GPL LICENSE BLOCK *****
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# ***** END GPL LICENSE BLOCK *****

# BGE-Remix (P6): NVIDIA PhysX 5 as the game engine's second physics engine,
# chosen per scene in World > Physics. Uses Bullet's mesh reader
# (CcdShapeConstructionInfo) and Bullet's view culling tree, so it needs
# WITH_BULLET.

remove_strict_flags()

set(INC
	.
	../common
	../Bullet
	../Dummy
	../../BlenderRoutines
	../../Converter
	../../Expressions
	../../GameLogic
	../../Ketsji
	../../Ketsji/KXNetwork
	../../Network
	../../Network/LoopBackNetwork
	../../Rasterizer
	../../Rasterizer/RAS_OpenGLRasterizer
	../../SceneGraph
	../../../blender
	../../../blender/blenkernel
	../../../blender/blenlib
	../../../blender/blenloader
	../../../blender/gpu
	../../../blender/imbuf
	../../../blender/makesdna
	../../../blender/makesrna
	../../../../intern/container
	../../../../intern/guardedalloc
	../../../../intern/glew-mx
	../../../../intern/string
)

set(INC_SYS
	../../../../intern/moto/include
	../../../../extern/recastnavigation/Detour/Include
	${BULLET_INCLUDE_DIRS}
	${PHYSX_INCLUDE_DIRS}
	${GLEW_INCLUDE_PATH}
	${PYTHON_INCLUDE_DIRS}
)

set(SRC
	PxCharacter.cpp
	PxDeformable.cpp
	PxPhysicsController.cpp
	PxPhysicsConvert.cpp
	PxPhysicsEnvironment.cpp
	PxShapes.cpp
	PxSystem.cpp
	PxVehicle.cpp

	PxCharacter.h
	PxDeformable.h
	PxMotionState.h
	PxPhysicsController.h
	PxPhysicsEnvironment.h
	PxShapes.h
	PxSystem.h
	PxVehicle.h
)

add_definitions(-DWITH_BULLET -DWITH_PHYSX ${GL_DEFINITIONS})
if(WIN32)
	add_definitions(-DNOMINMAX)
endif()

blender_add_lib(ge_phys_physx "${SRC}" "${INC}" "${INC_SYS}")
