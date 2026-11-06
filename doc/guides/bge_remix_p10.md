# BGE-Remix P10: the texture remaster, the Remix View's Edit and Run, the logic bricks

P10 adds three things, built side by side and released together
(bge-remix-p10):

- **The texture remaster** (P10.1 to P10.5): an audit of every image a game
  uses, preset fitting and full-channel bakes, derived channels and a value
  check, atlases (corner-matched tile sets, flipbooks, packing, the UV
  editor's atlas tools), and Apply, which bakes and encodes everything up
  front.
- **The Remix View's Edit and Run modes** (P10.6 to P10.9): editing the game
  through its avatars, the settings hub, and playing the game inside the
  view, with suspend, Resume or Start over.
- **The logic bricks brought up to date** (P10.10 to P10.13): each brick's
  live state in your colours, states and messages made visible, the Timer
  sensor and the RTX Remix actuator, and the bricks' old limits fixed (the
  full list: bge_remix_p10_brick_limits.md).

Decided along the way (5 to 7 Nov): random presets use 16 corner-matched
tiles in Remix by default; old P9 Brick presets are mended when a file
opens; converting Classic to RTX Remix keeps Diffuse Intensity; Classic's
remaining brightness in Remix is Remix's own reflection (a known
difference); the preset catalogue is built on first use; Remix's softer
picture is looked into with P14.

## The texture remaster

Notes for the guide (track p10a; the atlases, P10.4, are their own track).

### The RTX Remix Remaster panel (Image editor, sidebar)

- **Audit** lists every image the game scenes use (texture slots, Texture
  nodes feeding an RTX Remix Material node, face images; images used only
  outside the game scenes are left out). For each image: its size, the
  materials and objects using it, the channels its materials lack for Remix
  (roughness, normal, height), its texel density (pixels a metre: the
  lowest on any mesh using it), how often it repeats on its mesh, its
  painted-in light (a broad brightness slope), its kind and its plan. It
  changes nothing in the file; the report is the text
  "BGE-Remix remaster audit". From Python: `bge_remix.remaster_audit()`.
- **Kind**: *regular* (a strong repeat inside the image: bricks, tiles),
  *random* (an even spread of detail with no repeat: rock, dirt, grass) or
  *unique* (strong structure that does not repeat: text, signs, faces),
  measured from the image's frequency content, its self-similarity and how
  evenly its edges lie. No machine learning.
- **Plan**: *fit a preset* (regular or random images), *derive channels*
  (unique images, or a fit that scores too low), or *keep* (the material is
  already complete and the image sharp enough).
- **Sharpness target**: 512 pixels a metre (to be tuned on the test
  pictures). Images below it are flagged; P10 does not upscale.
- **Build Catalogue** bakes each preset about 200 times at 256 pixels across
  its settings (Scale, Wear, Wetness where it has one, Seed), once; the
  catalogue is kept in Blender's user folder
  (`datafiles/bge_remix_remaster`) and made again only when the presets
  change. It takes a few minutes; Esc stops it (finished presets are kept).
- **Fit** compares the image with every preset's catalogue by its frequency
  content, its detail at several scales (Bourque and Dudek's two measures)
  and a few colour statistics, then refines the best two with a simplex
  search. Seeds are whole numbers; Brick's Scale is a whole number of halves,
  so its tile holds whole bricks. The preset's Colour comes from the image's
  main colour. The score runs from 0 to 1; below 0.5 the plan becomes
  "derive channels". The tile's size in metres comes from the mesh (one
  repeat of the image on the mesh), so the bumps' slopes are true.
- The sliders: **Coarseness** (changes Scale), **Regularity** (changes
  Wear), **Direction** (measured only: the presets have no direction
  setting), then the preset's own Scale, Colour, Wear, Wetness and Seed.
  Changing any of them marks the image Adjusted.
- **Derive channels** (for images that stay): the painted-in light evened
  out (the **Painted Light** slider: how much), height from the brightness
  detail at several scales, the normal from the height, roughness from the
  brightness and the kind. All approximate; the images are marked so.
- **Accept** or **Reject** each image. **Preview** puts two squares in front
  of the Remix View (left: the original, right: the result), drawn by Remix
  as the game would draw them; Debug mode has a Remaster Preview panel too.
  The preview closes when the file is saved, so it never goes into the file.
- **Apply** writes the accepted images' RTX Remix materials: a fitted
  preset's group node (baked at 2K for Remix; 1K and 4K offered), or the
  derived images, saved as PNG files in a `remaster` folder next to the
  .blend (the file must be saved first). Each original material is kept as
  `<name>.p8` until the file has been saved twice, as P9.3 does. Then it
  bakes and encodes every texture of the game scenes for Remix, with a
  progress bar, so the game starts with every texture in.

### The value check (P10.3, part of the audit)

Every game material's values against realistic ranges, each problem with a
suggested fix:

- albedo: a mean below 0.02 or above 0.90 (the Physically Based database,
  physicallybased.info: charcoal about 0.02, fresh snow about 0.9; P9 had no
  such limits in its code, only its preset table's 0.01..0.95, which the
  check uses for an image's darkest and brightest pixels);
- metal: half metal (Metallic between 0.05 and 0.95 without an image), or a
  metal darker than 0.5 (metals reflect at least that);
- roughness below 0.05 (a perfect mirror) or above 0.98 (no reflection),
  except for water and glass.

### Fixed in P10

- Brick's tile (P9.6's known difference): every tile had one black brick at
  its corner. The brick's value was a Math node's Modulo of a number that
  is 0 for the brick at column and row 0, and the game's GLSL gives -1 for
  Modulo of 0 (Blender 2.79's own `math_modulo`), so that brick came out
  black in the game and in Remix, once per repeat: along the tile's repeat
  edges. The presets are version 2 now, with the fraction taken another way;
  materials given a preset before keep their version 1 group until the
  preset is picked again. Brick's tile is also baked over UV 0..1 only,
  holding whole repeats, with no cross-fade.
- Rock's tile (and every random preset's): P9.6 meant to draw a band past
  the tile's near edges and cross-fade it into the far ones, but a bake
  places every vertex at its UV, so the band was never drawn: the tile held
  the pattern squeezed (12.5 % coarser in Remix than in the game) and its
  edges blended unrelated parts of it, which showed as ghosted edges. Now
  the tile is drawn at the game's scale over one repeat and made seamless by
  blending in a copy of itself moved half a tile, near its edges only, the
  blend keeping the patches' own contrast.
- Brick's tile holds whole bricks, so its edges lie on mortar joints: the
  first PC run's seam measure took the joint at the tile's edge for a seam
  (the tile is periodic: its edge's step is the same as its middle's). The
  seam tests now compare the step at a tile's edge with the step half a tile
  in, too.
- Classic materials read about 25 levels brighter in Remix than in the game
  (the P9.4 texture wall): Remix's albedo colour is only used when there is
  no albedo image, so a Classic material's Diffuse Intensity (0.8 by
  default), which the game multiplies its colour image by, was lost. A
  texture slot's colour image now reaches Remix multiplied by it (in linear
  light; a new file in the cache, as its pixels differ; the Remix View's
  Inspect shows its scale). Face images are left as P8 made them (P8's
  Multitexture scenes draw them by fixed-function OpenGL and were matched
  as they are).

### Known difference: Classic materials read a little brighter in Remix

Accepted by Ayhan on 6 Nov. Classic (Blender Internal) materials with a
colour image read about 12 levels brighter in Remix than in the game (21 at
Diffuse Intensity 0.5). It is Remix's own reflection, not the colour: in
scene-linear light Remix adds about the same amount at every intensity
(+0.036 to +0.045), and its brightness rises with Diffuse Intensity as the
game's does (0.224 against 0.214 per unit). A matte tile beside them
(Specular Intensity 0, so roughness 1 in Remix) reads within 2 levels
(game [141.2, 136.2, 128.1], Remix [143.7, 137.6, 129.7]), while the same
tile with its specular reads 12.3 brighter. Remix's opaque materials always
reflect a little at the surface (a dielectric's few percent), which Blender
Internal draws only through its Specular settings: a material's Specular
Intensity and Hardness control how much of it shows (lower intensity or
hardness: rougher in Remix, less of it).

### The catalogue, built on first use (6 Nov)

A bake compiles its shader each time, about a second on the RTX 3080: a
full catalogue (14 presets, about 200 bakes each) takes over an hour. So
**Fit** builds it on first use, only for the presets an image may be: from
its kind (a regular image: Brick and the random presets nearest its main
colour; a random one: the random presets nearest its colour), four in all.
The progress bar shows it, and the console names each preset as it is
built. Each preset's catalogue is kept, so the next fit builds nothing for
it. **Build Catalogue** still builds every preset at once, for those who
want it all ready.

### Old files brought up to date (6 Nov)

When a file made before P10 opens in Blender (as P9.3's conversion does),
its Brick presets are mended in place: the value per brick no longer comes
from a Modulo node, so the black brick at each tile's corner goes. Every
material keeps its settings (Scale, Colour, Wear, Seed, the tile's size);
nothing else in the picture changes. The text "BGE-Remix conversion" says
which were brought up to date. It happens once: a mended file is left
alone the next time.

### Classic to RTX Remix keeps Diffuse Intensity (6 Nov)

Making a Classic material RTX Remix (Material: RTX Remix, or a file's
conversion) keeps its Diffuse Intensity: with a colour image, a Multiply
node by the intensity goes between the image and the RTX Remix node's
Albedo (the game draws it; Remix gets the image times the intensity, not a
bake); without one, the Albedo colour is the colour times the intensity, as
before. Materials made RTX Remix before keep what they have.

### RTX Remix's own texture tools

Remix's Toolkit has "AI Texture Tools": generative models that upscale
textures (up to 4x, later 8x) and make PBR maps (normal, roughness, height,
metallic) from a captured game's textures, including the community PBRFusion
models (a PBR model and a diffusion upscaler), and a REST API through which
other programs (ComfyUI, Blender) can drive the Toolkit while it runs. They
are machine learning, need the Toolkit (an Omniverse application) running
on a captured project, and work on Remix's captured assets rather than on a
Blender file's materials, so P10 does not use them ("not in P10": machine
learning). Our own derived channels stand in for them.

### Not in P10

Upscaling and learned fitting (machine learning); procedurals running
inside Remix (Remix only takes images).

## Atlases

These notes are for the texture remaster's atlases. The lead merges them
into the guide at sign-off.

Only RTX Remix gets the atlases. The game's OpenGL draws the presets live,
with no repeat, so it needs no tiles. Your file's meshes and images are
never changed: everything happens in what the game sends to Remix.

### Tile sets (no repeat in Remix)

A preset's Remix texture is one tile that repeats every metre. On a large
floor you can see the repeat. A tile set fixes that.

- **What it is.** The preset is baked as 16 tiles (or 81). Each corner of a
  tile has one of two colours (or three). Each colour is the preset at its
  own Seed. Two tiles fit side by side whenever their shared corners have
  the same colours.
- **What Remix gets.** The mesh sent to Remix is cut along its UV grid: one
  square for each UV unit. Each square shows the tile whose corners agree
  with its neighbours'. Which tile goes where comes from the square's place
  (a fixed hash), so it is the same every run.
- **The atlas.** All the tiles sit in one texture, laid out so that the
  atlas is itself a valid tiling. There is a gap round each tile, filled
  with the tile carried on past its edge. The mipmaps are made tile by tile,
  so far away the tiles' edges still agree.
- **Bricks.** For a regular preset (Brick), the colours' Seeds keep the
  bricks in step. Only the grain and the wear differ between tiles, and the
  brick layout itself still repeats, as a real wall does.
- **The default (your decision, 7 Nov).** Every random preset (Rock,
  Gravel, Tarmac, Grass, Bark, Dirt, Sand, Snow, Concrete, the skins) gets
  16 tiles in Remix without you doing anything, old files included. The
  tiles are 512 pixels each (512 a metre), so the atlas is 2048 pixels. The
  bake costs about a second a preset, once: after that the game finds it in
  its cache.
- **Not by default.** Brick (a regular pattern) and Water (its moving
  flipbook) keep their single tile unless you choose otherwise.
- **Your choice wins.** In the UV editor's sidebar, open *RTX Remix Atlas*:
  *Single Tile* turns the tiles off for that material, *Make Tile Set* picks
  16 or 81 tiles and their size, and *Default* goes back to the rule above.
  P10.5's Apply makes a tile set for fitted presets.
- **16 or 81?** 16 tiles cut the repeat at the tile's period to about two
  thirds of a plain tile (0.66, where a plain repeat is 1). 81 tiles cut it
  to about a half (0.55), with an atlas about five times larger. The test
  pictures decide (see below).

### Flipbooks (moving surfaces)

An animated preset, such as water, is baked as a looping run of 16 frames
(4 x 4) into a sprite sheet. Remix plays the sheet by itself, so the water
moves in Remix.

- **Speed.** The frame rate comes from the preset's speed: each frame moves
  the pattern by 1/64 of the tile. Water moves at 0.15 m/s, which gives 10
  frames a second, so the loop lasts 1.6 s.
- **The waves change shape (your decision, 7 Nov).** The water is baked 16
  times, once for each moment of the loop, so its waves form and fade
  rather than only sliding. Its normal map is made twice as strong, so the
  waves read in Remix. Water is glass in Remix, so only its normal map is
  baked 16 times; the rest once.
- **The loop.** Two layers of these frames, half a loop and half a tile
  apart, are blended so that each one jumps back to its start only while
  it is invisible. The last frame runs into the first without a jump.
- **Baked once.** The first start takes a few seconds for it; after that
  the game finds the sheet in its cache.
- **Remix 1.6.1 plays it.** Remix shows frame (seconds x frame rate) modulo
  16. It picks the frame by time for the whole material, never per face.
- **How.** In the *RTX Remix Atlas* panel, press *Make Flipbook*.
- **The game.** The game's OpenGL still draws the water as it is (still).

### Packing small images (an option, off by default)

*Pack Small Images* puts the game's small images (256 pixels or less) onto
shared pages. Each image gets a cell, with a gap round it filled with the
image carried on (wrapped when it repeats, its edge when it stops). Remix
then reads fewer textures. The picture is meant to stay the same, not get
better.

- **The materials.** Only materials whose images are all small and the
  same size are packed. The others are listed with the reason.
- **Far away.** Far away, a page's gap runs out and its cells can bleed into
  each other. When that would happen within the camera's range, the tool
  warns you and names the distance.
- **Where the pages go.** The pages are packed into the .blend, or saved as
  PNG files in a folder you give. They are kept when you save, even though
  nothing in Blender uses them (only the game reads them).
- **The Remix View.** It still shows the plain images. Only the game sends
  the pages.
- **Sharper in Remix (measured on your PC).** Up close, Remix showed small
  images on their own at a lower resolution than the same pixels on a page:
  a 128-pixel image looked like a 32-pixel one. This is Remix's own texture
  handling. Packed, they look as sharp as the original image, so packed and
  not-packed squares differ in colour by up to about 20 levels near the
  camera (1.6 far away).

### Picking a tile by hand

In edit mode, select faces and press a tile's number under *Selected faces
show* in the *RTX Remix Atlas* panel. *By Position* gives them back to the
hash.

The choice is kept in the mesh's face layer `bge_remix_cell`. It is saved
in the file, survives edit mode, and follows faces that are split or
copied.

*Show Cells* draws each UV square with the tile Remix shows there. A picked
tile is in yellow with a star.

*Check Atlas* checks three things, and marks what fails in red and selects
its faces:

- each tile's or cell's gap;
- where tiles meet;
- the faces' picks.

### Known limits

- **The Remix View.** It shows a preset's plain tile, not its tile set or
  flipbook. Only the game sends those.
- **Deformed meshes.** On a deformed mesh (an armature or a modifier), the
  pieces cut by the tile grid move with the nearest original vertex.
- **One preset, one atlas.** A material can have a tile set or a flipbook,
  not both.
- **Sprite sheets have no gaps.** Remix wraps the UVs inside each frame, so
  a frame's edge can show half a pixel of the next frame up close.
- **Mostly hidden repeats.** With 16 tiles, the same corner patterns still
  come back often. They no longer line up on a grid, which is what made
  the repeat visible.

## The Remix View: Edit and Run

Notes for Ayhan, to be merged into the guide at sign-off.

### Four modes

The Remix View's header has four mode buttons: Check, Debug, Edit and Run.
Each mode shows only its own tools in the sidebar (N):

| Mode  | Sidebar panels                                  |
|-------|-------------------------------------------------|
| Check | Element, Warnings, Targets, Layers              |
| Debug | Targets, Inspect, RTX Remix Statistics          |
| Edit  | Edit Element, Settings Hub                      |
| Run   | Run                                             |

The holograms show in Check, Edit and Run (not in Debug, as before).

### Edit mode

- **Click an avatar** (left click, or your select button) to select its
  element. Its object becomes the active object, so Blender's own editors
  (Properties, Logic) show the same thing.
- The **Edit Element** panel shows its settings, drawn from Blender's own
  data: a logic brick's settings, a body's physics (Blender's own Physics
  panels, drawn here), a sound's actuator and sound, a lamp's and a
  material's panels, a game property's value. A change here shows in
  Blender's editors and the other way round; undo works.
- **Drag from one avatar to another** to wire them:
  - a sensor to a controller, a controller to an actuator (either way round);
  - an actuator to the object it acts on (Track To, Add Object, Camera,
    Parent, Property Copy, Steering, Armature, a servo Motion's reference,
    a Scene actuator's camera, a Message actuator's "To");
  - a message to its listener (the listener's Message sensor gets the
    message's subject);
  - a child to its parent (the child stays where it is).
  A pair that cannot be wired is refused with a note in the status bar.
- **The Element menu** (header, the panel's menu button, or W over the view)
  adds a sensor, controller, actuator, body or sound to the selected
  avatar's object (or the active object), and removes the selected brick,
  body or sound.
- The warnings follow every edit (the model is rebuilt).

### The settings hub

The **Settings Hub** panel (Edit mode) lists every setting that affects the
game, once, grouped by your clusters (Entity, Factor, Behaviour, Gate) and
by layer: the scene's game settings, the pipeline and RTX Remix's
settings, the physics engine, the world, and each object's game settings
(one group per object, inside each layer). Each row is the setting itself;
the small icon beside it opens the Properties editor at the tab where
Blender also shows it. A row without an icon is shown only here (Blender's
panels show it only for other choices, such as the dome settings while
Stereo is off). Click a group's name to open or close it; the search field
finds settings by name.

How the hub knows where Blender shows a setting: it runs Blender's own
Properties panels with a stand-in layout that records what they draw. So the
links follow Blender's panels by themselves, and add-ons' game panels are
found too.

### Run mode

- **Run** plays the game inside the Remix View's own area. The header's
  choice: **Whole Scene**, or **Chosen Objects**: a mock-up of the selected
  objects plus what they need (their logic's targets, their parents and
  children, the objects they are jointed to, the camera, the lamps; the Run
  panel lists them and why). The rest is not in the game.
- While it plays, the **holograms stay over the game** and show its live
  state: each brick's colour (P10.10's states), what fired this frame (a
  green ring), each property's value and each body's contacts beside its
  name. The holograms are drawn over the picture (not hidden behind walls).
- **Suspend**: the Suspend button over the game (top left), or the Pause
  key. **Stop**: the Stop button, or the game's exit key (Esc). Nothing can
  be edited while the game plays. A game that hides the mouse cursor hides
  it here too: use the Pause key then.
- When suspended, the view goes to **Edit mode**, with the game frozen: the
  objects are where the game had them, the properties hold the game's
  values. Edit anything. The header shows "Suspended" with **Continue...**
  and **Stop...** in every mode.
- **Continue...** asks: **Resume with the changes** (the game goes on from
  where it was, with what you changed) or **Start over** (the game starts
  again from the beginning, with what you changed; what you did not change
  goes back to how the file had it).
- Resume takes: property values, places (location, rotation and scale), the
  logic's settings and links, the physics settings, and (with no extra
  work, see below) any other setting. It cannot take a new or removed
  object, a new or removed brick (or one whose type changed), a new or
  removed game property, a new parent, or a game that had gone to another
  scene: then only Start over is offered, and the pop-up says why.
- **Stop...** asks whether to keep the changes made while suspended.
  **Yes** keeps them (what the game itself moved goes back to the file's
  values); **No** puts the file back exactly as it was when Run started.
  If the game was never suspended, nothing changed and nothing is asked.

#### How suspend works

Blender 2.79's game inside Blender blocks Blender until it ends. Suspend
ends the game (as Esc does) after keeping a **snapshot** of its state, by
object names: every object's place, velocity, logic states, visibility and
properties, the objects the game added and ended, the active camera. The
snapshot's places and property values are written into the file, so you
edit the frozen game. Resume starts the game again from the file (with
your changes) and puts the rest of the snapshot back (velocities, states,
added and ended objects, the camera).

Because the game is really gone while suspended, nothing can crash if the
file changes then: undo, deleting objects, even opening another file (the
run is then forgotten). What a snapshot cannot hold starts afresh on
Resume: Python's own variables (anything a script keeps outside game
properties), actions part-way through, sounds, delay and random sensors'
counters, constraints made by scripts.

When Run starts, the file is kept in memory (as an undo step is); Stop,
then No, puts it back. That is itself an undo step, so it can be undone.

Saving while suspended saves the game's frozen places and values.

### Known differences in RTX Remix

- The holograms over the game are drawn after RTX Remix's picture, into the
  same frame; Remix's picture has no depth, so the holograms are always in
  front (in Check and Edit mode they hide behind walls).
- RTX Remix keeps running between Suspend and Resume (the game ends, Remix
  does not), so Resume does not wait for Remix to start again; the scene is
  sent to Remix again, so its textures take their usual moment.

## The logic bricks

The bricks stay, and old games play as before. Node logic is not part of
P10. The bricks' old limits (P10.13) are listed in
`bge_remix_p10_brick_limits.md`; the approved ones are built (the last
section below).

### P10.10: each brick's live state

While a game runs, each brick has a state in your colours. Run mode (the
Remix View's P10.9) draws it on the brick's avatar. The engine only reads
the bricks to find their state: a game plays exactly the same with or
without it (test T10.c3).

#### Every brick

| Colour | When |
|---|---|
| Red | Its object, target or file is missing. This covers: no object chosen (Track To, Add Object, Camera, Set Parent, Copy Property); no action (Action, Shape Action); no target or navigation mesh (Steering); no sound, or a sound file that is not there; a Start Game file that is not there; no scene (Set or Add Scene) or camera; a custom 2D filter with no text. Also a target that ended during the game. |

#### Sensors

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also: an Actuator sensor watching an actuator its object does not have, or a Property sensor watching a property its object does not have. |
| Green | It fired a positive pulse this tic, or it is positive (a key held, an Always sensor). |
| Black | No controller of a state the object is in listens to it: the engine does not run it. Also when it is suspended. |
| Yellow | A Delay sensor counting, or a Timer sensor not yet due. |
| White | Ready, not positive. |

#### Controllers

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also a Python controller with no script, or one that failed to compile or import. |
| Green | It ran this tic and sent "on" (And, Or, Nand, Nor, Xor, Xnor). A Python or Expression controller is Green whenever it ran. |
| Black | Its state is not one the object is in. |
| White | Ready. |

#### Actuators

| Colour | When |
|---|---|
| Red | Any reason under "Every brick". Also an RTX Remix actuator in a scene that does not use Remix. |
| Green | It got an "on" pulse this tic. An RTX Remix actuator is also Green on the tic it sends its setting. |
| Yellow | An Action actuator asked to play while another action holds its layer, or an RTX Remix actuator waiting for Remix to start. |
| Blue | Still at work after its pulse: a motion or servo motion, an action or sound playing, a camera following, a constraint, a steering. |
| Black | No controller of a state the object is in drives it. |
| White | Ready, idle. |

For the tests, the player can write every brick's state at every logic tic
into a file. Set `BGE_REMIX_P10_BRICK_LOG=<file>` to turn it on. Each line
holds the tic, scene, object, brick, kind and state. Unset, it costs
nothing.

### P10.11: states and messages in the Remix View

In the logic layer, each object's bricks stand in columns, one per state.
- **The group**: each column is topped by its state's group ("State 2"). A dashed line runs from each group to every controller in that state.
- **The starting states**: the states the object starts in are marked Green. The live ones come with Run mode.
- **State actuators**: each one is an arrow from the state it is driven in to the state it enters.

In the messages layer, each message's arc now ends in an arrowhead at its
listener. P7's warning for a message nobody hears stays.

**Starting states** (Edit mode): the Starting States panel in the Remix
View's sidebar shows the selected object's starting states, which you can
toggle (states 1 to 30). It also lists which states its bricks use, which
ones State actuators enter, and any state that is never entered.

**A message's subject** (logic editor): beside the Subject field of a
Message sensor or Message actuator there is a small list button. It lists
the subjects the game's bricks already use, so a typing slip cannot break
a message. You can still type a new subject.

### P10.12: two new bricks

**The Timer sensor** (Seconds, Repeat): it fires once, Seconds after its
state becomes active. With Repeat ticked, it fires every Seconds. It counts
the game's time as a timer property does, and needs no property. Each firing
is "on" for one logic tic. Python: `SCA_TimerSensor`, with `seconds` and
`repeat`.

**The RTX Remix actuator** (Setting, Value): on a pulse, it sends one of RTX
Remix's own settings to Remix, as `bge.render.remixSetOption` does.
- **The settings list**: the list button beside Setting holds the Render tab's settings and the quality settings. Picking one fills in its default value. You can also type any `rtx.*` setting.
- **Value**: as Remix writes it: True or False, a number, or "a, b".
- **While Remix starts**: a pulse given while Remix is still starting (about half a minute) is kept and sent on Remix's first frame. The actuator shows Yellow while it waits.
- **In the Classic pipeline**: it does nothing, and the Remix View warns about it (on the Remix layer).
- **Python**: `KX_RemixActuator`, with `setting` and `value`.
- **Settings the game sets itself**: the game sends the camera's effects to Remix itself (vignette, motion blur, bloom, auto exposure) whenever the camera's effects change, and that overrides what an actuator set. The list leaves those settings out.

**Stock 2.79b**: a file using either brick still opens and plays in stock
2.79b, where the bricks do nothing. Stock does not know their type or
data. It reads them without their data, and its converter makes nothing for
them. The rest of the game runs (test T10.c9). If stock 2.79b saves the
file, the two bricks lose their settings. BGE-Remix gives them their
defaults again: 1 second, and no setting.

### P10.13: the bricks' old limits, fixed (approved 6 Nov)

The full list, with each entry marked fixed, option or warning, is in
`bge_remix_p10_brick_limits.md`. Old games play exactly as before. Each
option is kept in the file and is off in every old file, and the files still
open in stock 2.79b, which keeps the options and ignores them.

**Fixed:**
- **A controller in several states**: Shift+click in the logic editor's state menu adds or removes a state. The header shows "2+" for a controller in more than one.
- **Logic Priority**: set per object in the logic editor's sidebar (Logic Order). Objects with a higher number run their controllers first in each logic tic. At 0 (every old file) the order is the one stock 2.79b gives.
- **The Delay sensor**: takes up to 32767 tics in the editor (stock: 5000). The file holds no more than that; for longer waits, use the Timer sensor.
- **The Message sensor's From and Body**: optional filters. When set, the sensor only hears messages from that object, or with that body. Empty means any, as before.

**Options, off unless you turn them on:**
- **Add Once per Message** (Property actuator, Add mode): two messages in one tic add twice, not once.
- **Read Held Keys** (Keyboard sensor): a key already held when the sensor's state is entered fires the sensor once. Stock waits for a new press, unless the sensor's Level is on.
- **Stop Left State Now** (State actuator): the running actuators of the states it leaves stop in the same tic, not one tic later.
- **Own Max Physics Steps** (World tab, Physics): Max Physics Steps works as labelled. Stock reads Max Logic Steps for it. It is on in new files and new scenes, and off in old files.
- **Timers Follow Game Time** (World tab, Physics): when the game runs slower than its logic rate, timers, Timer sensors and added objects' life count the game time each logic step stands for. This only matters with Own Max Physics Steps on: without it, a logic step never stands for more than one tic.

**New warnings in the Remix View's logic layer:**

| Code | Colour | When |
|---|---|---|
| LOG_NO_TARGET | Red (was Black) | An actuator aimed at nothing. |
| DAT_PROP_MISSING | Red (was Black) | A Property sensor watching a property its object does not have. |
| LOG_NO_ACTUATOR | Red | An Actuator sensor watching an actuator its object does not have. |
| LOG_NO_FILE | Red | A Start Game file that is not there. |
| LOG_EXPR_FULL | Note | An expression that fills its 127 characters. |
| LOG_EXPR_NAME_CLASH | Note | An expression name that is both a linked sensor and a property (the sensor is read), or a sensor not linked to the controller (it cannot be read). |
| LOG_SEES_ACTORS | Note | A Near or Radar sensor, and no other object with Actor on. |
| LOG_SHAPE_NO_KEYS | Note | An action that animates shape keys, none of them the mesh's. |

Bricks are now told apart by kind as well as name, so a sensor and an
actuator with the same name no longer mix up their warnings.

For the tests only: `BGE_REMIX_P10_HOLD_KEY=<key>:<tic>` presses a key (a
digit or a letter) at that logic tic and holds it.

## Testing your own work (P10)

- **Blender for testing by hand:** start Blender with
  `tests\bge_remix\blender_remix.bat` (optionally with a file). It runs the
  build under Windows' debugger and keeps every run's logs in
  `C:\BGE-Remix\crash-reports` (the console, Remix's own log, and on a crash
  a summary with the functions it crashed in). The tests never touch that
  folder.
- **The crash check:** `run_local.bat smoke` (or `smoke\run_smoke.bat`)
  opens Blender's window and does what a person does by hand: Solid straight
  to Rendered with Remix on, orbiting, edit mode, adding and deleting, undo,
  the other shadings, P to play, F12, the Remix View's four modes, saving. A
  crash is reported with the step it happened in and its stack. Given a file
  (`smoke\run_smoke.bat "D:\path\file.blend"`), it runs the same steps on
  that file (playing only with BGE_REMIX_SMOKE_PLAY=1).
- **Fewer reruns:** `set BGE_REMIX_P10_TRACKS=p10a,p10c` before
  `run_local.bat p10` (or the RTX run) runs only those tracks.
- **GitHub's pre-flight:** every push checks that the test scripts compile
  and that every P9 and P10 test scene can be made, before a run on the PC.
