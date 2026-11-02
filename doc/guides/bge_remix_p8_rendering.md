# BGE-Remix P8: Rendering

P8 gives the game, the 3D view, F12 and RTX Remix one rendering system on
OpenGL 4.5. This guide describes what changed for people making games with
BlendRe-Mix, the settings and switches P8 adds, what it leaves out, and where
the finished P8 differs from its plan.

## Requirements

- A graphics card and driver with **OpenGL 4.5**. On an older card, Blender
  and the player print `BGE-Remix needs OpenGL 4.5.` and stop.
- Blender and the player ask for an OpenGL 4.5 **core** context. The old
  OpenGL calls Blender's editors, add-ons and scripts still make are emulated
  on it (see "The editors on OpenGL 4.5 core" below).
- **Tiers.** Tier 1 is plain OpenGL 4.5. Tier 2 adds what the card offers
  beyond it: bindless textures, sparse textures, Vulkan interop, anisotropic
  filtering and layered shadow views. The tier is chosen at start and printed
  in the console (`BGE-Remix GL: 4.5 core, tier 2 ...`).
- RTX Remix still needs an NVIDIA RTX card and the Remix runtime. Without
  them, everything else works.

## Materials

- **One mode.** The Multitexture/GLSL switch is gone from the interface:
  every file is drawn by the GLSL material system. Multitexture files open
  and look as they did (face textures, `setBlending`, object colours, texture
  to alpha). Their "GLSL Color Management" is turned off when opened, as
  Multitexture had none. The Display Lists, Vertex Arrays and VBO setting
  stays in files and has no effect: meshes are always drawn as batches.
- **Physical material** (Material > Surface > Shading: Physical). Metallic,
  Roughness, Specular, Transmission with IOR, Subsurface with a radius per
  colour, and Emission with a strength. The game, Remix and F12 read the same
  values. F12 translates them to Blender Internal's settings, which only
  approximates them. Classic shading stays the default, so old files are
  unchanged.
- **Procedural textures** (Clouds, Wood, Marble, Musgrave, Voronoi, Noise,
  Stucci, Distorted Noise, Magic, Blend) are drawn in the game's shaders and
  baked for Remix. Bakes run on the graphics card and are cached by content,
  so a second start bakes nothing. Material > RTX Remix > Bake Size sets the
  size.
- **Users' old shaders** (GLSL 1.20 with `gl_ModelViewMatrix`,
  `gl_LightSource`, `texture2D` and the like) are translated to GLSL 4.50
  when compiled. Anything the translator does not know is reported with its
  line, and the shader then fails as it would in stock 2.79. Names GLSL 4.50
  reserves get a suffix; looking them up by the old name still works.
- **Shader cache.** Compiled shaders are kept in `shader_cache` next to the
  user's config folder: at most 256 MB, oldest dropped first. A second start
  is faster.

## Pipelines

Render > Shading > Pipeline (`scene.game_settings.render_pipeline`):

- **Classic**: 8-bit colour, drawn as Blender 2.79 drew the game. Files from
  before P8 open with it.
- **Modern**: half-float colour; the scene's exposure, view transform, look
  and gamma (Color Management) are applied at the end. Modern adds the
  camera effects, order-independent transparency, tessellated displacement
  and volumetric fog with light shafts.

## Lights and shadows

- One light model for all lamp types, area lamps included. Point and spot
  lamps can fall off physically (Lamp > Falloff: Physical), inverse-square
  with the light's size, as Remix lights do. A sun lamp has an angle
  (`bge_sun_angle`, the real sun's 0.53 degrees by default) that softens its
  shadows in the game and in Remix.
- Many lamps: lamps are sorted into screen tiles (clustered), so a scene can
  hold hundreds. Lamps with a texture or a falloff curve, and Variance
  shadows, keep the per-lamp path.
- Shadows come from a shared pool of maps: cube maps for point lamps,
  cascades for the sun, soft or hard. Contact shadows are a per-lamp setting
  (`bge_contact_shadow`).

## Sky, mist, reflections, volumes

- The world is the game's sky: Blend Sky, Paper Sky, Real Sky, image skies
  and Sky & Atmosphere. Remix gets the same sky as its dome picture.
- Mist gets a height (World > Mist > Height; `mistHeight` on the game
  scene's `world` in Python). The world's mist is left out of Remix: Remix's
  fog turned the whole picture black on the RTX 3080, however thin.
  `BGE_REMIX_MIST_FOG` sends it anyway (exponential there, so linear and
  quadratic mist are approximated). A volume object the camera is inside
  still reaches Remix as fog.
- Reflections: reflection probes (Object > Reflection Probe, with a reach and
  an update rate), planar mirrors (Material > Planar Mirror) and screen-space
  reflections (World > Reflections).
- Volume materials, smoke and fire are drawn in the game. Modern scenes add
  a volumetric fog grid with light shafts.

## Hair, halos, particles, transparency, other objects

- **Hair** becomes ribbon meshes ("hair cards"), shaped by the Strand
  settings, built on the card and drawn instanced. Remix gets the same cards.
  Hair dynamics are not simulated.
- **Halos** become camera-facing sprites with Blender Internal's rings,
  lines and stars, and lens flares. Remix gets each halo as an emissive
  billboard.
- **Particles** are simulated in the game from the description the game
  already sends to Remix's GPU particles. Halo, Billboard, Object, Group and
  Line render types are drawn; Path and None are not.
- **Transparency**: Modern scenes use weighted blended transparency, which
  does not depend on draw order. Classic scenes keep sorted blending, as
  stock 2.79 does.
- **Curves, surfaces, metaballs and text** become meshes when the game
  starts, at their viewport resolution, and reach Remix as real geometry.
  Wire materials draw lines. Displacement textures raise the surface in
  Modern scenes.

## Camera and colour effects (Modern)

Render > Camera Effects: bloom (intensity, threshold, radius), auto exposure
(speed), vignette and depth of field (the camera's f-stop and focus). Motion
blur uses the Render tab's shutter, and ambient occlusion the world's
settings. In a Modern scene, Remix gets the same bloom, auto exposure,
vignette and motion blur through its options; its runtime has no depth of
field. A Classic scene leaves Remix's bloom on and its auto exposure off.

## RTX Remix

- Every material has an RTX Remix block: Fields (each Remix material field
  with a value and an optional texture), Options and Match. Match shows each field as reaching Remix exactly,
  approximately or not at all. Old files open with the block empty.
- One compressed texture cache serves the game and Remix: the game loads the
  same files Remix reads.
- Brightness: Remix's automatic exposure is off (a Modern scene's Auto
  Exposure turns it on). Lamps reach Remix as bright as in Blender Internal
  (Light Scale 1) with no exposure added (Exposure 0), so a scene looks as in
  the game and as in Cycles, dark scenes included. Files that saved their
  own Light Scale or Exposure keep them.
- A shadeless material with an image glows with its image. Face Texture
  (bitmap) text reaches Remix letter by letter.
- The property tabs show one panel per idea, shared by the game, F12 and
  Remix. In a game scene, options that do nothing are hidden; options that
  work in only some renderers carry an "OpenGL only", "Remix only" or "F12
  only" mark. Hidden options keep their values, and Python still reaches
  them.

## The editors on OpenGL 4.5 core

Blender 2.79's editors make about 4,700 old OpenGL calls. On a core context
they go to one emulation in the GPU module: immediate mode, client arrays,
display lists, the matrix and attribute stacks, lamps and materials, texture
environments, fog, stipples, clip planes, the raster position, bitmaps,
`glDrawPixels` and GLU's quadrics. The editors look as in stock 2.79; lines
and text can differ by a pixel. Selection uses occlusion queries.

- `--gl-compat` on the command line (Blender or the player), or the
  environment variable `BGE_REMIX_GL_COMPAT=1`, asks for the old
  compatibility context instead. It is kept for this release as a safety
  net.
- **Python `bgl`** works through the same emulation. The few old calls it
  does not copy are skipped, and the console names the script and line, for
  example `BGE-Remix GL emulation: bgl.glAccum is not emulated on the core
  context (skipped; my_script.py, line 79)`. They are: the accumulation
  buffer, feedback and the old select mode (they return no hits), 2D
  evaluators, colour-index mode, pixel maps, `glCopyPixels`, depth and
  stencil `glDrawPixels`, and `glInterleavedArrays`.

## Speed switches

Each technique that only makes the game faster can be switched off, to
compare or to work around a driver:

| Name | What it does |
| --- | --- |
| `instancing` | Objects sharing a mesh drawn in one instanced draw |
| `gpu_culling` | Hidden objects found on the graphics card |
| `gpu_skinning` | Armatures and shape keys deformed on the card |
| `layered_shadows` | A lamp's shadow views drawn in one pass (Tier 2) |
| `clustered_lights` | Lamps sorted into screen tiles |
| `oit` | Order-independent transparency (Modern) |
| `tessellation` | Tessellated displacement (Modern) |
| `screen_effects` | The camera effects (Modern) |
| `texture_cache` | Images from the shared compressed texture cache |

Switch one off with the environment variable `BGE_REMIX_NO_<NAME>=1` (for
example `BGE_REMIX_NO_INSTANCING=1`), or from a game script with
`bge.render.setSpeedFeature(name, False)`.

## Other switches

| Environment variable | Effect |
| --- | --- |
| `BGE_REMIX_GL_COMPAT=1` | The compatibility context (as `--gl-compat`) |
| `BGE_REMIX_GL_TIER=1` | Tier 1 only, whatever the card offers |
| `BGE_REMIX_GL_DEBUG=1` | A debug context: driver messages in the console |
| `BGE_REMIX_GL_CHECK=1` | Report OpenGL state left changed after each editor region and game frame |
| `BGE_REMIX_SHADER_CACHE=off` or a folder | No shader cache, or the cache in that folder |
| `BGE_REMIX_PROBE_SIZE=<pixels>` | Reflection probe size (default 128) |
| `BGE_REMIX_FOG_GRID=WxHxD` | Volumetric fog grid size (default 160x90x64) |
| `BGE_REMIX_MIST_FOG=<factor>` | Send the world's mist to Remix as fog, its distance times the factor (1: as set) |

## Python additions

- `bge.render.getRenderPipeline()`: `'CLASSIC'` or `'MODERN'`.
- `bge.render.setSpeedFeature(name, on)` and `bge.render.getSpeedFeature(name)`.
- `mistHeight` on the game scene's `world`.
- `gpu.draw_stats(reset=True)` in Blender: what the drawing sent since the
  last reset (streaming bytes and waits, emulated draws and vertices).
- New Blender properties: `scene.game_settings.render_pipeline` and the
  `fx_*` camera effect settings; the material's `bge_physical`,
  `bge_metallic`, `bge_roughness`, `bge_specular`, `bge_transmission`,
  `bge_ior`, `bge_subsurface`, `bge_subsurface_radius`, `bge_emission`,
  `bge_emission_strength`, `bge_planar_mirror`; the lamp's `bge_physical`,
  `bge_sun_angle`, `bge_contact_shadow`; the object's `bge_probe`,
  `bge_probe_reach`, `bge_probe_rate`; the world's `bge_ssr` and
  `bge_mirror_scale`. Stock 2.79 keeps them in the file and ignores them.

## Known differences

- Old Multitexture games look a little different: per-pixel lighting replaces
  per-vertex lighting.
- The game, F12 and Remix are close, not identical: procedurals, sky and lamps
  differ slightly. F12 only approximates the physical values; the game and
  Remix are the reference.
- The world's mist is not shown in Remix (see Sky, mist, reflections,
  volumes).
- In Remix a hemi lamp is sky light: it lights the whole scene, and no lamp
  keeps to its own layer.
- Remix's lights reach everywhere: a lamp's Distance (and Sphere) does not
  stop its light, so many small lamps blend into one glow.
- `bge.texture` pictures (render to texture, mirrors, video) do not update
  in Remix: it shows the image they start from.
- Remix's bakes use one UV map; meshes without one are reported.
- Remix draws orthographic cameras wrongly (the picture comes out zoomed
  in): use a perspective camera for scenes shown through Remix.
- In the editors, lines and fonts can differ by a pixel from stock 2.79.
- On a core context, a whole-window redraw takes about 22 ms on an RTX 3080,
  against about 19 ms in stock 2.79. Edit mode on a million faces is as fast
  as stock.

## Not in P8

What Remix's API cannot do (per-object volumes and smoke, light textures and
IES, per-lamp shadow switches, light linking: marked "not in Remix" instead);
hair dynamics; Freestyle lines and the compositor in the game; the video
sequencer's effects; a new shading look for old files; a Mac build; Vulkan or
Metal. AMD and Intel cards are not tested.

## P8 as built: where it differs from its plan

### Part A: the foundation and materials

- The fallback draws with the built-in classic shader instead of
  fixed-function OpenGL.
- Quads are split as an indexed `GL_QUADS` draw splits them.

### Part B: the game's drawing, lights, shadows, sky, reflections

- Remix mode reaches OpenGL through P4's direct mode; the Remix SDK offers no
  other way to share its picture.
- Reversed depth was not built.
- `bge.texture` mirrors need no clip planes (2.79 already uses an off-axis
  view).
- Shadows: Tier 1 draws a lamp's views one at a time. Casters are culled once
  on the processor, with a box holding every view.
- Clustered lamps: lamps with a texture or a falloff curve, and Variance
  shadows, keep the per-lamp path. Area lamps keep F12's rectangle form; their
  specular is F12's. The render layer's light override is not in the loop.
  Instanced draws use the first object's layer for This Layer Only lamps.
- Reflection probes draw their six faces one at a time, and at most two
  probes are drawn per frame. Screen-space reflections step linearly through
  the depth and read the last frame's colour without reprojection.
- Volumes in Modern scenes are stepped as in Classic ones, not added to the
  fog grid. Fire's colour is a fixed orange.
- The fog grid uses one shadow tap. Sky & Atmosphere follows the first sun
  lamp with the effect.
- Batching and streaming have no off switch (always on).

### Part C: effects, the Remix match, the editor, core

- Hair does not follow skinned faces, and the cards face the camera of the
  first frame.
- Halo textures are not drawn; billboard particles are unshaded. The game's
  particles match Remix's in count, rate and spread, not particle by
  particle.
- Video textures are not sent to Remix.
- Displacement is a pre-subdivided mesh made on the processor.
- Ambient occlusion is horizon-based; motion blur follows the camera only.
  2D filters over Remix's picture get no depth.
- The shared texture cache covers 8-bit colour (BC7) only.
- In the Remix block, opacity has no image of its own, and subsurface shares
  the albedo colour.
- The editors: one emulation of the old calls, as planned, but the 3D view's
  mesh drawing was not rewritten into batches either. The emulation was made
  fast enough instead (edit mode on a million faces matches stock 2.79). Text
  is drawn one batch per string. Material mode keeps Blender 2.79's material
  drawing, now through the core front end.
- The texture environment's crossbar (`GL_TEXTUREn` sources) is not
  emulated. A line stipple on a polygon outline restarts at each edge;
  triangle wireframes drawn by the driver have no stipple.
- On a core context, the GL_SELECT preference is ignored (occlusion queries
  are used), dupli objects are drawn one by one (no display lists), and
  OpenSubdiv draws on the processor.
- The old code paths stay in the program for `--gl-compat`; they are skipped
  on a core context.
- The release's RTX runs: shadow maps were added to instancing (crowd's
  frame went from 63 to about 16 ms); the world's mist was left out of Remix
  (black at any fog distance); Remix's automatic exposure was turned off,
  with Light Scale 1 and Exposure 0 (before: automatic, 0.125 and +1): the
  P2 room's difference from its Cycles render went from 43 to 6, and dark
  Classic scenes stopped washing out. P3's and P4's rooms are brighter than
  their sign-off pictures.
