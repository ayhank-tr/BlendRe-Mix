# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>
import bpy
from bpy.types import Panel, Menu


class PhysicsButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "physics"


class PHYSICS_PT_game_physics(PhysicsButtonsPanel, Panel):
    bl_label = "Physics"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        ob = context.active_object
        rd = context.scene.render
        return ob and ob.game and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        ob = context.active_object
        game = ob.game
        soft = ob.game.soft_body

        layout.prop(game, "physics_type")
        layout.separator()

        physics_type = game.physics_type

        if physics_type == 'CHARACTER':
            layout.prop(game, "use_actor")
            layout.prop(ob, "hide_render", text="Invisible")  # out of place but useful
            layout.prop(game, "step_height", slider=True)
            layout.prop(game, "jump_speed")
            layout.prop(game, "fall_speed")
            layout.prop(game, "jump_max")

        elif physics_type in {'DYNAMIC', 'RIGID_BODY'}:
            split = layout.split()

            col = split.column()
            col.prop(game, "use_actor")
            col.prop(game, "use_ghost")
            col.prop(ob, "hide_render", text="Invisible")  # out of place but useful

            col = split.column()
            col.prop(game, "use_material_physics_fh")
            col.prop(game, "use_rotate_from_normal")
            col.prop(game, "use_sleep")

            layout.separator()

            split = layout.split()

            col = split.column()
            col.label(text="Attributes:")
            col.prop(game, "mass")
            col.prop(game, "radius")
            col.prop(game, "form_factor")

            col = split.column()
            sub = col.column()
            sub.prop(game, "use_anisotropic_friction")
            subsub = sub.column()
            subsub.active = game.use_anisotropic_friction
            subsub.prop(game, "friction_coefficients", text="", slider=True)

            split = layout.split()

            col = split.column()
            col.label(text="Linear velocity:")
            sub = col.column(align=True)
            sub.prop(game, "velocity_min", text="Minimum")
            sub.prop(game, "velocity_max", text="Maximum")
            col.label(text="Angular velocity:")
            sub = col.column(align=True)
            sub.prop(game, "angular_velocity_min", text="Minimum")
            sub.prop(game, "angular_velocity_max", text="Maximum")

            col = split.column()
            col.label(text="Damping:")
            sub = col.column(align=True)
            sub.prop(game, "damping", text="Translation", slider=True)
            sub.prop(game, "rotation_damping", text="Rotation", slider=True)

            layout.separator()

            split = layout.split()

            col = split.column()
            col.label(text="Lock Translation:")
            col.prop(game, "lock_location_x", text="X")
            col.prop(game, "lock_location_y", text="Y")
            col.prop(game, "lock_location_z", text="Z")

        if physics_type == 'RIGID_BODY':
            col = split.column()
            col.label(text="Lock Rotation:")
            col.prop(game, "lock_rotation_x", text="X")
            col.prop(game, "lock_rotation_y", text="Y")
            col.prop(game, "lock_rotation_z", text="Z")

        elif physics_type == 'SOFT_BODY':
            col = layout.column()
            col.prop(game, "use_actor")
            col.prop(game, "use_ghost")
            col.prop(ob, "hide_render", text="Invisible")

            layout.separator()

            split = layout.split()

            col = split.column()
            col.label(text="Attributes:")
            col.prop(game, "mass")
            # disabled in the code
            # col.prop(soft, "weld_threshold")
            col.prop(soft, "location_iterations")
            col.prop(soft, "linear_stiffness", slider=True)
            col.prop(soft, "dynamic_friction", slider=True)
            col.prop(soft, "collision_margin", slider=True)
            col.prop(soft, "use_bending_constraints", text="Bending Constraints")

            col = split.column()
            col.prop(soft, "use_shape_match")
            sub = col.column()
            sub.active = soft.use_shape_match
            sub.prop(soft, "shape_threshold", slider=True)

            col.separator()

            col.label(text="Cluster Collision:")
            col.prop(soft, "use_cluster_rigid_to_softbody")
            col.prop(soft, "use_cluster_soft_to_softbody")
            sub = col.column()
            sub.active = (soft.use_cluster_rigid_to_softbody or soft.use_cluster_soft_to_softbody)
            sub.prop(soft, "cluster_iterations", text="Iterations")

        elif physics_type == 'STATIC':
            col = layout.column()
            col.prop(game, "use_actor")
            col.prop(game, "use_ghost")
            col.prop(game, "use_record_animation")
            col.prop(ob, "hide_render", text="Invisible")

            layout.separator()

            split = layout.split()

            col = split.column()
            col.label(text="Attributes:")
            col.prop(game, "radius")

            col = split.column()
            sub = col.column()
            sub.prop(game, "use_anisotropic_friction")
            subsub = sub.column()
            subsub.active = game.use_anisotropic_friction
            subsub.prop(game, "friction_coefficients", text="", slider=True)

        elif physics_type == 'SENSOR':
            col = layout.column()
            col.prop(game, "use_actor", text="Detect Actors")
            col.prop(ob, "hide_render", text="Invisible")

        elif physics_type in {'INVISIBLE', 'NO_COLLISION', 'OCCLUDER'}:
            layout.prop(ob, "hide_render", text="Invisible")

        elif physics_type == 'NAVMESH':
            layout.operator("mesh.navmesh_face_copy")
            layout.operator("mesh.navmesh_face_add")

            layout.separator()

            layout.operator("mesh.navmesh_reset")
            layout.operator("mesh.navmesh_clear")


class PHYSICS_PT_game_collision_bounds(PhysicsButtonsPanel, Panel):
    bl_label = "Collision Bounds"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        game = context.object.game
        rd = context.scene.render
        return (rd.engine in cls.COMPAT_ENGINES) \
                and (game.physics_type in {'SENSOR', 'STATIC', 'DYNAMIC', 'RIGID_BODY', 'CHARACTER', 'SOFT_BODY'})

    def draw_header(self, context):
        game = context.active_object.game

        self.layout.prop(game, "use_collision_bounds", text="")

    def draw(self, context):
        layout = self.layout

        game = context.active_object.game
        split = layout.split()
        split.active = game.use_collision_bounds

        col = split.column()
        col.prop(game, "collision_bounds_type", text="Bounds")

        row = col.row()
        row.prop(game, "collision_margin", text="Margin", slider=True)

        sub = row.row()
        sub.active = game.physics_type not in {'SOFT_BODY', 'CHARACTER'}
        sub.prop(game, "use_collision_compound", text="Compound")

        layout.separator()
        split = layout.split()
        col = split.column()
        col.prop(game, "collision_group")
        col = split.column()
        col.prop(game, "collision_mask")


class PHYSICS_PT_game_obstacles(PhysicsButtonsPanel, Panel):
    bl_label = "Create Obstacle"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        game = context.object.game
        rd = context.scene.render
        return (rd.engine in cls.COMPAT_ENGINES) \
                and (game.physics_type in {'SENSOR', 'STATIC', 'DYNAMIC', 'RIGID_BODY', 'SOFT_BODY', 'CHARACTER', 'NO_COLLISION'})

    def draw_header(self, context):
        game = context.active_object.game

        self.layout.prop(game, "use_obstacle_create", text="")

    def draw(self, context):
        layout = self.layout

        game = context.active_object.game

        layout.active = game.use_obstacle_create

        row = layout.row()
        row.prop(game, "obstacle_radius", text="Radius")
        row.label()


class RenderButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "render"

    @classmethod
    def poll(cls, context):
        rd = context.scene.render
        return (rd.engine in cls.COMPAT_ENGINES)


class RENDER_PT_embedded(RenderButtonsPanel, Panel):
    bl_label = "Embedded Player"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        layout = self.layout

        rd = context.scene.render

        row = layout.row()
        row.operator("view3d.game_start", text="Start")
        row = layout.row()
        row.label(text="Resolution:")
        row = layout.row(align=True)
        row.prop(rd, "resolution_x", slider=False, text="X")
        row.prop(rd, "resolution_y", slider=False, text="Y")


class RENDER_PT_game_player(RenderButtonsPanel, Panel):
    bl_label = "Standalone Player"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        import sys
        layout = self.layout
        not_osx = sys.platform != "darwin"

        gs = context.scene.game_settings

        row = layout.row()
        row.operator("wm.blenderplayer_start", text="Start")
        row = layout.row()
        row.label(text="Resolution:")
        row = layout.row(align=True)
        row.active = not_osx or not gs.show_fullscreen
        row.prop(gs, "resolution_x", slider=False, text="X")
        row.prop(gs, "resolution_y", slider=False, text="Y")
        row = layout.row()
        col = row.column()
        col.prop(gs, "show_fullscreen")

        if not_osx:
            col = row.column()
            col.prop(gs, "use_desktop")
            col.active = gs.show_fullscreen

        col = layout.column()
        col.label(text="Quality:")
        col.prop(gs, "samples")
        col = layout.column(align=True)
        col.prop(gs, "depth", text="Bit Depth", slider=False)
        col.prop(gs, "frequency", text="Refresh Rate", slider=False)


class RENDER_PT_game_stereo(RenderButtonsPanel, Panel):
    bl_label = "Stereo"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings
        stereo_mode = gs.stereo

        # stereo options:
        layout.row().prop(gs, "stereo", expand=True)

        # stereo:
        if stereo_mode == 'STEREO':
            layout.prop(gs, "stereo_mode")
            layout.prop(gs, "stereo_eye_separation")

        # dome:
        elif stereo_mode == 'DOME':
            layout.prop(gs, "dome_mode", text="Dome Type")

            dome_type = gs.dome_mode

            split = layout.split()

            if dome_type in {'FISHEYE', 'TRUNCATED_REAR', 'TRUNCATED_FRONT'}:
                col = split.column()
                col.prop(gs, "dome_buffer_resolution", text="Resolution", slider=True)
                col.prop(gs, "dome_angle", slider=True)

                col = split.column()
                col.prop(gs, "dome_tessellation", text="Tessellation")
                col.prop(gs, "dome_tilt")

            elif dome_type == 'PANORAM_SPH':
                col = split.column()
                col.prop(gs, "dome_buffer_resolution", text="Resolution", slider=True)

                col = split.column()
                col.prop(gs, "dome_tessellation", text="Tessellation")

            else:  # cube map
                col = split.column()
                col.prop(gs, "dome_buffer_resolution", text="Resolution", slider=True)

                col = split.column()

            layout.prop(gs, "dome_text")


class RENDER_PT_game_shading(RenderButtonsPanel, Panel):
    bl_label = "Shading"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings

        # BGE-Remix P8 Part B: the pipeline (Modern applies the scene's Color
        # Management view transform, exposure, look and gamma at the end)
        layout.row().prop(gs, "render_pipeline", expand=True)

        # BGE-Remix P8: one material mode (GLSL), so no mode switch
        split = layout.split()

        col = split.column()
        col.prop(gs, "use_glsl_lights", text="Lights")
        col.prop(gs, "use_glsl_shaders", text="Shaders")
        col.prop(gs, "use_glsl_shadows", text="Shadows")
        col.prop(gs, "use_glsl_environment_lighting", text="Environment Lighting")

        col = split.column()
        col.prop(gs, "use_glsl_ramps", text="Ramps")
        col.prop(gs, "use_glsl_nodes", text="Nodes")
        col.prop(gs, "use_glsl_extra_textures", text="Extra Textures")
        # BGE-Remix P8: off for files saved in Multitexture (colours as they were drawn)
        col.prop(gs, "use_glsl_color_management", text="Color Management")


class RENDER_PT_game_remix(RenderButtonsPanel, Panel):
    """BGE-Remix: render the game through NVIDIA RTX Remix (path tracing).

    The settings are custom properties of the scene (bge_remix,
    bge_remix_fast, bge_remix_light_scale), which stock Blender 2.79b
    keeps and ignores."""
    bl_label = "Rasterizer"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def register(cls):
        from bpy.props import BoolProperty, EnumProperty, FloatProperty, IntProperty
        bpy.types.Scene.bge_remix = EnumProperty(
            name="Rasterizer",
            description="How the game engine draws this scene",
            items=(('OPENGL', "OpenGL", "Blender's own game engine drawing", 0),
                   ('RTX_REMIX', "RTX Remix", "Path traced by NVIDIA RTX Remix (needs an RTX card "
                    "and the runtime from Preferences > System)", 1)),
            default='OPENGL')
        bpy.types.Scene.bge_remix_fast = BoolProperty(
            name="Window Mode",
            description="Standalone player only: Remix shows its picture in a window of its own over the "
                        "game, without copying it (faster, but no text or overlays on top)",
            default=False)
        bpy.types.Scene.bge_remix_light_scale = FloatProperty(
            name="Light Scale",
            description="Brightness factor for lamps under RTX Remix",
            default=0.125, min=0.0, soft_max=100.0)
        bpy.types.Scene.bge_remix_exposure = FloatProperty(
            name="Exposure",
            description="Stops added to RTX Remix's automatic exposure (positive: brighter); "
                        "+1 matches Cycles' brightness",
            default=1.0, min=-10.0, max=10.0)
        # P5.1: Remix's quality settings, for the game, the 3D view and F12
        # (the menus' numbers are what source/blender/remix/intern/remix_session.c reads)
        bpy.types.Scene.bge_remix_preset = EnumProperty(
            name="Quality",
            description="RTX Remix's quality preset (light bounces, effects); Auto lets Remix choose for the "
                        "graphics card",
            items=(('AUTO', "Auto", "RTX Remix chooses for the graphics card", 0),
                   ('LOW', "Low", "Fastest", 1),
                   ('MEDIUM', "Medium", "", 2),
                   ('HIGH', "High", "", 3),
                   ('ULTRA', "Ultra", "Best looking", 4)),
            default='AUTO')
        bpy.types.Scene.bge_remix_dlss = EnumProperty(
            name="DLSS",
            description="How much smaller RTX Remix renders before DLSS scales the picture up (smaller is faster)",
            items=(('AUTO', "Auto", "RTX Remix chooses for the picture size", 0),
                   ('ULTRA_PERFORMANCE', "Ultra Performance", "A third of the size on each side", 1),
                   ('PERFORMANCE', "Performance", "Half the size on each side", 2),
                   ('BALANCED', "Balanced", "About 58% on each side", 3),
                   ('QUALITY', "Quality", "About two thirds on each side", 4),
                   ('DLAA', "DLAA", "Full size (DLSS only smooths the edges)", 5)),
            default='AUTO')
        bpy.types.Scene.bge_remix_rr = BoolProperty(
            name="Ray Reconstruction",
            description="DLSS Ray Reconstruction removes the noise of the path tracer (else Remix's own denoiser)",
            default=True)
        bpy.types.Scene.bge_remix_volumetrics = BoolProperty(
            name="Volumetrics",
            description="Light in the air (fog, light shafts)",
            default=True)
        # P5.6: F12 with RTX Remix
        bpy.types.Scene.bge_remix_f12_mode = EnumProperty(
            name="F12",
            description="How F12 renders with RTX Remix",
            items=(('REALTIME', "Real-time", "As the game shows it: a number of frames, denoised", 0),
                   ('REFERENCE', "Reference", "Samples added up without a denoiser: slower, no noise left", 1)),
            default='REALTIME')
        bpy.types.Scene.bge_remix_f12_frames = IntProperty(
            name="Frames",
            description="Real-time: frames rendered before the picture is taken (the denoiser settles)",
            default=32, min=1, max=4096)
        bpy.types.Scene.bge_remix_f12_samples = IntProperty(
            name="Samples",
            description="Reference: frames added up",
            default=256, min=1, max=1024)

    @classmethod
    def unregister(cls):
        for name in ("bge_remix", "bge_remix_fast", "bge_remix_light_scale", "bge_remix_exposure",
                     "bge_remix_preset", "bge_remix_dlss", "bge_remix_rr", "bge_remix_volumetrics",
                     "bge_remix_f12_mode", "bge_remix_f12_frames", "bge_remix_f12_samples"):
            if hasattr(bpy.types.Scene, name):
                delattr(bpy.types.Scene, name)

    @staticmethod
    def draw_remix_settings(layout, scene):
        """The settings the game, the 3D view and F12 share (also the RTX Remix engine's panel)."""
        col = layout.column()
        col.prop(scene, "bge_remix_light_scale")
        col.prop(scene, "bge_remix_exposure")
        col.separator()
        col.prop(scene, "bge_remix_preset")
        col.prop(scene, "bge_remix_dlss")
        row = col.row()
        row.prop(scene, "bge_remix_rr")
        row.prop(scene, "bge_remix_volumetrics")
        col.separator()
        row = col.row()
        row.prop(scene, "bge_remix_f12_mode", expand=True)
        if scene.bge_remix_f12_mode == 'REFERENCE':
            col.prop(scene, "bge_remix_f12_samples")
        else:
            col.prop(scene, "bge_remix_f12_frames")
        col.label("F12 gives what Remix shows: use the Standard view transform", icon='INFO')

    def draw(self, context):
        layout = self.layout
        scene = context.scene
        layout.row().prop(scene, "bge_remix", expand=True)
        if scene.bge_remix != 'RTX_REMIX':
            return
        RENDER_PT_game_remix.draw_remix_settings(layout, scene)
        layout.prop(scene, "bge_remix_fast")
        layout.label("3D view: Rendered shading shows RTX Remix", icon='INFO')
        try:
            import bge_remix
            ok = bge_remix.available() and bge_remix.installed()
        except Exception:
            ok = False
        if not ok:
            layout.label("RTX Remix is not installed: Preferences > System > RTX Remix", icon='ERROR')
        else:
            layout.label("2D filters and 3D text: OpenGL only for now", icon='INFO')


class RENDER_PT_game_system(RenderButtonsPanel, Panel):
    bl_label = "System"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings
        col = layout.column()
        row = col.row()
        col = row.column()
        col.prop(gs, "use_frame_rate")
        col.prop(gs, "use_restrict_animation_updates")
        col.prop(gs, "use_material_caching")
        col = row.column()
        col.prop(gs, "use_display_lists")
        col.active = gs.raster_storage != 'VERTEX_BUFFER_OBJECT'

        row = layout.row()
        row.prop(gs, "vsync")

        row = layout.row()
        row.prop(gs, "raster_storage")

        row = layout.row()
        row.label("Exit Key")
        row.prop(gs, "exit_key", text="", event=True)


class RENDER_PT_game_display(RenderButtonsPanel, Panel):
    bl_label = "Display"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings

        layout.prop(context.scene.render, "fps", text="Animation Frame Rate", slider=False)

        flow = layout.column_flow()
        flow.prop(gs, "show_debug_properties", text="Debug Properties")
        flow.prop(gs, "show_framerate_profile", text="Framerate and Profile")
        flow.prop(gs, "show_physics_visualization", text="Physics Visualization")
        flow.prop(gs, "use_deprecation_warnings")
        flow.prop(gs, "show_mouse", text="Mouse Cursor")

        col = layout.column()
        col.label(text="Framing:")
        col.row().prop(gs, "frame_type", expand=True)
        if gs.frame_type == 'LETTERBOX':
            col.prop(gs, "frame_color", text="")


class SceneButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "scene"


class SCENE_PT_game_physics(SceneButtonsPanel, Panel):
    bl_label = "Physics"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene.render.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings

        layout.prop(gs, "physics_engine", text="Engine")
        if gs.physics_engine == 'PHYSX':  # BGE-Remix (P6)
            layout.prop(gs, "physx_device", text="Device", expand=True)
        if gs.physics_engine != 'NONE':
            layout.prop(gs, "physics_gravity", text="Gravity")

            split = layout.split()

            col = split.column()
            col.label(text="Physics Steps:")
            sub = col.column(align=True)
            sub.prop(gs, "physics_step_max", text="Max")
            sub.prop(gs, "physics_step_sub", text="Substeps")
            col.prop(gs, "fps", text="FPS")

            col = split.column()
            col.label(text="Logic Steps:")
            col.prop(gs, "logic_step_max", text="Max")

            col = layout.column()
            col.label(text="Physics Deactivation:")
            sub = col.row(align=True)
            sub.prop(gs, "deactivation_linear_threshold", text="Linear Threshold")
            sub.prop(gs, "deactivation_angular_threshold", text="Angular Threshold")
            sub = col.row()
            sub.prop(gs, "deactivation_time", text="Time")

            col = layout.column()
            col.prop(gs, "use_occlusion_culling", text="Occlusion Culling")
            sub = col.column()
            sub.active = gs.use_occlusion_culling
            sub.prop(gs, "occlusion_culling_resolution", text="Resolution")

        else:
            split = layout.split()

            col = split.column()
            col.label(text="Physics Steps:")
            col.prop(gs, "fps", text="FPS")

            col = split.column()
            col.label(text="Logic Steps:")
            col.prop(gs, "logic_step_max", text="Max")


class SCENE_PT_game_physics_obstacles(SceneButtonsPanel, Panel):
    bl_label = "Obstacle Simulation"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene.render.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        gs = context.scene.game_settings

        layout.prop(gs, "obstacle_simulation", text="Type")
        if gs.obstacle_simulation != 'NONE':
            layout.prop(gs, "level_height")
            layout.prop(gs, "show_obstacle_simulation")


class SCENE_PT_game_navmesh(SceneButtonsPanel, Panel):
    bl_label = "Navigation Mesh"
    bl_options = {'DEFAULT_CLOSED'}
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene and scene.render.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        rd = context.scene.game_settings.recast_data

        layout.operator("mesh.navmesh_make", text="Build Navigation Mesh")

        col = layout.column()
        col.label(text="Rasterization:")
        row = col.row()
        row.prop(rd, "cell_size")
        row.prop(rd, "cell_height")

        col = layout.column()
        col.label(text="Agent:")
        split = col.split()

        col = split.column()
        col.prop(rd, "agent_height", text="Height")
        col.prop(rd, "agent_radius", text="Radius")

        col = split.column()
        col.prop(rd, "slope_max")
        col.prop(rd, "climb_max")

        col = layout.column()
        col.label(text="Region:")
        row = col.row()
        row.prop(rd, "region_min_size")
        if rd.partitioning != 'LAYERS':
            row.prop(rd, "region_merge_size")

        col = layout.column()
        col.prop(rd, "partitioning")

        col = layout.column()
        col.label(text="Polygonization:")
        split = col.split()

        col = split.column()
        col.prop(rd, "edge_max_len")
        col.prop(rd, "edge_max_error")

        split.prop(rd, "verts_per_poly")

        col = layout.column()
        col.label(text="Detail Mesh:")
        row = col.row()
        row.prop(rd, "sample_dist")
        row.prop(rd, "sample_max_error")


class SCENE_PT_game_hysteresis(SceneButtonsPanel, Panel):
    bl_label = "Level of Detail"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene and scene.render.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout
        gs = context.scene.game_settings

        row = layout.row()
        row.prop(gs, "use_scene_hysteresis", text="Hysteresis")
        row = layout.row()
        row.active = gs.use_scene_hysteresis
        row.prop(gs, "scene_hysteresis_percentage", text="")


class WorldButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "world"


class WORLD_PT_game_context_world(WorldButtonsPanel, Panel):
    bl_label = ""
    bl_options = {'HIDE_HEADER'}
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        rd = context.scene.render
        return (context.scene) and (rd.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        scene = context.scene
        world = context.world
        space = context.space_data

        split = layout.split(percentage=0.65)
        if scene:
            split.template_ID(scene, "world", new="world.new")
        elif world:
            split.template_ID(space, "pin_id")


class WORLD_PT_game_world(WorldButtonsPanel, Panel):
    bl_label = "World"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene.world and scene.render.engine in cls.COMPAT_ENGINES)

    def draw(self, context):
        layout = self.layout

        world = context.world

        row = layout.row()
        row.column().prop(world, "horizon_color")
        row.column().prop(world, "zenith_color")
        row.column().prop(world, "ambient_color")


class WORLD_PT_game_environment_lighting(WorldButtonsPanel, Panel):
    bl_label = "Environment Lighting"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene.world and scene.render.engine in cls.COMPAT_ENGINES)

    def draw_header(self, context):
        light = context.world.light_settings
        self.layout.prop(light, "use_environment_light", text="")

    def draw(self, context):
        layout = self.layout

        light = context.world.light_settings

        layout.active = light.use_environment_light

        split = layout.split()
        split.prop(light, "environment_energy", text="Energy")
        split.prop(light, "environment_color", text="")


class WORLD_PT_game_mist(WorldButtonsPanel, Panel):
    bl_label = "Mist"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        scene = context.scene
        return (scene.world and scene.render.engine in cls.COMPAT_ENGINES)

    def draw_header(self, context):
        world = context.world

        self.layout.prop(world.mist_settings, "use_mist", text="")

    def draw(self, context):
        layout = self.layout

        world = context.world

        layout.active = world.mist_settings.use_mist

        layout.prop(world.mist_settings, "falloff")

        row = layout.row(align=True)
        row.prop(world.mist_settings, "start")
        row.prop(world.mist_settings, "depth")

        layout.prop(world.mist_settings, "intensity", text="Minimum Intensity")


class DataButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"


class DATA_PT_shadow_game(DataButtonsPanel, Panel):
    bl_label = "Shadow"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        # BGE-Remix P8.10: every lamp casts (point and area lamps from a cube
        # map, the sun from cascades that follow the camera)
        COMPAT_LIGHTS = {'SPOT', 'SUN', 'POINT', 'AREA'}
        lamp = context.lamp
        engine = context.scene.render.engine
        return (lamp and lamp.type in COMPAT_LIGHTS) and (engine in cls.COMPAT_ENGINES)

    def draw_header(self, context):
        lamp = context.lamp

        self.layout.prop(lamp, "use_shadow", text="")

    def draw(self, context):
        layout = self.layout

        lamp = context.lamp

        layout.active = lamp.use_shadow

        split = layout.split()

        col = split.column()
        col.prop(lamp, "shadow_color", text="")

        col = split.column()
        col.prop(lamp, "use_shadow_layer", text="This Layer Only")
        col.prop(lamp, "use_only_shadow")

        # the softness comes from the light's size (Lamp panel: Size, Angle)
        if lamp.type == 'SPOT':
            col = layout.column()
            col.label("Buffer Type:")
            col.prop(lamp, "ge_shadow_buffer_type", text="", toggle=True)
        variance = lamp.type == 'SPOT' and lamp.ge_shadow_buffer_type == 'VARIANCE'
        col = layout.column()
        col.label("Quality:")
        col = layout.column(align=True)
        col.prop(lamp, "shadow_buffer_size", text="Size")
        col.prop(lamp, "shadow_buffer_bias", text="Bias")
        if variance:
            col.prop(lamp, "shadow_buffer_bleed_bias", text="Bleed Bias")

        if lamp.type == 'SUN':
            # the cascades cover the camera's view up to this distance
            layout.prop(lamp, "shadow_buffer_clip_end", text="Distance")
        else:
            row = layout.row()
            row.label("Clipping:")
            row = layout.row(align=True)
            row.prop(lamp, "shadow_buffer_clip_start", text="Clip Start")
            row.prop(lamp, "shadow_buffer_clip_end", text="Clip End")

        if hasattr(bpy.types.Lamp, "bge_contact_shadow"):
            layout.prop(lamp, "bge_contact_shadow")


class ObjectButtonsPanel:
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "object"


class OBJECT_MT_lod_tools(Menu):
    bl_label = "Level Of Detail Tools"

    def draw(self, context):
        layout = self.layout

        layout.operator("object.lod_by_name", text="Set By Name")
        layout.operator("object.lod_generate", text="Generate")
        layout.operator("object.lod_clear_all", text="Clear All", icon='PANEL_CLOSE')


class OBJECT_PT_levels_of_detail(ObjectButtonsPanel, Panel):
    bl_label = "Levels of Detail"
    COMPAT_ENGINES = {'BLENDER_GAME'}

    @classmethod
    def poll(cls, context):
        return context.scene.render.engine in cls.COMPAT_ENGINES

    def draw(self, context):
        layout = self.layout
        ob = context.object
        gs = context.scene.game_settings

        col = layout.column()

        for i, level in enumerate(ob.lod_levels):
            if i == 0:
                continue
            box = col.box()
            row = box.row()
            row.prop(level, "object", text="")
            row.operator("object.lod_remove", text="", icon='PANEL_CLOSE').index = i

            row = box.row()
            row.prop(level, "distance")
            row = row.row(align=True)
            row.prop(level, "use_mesh", text="")
            row.prop(level, "use_material", text="")

            row = box.row()
            row.active = gs.use_scene_hysteresis
            row.prop(level, "use_object_hysteresis", text="Hysteresis Override")
            row = box.row()
            row.active = gs.use_scene_hysteresis and level.use_object_hysteresis
            row.prop(level, "object_hysteresis_percentage", text="")

        row = col.row(align=True)
        row.operator("object.lod_add", text="Add", icon='ZOOMIN')
        row.menu("OBJECT_MT_lod_tools", text="", icon='TRIA_DOWN')


classes = (
    PHYSICS_PT_game_physics,
    PHYSICS_PT_game_collision_bounds,
    PHYSICS_PT_game_obstacles,
    RENDER_PT_embedded,
    RENDER_PT_game_player,
    RENDER_PT_game_stereo,
    RENDER_PT_game_shading,
    RENDER_PT_game_remix,
    RENDER_PT_game_system,
    RENDER_PT_game_display,
    SCENE_PT_game_physics,
    SCENE_PT_game_physics_obstacles,
    SCENE_PT_game_navmesh,
    SCENE_PT_game_hysteresis,
    WORLD_PT_game_context_world,
    WORLD_PT_game_world,
    WORLD_PT_game_environment_lighting,
    WORLD_PT_game_mist,
    DATA_PT_shadow_game,
    OBJECT_MT_lod_tools,
    OBJECT_PT_levels_of_detail,
)

if __name__ == "__main__":  # only for live edit.
    from bpy.utils import register_class
    for cls in classes:
        register_class(cls)
