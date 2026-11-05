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
  Multitexture had none. A texture mapped to alpha turns blending on only in
  scenes saved in Multitexture; in GLSL files, as in stock 2.79, only
  Transparency does (P8.1). The Display Lists, Vertex Arrays and VBO setting
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

Render > Shading > Pipeline (`scene.game_settings.render_pipeline`). P9.1
cut the choice down to two:

- **Classic** (`'CLASSIC'`): 8-bit colour, drawn as Blender 2.79 drew the
  game. Classic never uses RTX Remix, whatever its switch says. Files from
  before P8 open with it.
- **RTX Remix** (`'RTX_REMIX'`, P8's Modern): Remix draws the game when the
  RTX Remix panel's switch is On and Remix can draw; otherwise its OpenGL
  fallback does. The fallback is P8's Modern drawing (half-float colour, the
  camera effects, order-independent transparency, tessellated displacement,
  volumetric fog with light shafts) made to look like Remix: the world's sky
  lights the scene as Remix's dome does (in place of Environment Lighting),
  and a sun always casts shadows.
- **Tone Mapping** (RTX Remix panel, `scene.game_settings.remix_tone_mapping`):
  View Transform (the scene's Color Management in the fallback, Remix's own
  tone mapper in Remix, as P8's Modern scenes had it) or ACES (Remix's
  global tone mapper finished with ACES at an exposure 0.3 higher, measured
  to match the fallback's ACES). Remix's tone curve still adapts to each
  picture: the two match closely, not exactly.
  A scene moved to RTX Remix by hand gets ACES.
- **RTX Remix Settings** (a closed panel under RTX Remix): the Remix options
  that matter for games, saved with the scene (`bge_rtx_*`): upscaling and
  frame generation, light bounces, emissive intensity, anti-culling,
  chromatic aberration, particles and, with the view transform, Remix's tone
  mapper. A setting left alone keeps Remix's default. A scene that sets its
  own resolution scale or light-bounce settings puts Remix's graphics
  preset on Custom, so the preset (Auto picks one per graphics card: Medium
  on an RTX 3080) does not replace them (P9.2b). Resolution Scale counts
  only with DLSS off: with DLSS on, the DLSS setting decides the size Remix
  renders at.

P8's files open as follows: Classic with Remix off stays Classic; Classic
with Remix on (or rendered with the RTX Remix engine) opens in RTX Remix with
Remix on; Modern with Remix on or off opens in RTX Remix with the switch as
it was. All keep the view transform. A scene put in Classic by hand stays
Classic, its Remix switch left as it was.

## RTX Remix materials (P9.2)

Material tab, under the material's name: **Classic** or **RTX Remix**.

- **RTX Remix** turns the material into a node material whose output is an
  **RTX Remix Material** node (Blender's Output node stays in the tree,
  unwired, holding the node's albedo and opacity). Its values come from the
  material: a Physical material's as they are; a Classic one's colour (times
  its diffuse intensity), specular hardness (as roughness), emit and alpha.
  Each texture slot becomes a Texture node read at the slot's coordinates
  (UV through a Geometry node; a Mapping node when the slot moves or scales
  them), wired by what the slot affects. Normal, roughness and metallic
  images are set to Non-Color, as the slots read them.
- **Classic** switches the nodes off and keeps the tree: RTX Remix brings it
  back as it was. A P8 Physical material shows neither; RTX Remix takes its
  values (P9.3 converts them when a file opens).
- The node's inputs are every field of a Remix material: Albedo, Opacity,
  Roughness, Metallic, Normal (a tangent-space normal map; DirectX green is an
  option), Emissive Color and Intensity, Height with Displace In and Out,
  Anisotropy, Thin Film (nanometres), IOR, Transmittance and its distance,
  Thin Wall Thickness, Subsurface with its colour and radius. On the node:
  Type (Opaque or Translucent: Remix's glass), Blend (the material's game
  transparency), the alpha test's cutoff, Thin Walled, Particle, the Remix
  categories, Bake Size.
- The game draws the node with the Physical surface's shading, in both
  pipelines; F12 renders it with the same lighting maths as the game (lamps,
  their shadows, the world and, in the RTX Remix pipeline, the sky light);
  Remix gets every input. Height is displaced by Remix and, with no normal
  wired, bumps the surface in the game (P9.6); F12 does not draw it.
  Anisotropy and thin film are drawn by Remix only. Displace In and Out go
  to Remix only with something wired into Height.
- To Remix: an input fed by a Texture node holding an image, read at the
  Geometry node's UV, goes as that image (its Value output: the image's
  alpha); anything else wired into Albedo, Opacity, Roughness, Metallic,
  Normal or the emission is baked into that channel; other inputs send their
  own value.
- The node's Match panel (the node editor's sidebar) and the material's RTX
  Remix Match say, input by input, how each reaches Remix.
- In the RTX Remix pipeline both sides of a face cast shadows, as in Remix.
- Stock 2.79 does not know the node: it draws the Output node's own colour
  and alpha, kept at the node's albedo and opacity.

## Old materials (P9.3)

When a file opens in Blender, the materials of its game scenes that are not
Classic become RTX Remix materials:

- a Physical material (P8), as picking RTX Remix does;
- a Cycles node material, read as RTX Remix already read it: Principled,
  Diffuse, Glossy, Glass, Emission and Subsurface Scattering map straight,
  an image wired straight in becomes a Texture node, a see-through Mix keeps
  its opacity, a Mix of a surface and a glow keeps both, any other Mix the
  stronger surface;
- a Blender Internal node material: its graph stays and feeds the node, and
  its Output node stays wired, so stock 2.79 draws the graph; Remix gets the
  graph baked, its Material nodes as their surface colour (Remix lights
  it);
- a material whose RTX Remix block (P8) is in use: the block's values and
  images set the node's inputs. The block still applies in Remix; its panel
  (RTX Remix Fields) shows only on materials whose block is in use.

Classic materials stay as they are. Each original is kept as a fake-user
material `<name>.p8` until the file has been saved twice. The text block
"BGE-Remix conversion" lists what was converted and anything approximated;
the console and the status bar say it too. The player plays old files
unchanged. Blender run from a script (in the background) converts only with
`BGE_REMIX_CONVERT=1`; `BGE_REMIX_CONVERT=0` turns conversion off.

A material in a file that is also rendered with Cycles loses its Cycles
nodes in the conversion (the `.p8` copy keeps them).

## Texture settings in Remix (P9.4)

Remix takes one wrap and one filter per material. When all of a material's
image textures agree, their settings go to Remix as they are: Repeat,
Extend (clamp), Clip and Mirror become Remix's wrap, and Interpolation off
turns filtering off. A slot's scale and offset, the image's repeat counts and
Flip X/Y are applied to the mesh's UVs; an RTX Remix material does the same
through a Mapping node (no rotation) before its Texture node. Anything Remix
cannot take as it is is baked into an image, channel by channel: influence
below 1, blends other than Mix, several slots in one channel, coordinates
other than UV, the Colors panel, Negative, Checker, bump maps, and textures
whose settings differ. The game itself draws only a slot's scale and offset,
so repeats, mirror, flip, Extend and Clip show in Remix and F12, not in the
fallback. The Match panel's "filtering and wrapping" row says which way the
textures went, and each baked field says why.

## The Texture tab (P9.5)

For an RTX Remix material the tab lists the material's texture nodes, each
named after the input of the RTX Remix Material node it feeds ("Albedo",
"Roughness"). Pick one to see its texture as usual; the RTX Remix Input panel
shows its image, extension, interpolation, the Mapping node in front (offset,
size) and its strength, and changing them changes the node. Classic
materials keep their texture slots as in 2.79.

## Preset materials (P9.6)

The RTX Remix Material panel's Preset menu gives a material a ready-made
surface: Rock, Gravel, Tarmac, Grass, Brick, Tree bark, Water, Skin (light,
medium or dark), Dirt road / mud, Sand, Snow or Concrete. A material that is
not RTX Remix yet becomes one. The preset is one group node with a few
inputs: Scale (repeats per UV unit, one UV unit taken as 1 m), Colour, Wear
(weathering, chips, dust), Wetness (Tarmac and Dirt road: darker and glossy
where water would gather) and Seed (shifts the pattern). The game draws the
preset live; Remix gets it as one seamless tile with colour, roughness,
height and bumps, baked once per material however many meshes use it.
Water stands still, in the game and in Remix.

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
  Line render types are drawn; Path and None are not. Remix 1.6's particle
  fields are sent too: Rotation > Random becomes the spawn rotation's spread.
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
- Inside Blender, Remix builds its shaders one after another, not on its
  background threads: a game started with P in Texture or Material shading
  crashed NVIDIA's driver while Blender built its own shaders (P8.1). Remix's
  first start inside Blender takes a little longer; the player is unchanged.
- A shadeless material with an image glows with its image. Face Texture
  (bitmap) text reaches Remix letter by letter.
- Once a game draws through Remix, the scenes it switches to and the files it
  loads (Scene and Game actuators, Python) stay in Remix, unless they turn
  RTX Remix off themselves. The standalone player's `-g remix = 1` turns it
  on from the first scene.
- The property tabs show one panel per idea, shared by the game, F12 and
  Remix. In a game scene, options that do nothing are hidden; options that
  work in only some renderers carry an "OpenGL only", "Remix only" or "F12
  only" mark. Hidden options keep their values, and Python still reaches
  them.

## The Remix View (P9.7)

The Remix View (P7's Game View) has two modes. Check shows the game layer by
layer: holograms, warnings and targets. Debug shows RTX Remix's own picture:
the menu next to the mode buttons picks one of Remix's debug views, and the
gear beside it sets how values show (display type, value and EV ranges,
false colour, an overlay over the picture). The figures after it are draws,
lights, textures waiting and frame time; one turns red past your targets,
and a click shows every counter. Click an object to see, in the Inspect
panel, what Remix got for it: its mesh, how many objects share it, each
material input and where it came from, each texture's cache file. A game
started from Blender keeps the debug view picked. Hash collisions and calls
Remix refused show as warnings on the Remix layer, in Check mode.

## The editors on OpenGL 4.5

Blender and the player run in an OpenGL 4.5 compatibility context (since
P8.1): the full 4.5 feature set, and the driver itself draws the old calls
Blender 2.79's editors make. P8 had run them in a core context, where those
calls go through an emulation; Rally Cross found a state the emulation lost
(a crash in the 3D view), and the compatibility context also redraws faster
(about 18 ms a window on an RTX 3080, as stock 2.79, against 23 ms).

Blender 2.79's editors make about 4,700 old OpenGL calls. On a core context
(`--gl-core`) they go to one emulation in the GPU module: immediate mode, client arrays,
display lists, the matrix and attribute stacks, lamps and materials, texture
environments, fog, stipples, clip planes, the raster position, bitmaps,
`glDrawPixels` and GLU's quadrics. The editors look as in stock 2.79; lines
and text can differ by a pixel. Selection uses occlusion queries.

- `--gl-core` on the command line (Blender or the player), or the
  environment variable `BGE_REMIX_GL_CORE=1`, asks for the core context and
  the emulation. It is kept for a later Mac version (macOS offers only core
  contexts). `--gl-compat` (`BGE_REMIX_GL_COMPAT=1`) still asks for the
  default.
- In a core context each emulated draw is checked against what OpenGL holds
  (the bound vertex array and its element buffer) and put right, with a
  `BGE-Remix GL check:` line; `BGE_REMIX_LGL_CHECK=1` checks every attribute
  too, `BGE_REMIX_LGL_CHECK=0` leaves the check out.
- **Python `bgl`** works through the same emulation in a core context. The few old calls it
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
| `BGE_REMIX_GL_COMPAT=1` | The compatibility context (as `--gl-compat`; the default) |
| `BGE_REMIX_GL_CORE=1` | The core context, the old calls emulated (as `--gl-core`) |
| `RTX_ENABLE_ASYNC_COMPILATION=1` | Inside Blender, Remix builds its shaders on background threads again (P8.1 sets 0 there: with Texture or Material shading the driver crashed) |
| `BGE_REMIX_GL_TIER=1` | Tier 1 only, whatever the card offers |
| `BGE_REMIX_GL_DEBUG=1` | A debug context: driver messages in the console |
| `BGE_REMIX_GL_CHECK=1` | Report OpenGL state left changed after each editor region and game frame |
| `BGE_REMIX_SHADER_CACHE=off` or a folder | No shader cache, or the cache in that folder |
| `BGE_REMIX_PROBE_SIZE=<pixels>` | Reflection probe size (default 128) |
| `BGE_REMIX_FOG_GRID=WxHxD` | Volumetric fog grid size (default 160x90x64) |
| `BGE_REMIX_MIST_FOG=<factor>` | Send the world's mist to Remix as fog, its distance times the factor (1: as set) |
| `BGE_REMIX_LIVE_TEXTURE_HZ=<n>` | `bge.texture` pictures sent to Remix n times a second (default 5; 0: none) |
| `BGE_REMIX_GUARDED_ALLOC=1` | The player uses Blender's guarded allocator: the blocks left at exit are listed by name after "Error Totblock" (P8.1) |
| `BGE_REMIX_GPU_INSTANCING=0` | Objects sharing a mesh and material sent to Remix one by one, not as one instanced draw |
| `BGE_REMIX_PARTICLE_SPAWN_UV=1` | Remix's particles take their texture coordinates from where they spawn |
| `BGE_REMIX_PARTICLE_SPRITE_MODE=<n>` | Remix's particle sprite sheet mode (0 off) |
| `BGE_REMIX_RTX_SET=k=v;k=v` | Set Remix options (`rtx.*`) after the scene's own |

## Python additions

- `bge.render.getRenderPipeline()`: `'CLASSIC'` or `'RTX_REMIX'` (P9.1; was
  `'MODERN'`).
- `bge.render.isRemixDrawing()` (P9.1): True when RTX Remix drew the active
  scene (the last frame), False when OpenGL did.
- `bge.render.remixSetOption(name, value)` (P9.0): change one of Remix's own
  settings (`rtx.*`) while it runs; True if Remix took it.
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
- With ACES, bright saturated colours (a strong orange glow, say) keep more
  of their colour in Remix than in the fallback: Remix's ACES converts
  colours in a way the fallback's curve does not copy (P9.2). Very dark
  colours also read a little lighter in Remix.
- Image textures and normal maps look softer in Remix than in the fallback,
  less so at larger window sizes, most likely Remix's denoiser (P9.2b found
  no Remix setting that changes it: texture detail, anisotropic filtering,
  upscaling, the graphics preset; to be looked at again after P9.3).
- In Remix a hemi lamp is sky light: it lights the whole scene, and no lamp
  keeps to its own layer.
- Remix's lights reach everywhere: a lamp's Distance (and Sphere) does not
  stop its light, so many small lamps blend into one glow.
- `bge.texture` pictures (render to texture, mirrors, video) reach Remix up
  to five times a second, not every frame. A render to texture or a mirror
  shows the game's OpenGL picture there, not a ray-traced one. Under Remix
  a render to texture without its own offscreen buffer draws into one of
  its own (the window shows Remix's picture), so 2D filters find nothing
  of it (P8.1).
- Text objects show their current text in Remix: each new text is made
  once and kept, at most 512 per object (P8.1).
- Remix's particles are lit surfaces in the emitter's material: they do
  not glow or blend as the game's do, so smoke and sparks look darker, or
  barely show, in dark scenes.
- Classic (Blender Internal) materials lit by a sun read brighter and paler
  in Remix than in the fallback (about 25 levels on the P9.4 texture wall);
  their patterns match. Flat skin presets read paler in Remix too (its
  subsurface light). To be looked at with P10's value check.
- A texture set to Clip shows black beyond the image in Remix, the
  material's colour in F12.
- Preset tiles baked for Remix: Brick shows dark bricks along the tile's
  repeat edges, Rock faint ghosted edges where the tile is cross-faded (P10
  rebuilds the tile bakes).
- Remix's bakes use one UV map; meshes without one are reported (on the
  Remix View's Remix layer, once per material).
- The game starts before its textures are ready: each is encoded for Remix
  in turn (with Remix running, about a second for a large baked one on an
  RTX 3080), and a material shows its plain colour until its textures are
  in. A second start finds them in the cache.
- Remix has no orthographic camera: an orthographic camera reaches it as a
  distant, narrow perspective camera framing the same view (within about
  1 % of the size across the view's depth), so orthographic 3D scenes are
  ray traced. A scene with an orthographic camera and no lamps is taken as
  a 2D screen (a menu): it is drawn in OpenGL as made, and the game stays in
  Remix for the scenes after it. Overlay scenes (added on top) are always
  drawn in OpenGL over Remix's picture.
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
- Video textures and other `bge.texture` pictures reach Remix as
  rewritten texture files, a few times a second (Remix 1.6.1 reads a
  texture from its file only).
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
- Remix 1.6.1's additions taken into P8 before its release: the particles'
  new fields, GPU instancing through the API (bridge ABI 11), `bge.texture`
  pictures in Remix, and a check of the emissive settings (boost emissives,
  the emissive blend override). Measured on the RTX 3080: crowd's 20,000
  objects in Remix went from 6.3 to 3.5 ms a frame with GPU instancing (its
  worst frame from 57 to 16 ms), the picture unchanged, so it is on; the
  particle fields and emissive settings changed no test picture, so Remix's
  defaults stay. Material texture transforms need API 0.6.6,
  newer than the pinned runtime's 0.6.5.
