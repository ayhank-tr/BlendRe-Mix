# BGE-Remix P10: the texture remaster (P10.1-P10.3, P10.5)

Notes for the guide (track p10a; the atlases, P10.4, are their own track).

## The RTX Remix Remaster panel (Image editor, sidebar)

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

## The value check (P10.3, part of the audit)

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

## Fixed in P10

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

## Known difference: Classic materials read a little brighter in Remix

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

## The catalogue, built on first use (6 Nov)

A bake compiles its shader each time, about a second on the RTX 3080: a
full catalogue (14 presets, about 200 bakes each) takes over an hour. So
**Fit** builds it on first use, only for the presets an image may be: from
its kind (a regular image: Brick and the random presets nearest its main
colour; a random one: the random presets nearest its colour), four in all.
The progress bar shows it, and the console names each preset as it is
built. Each preset's catalogue is kept, so the next fit builds nothing for
it. **Build Catalogue** still builds every preset at once, for those who
want it all ready.

## Old files brought up to date (6 Nov)

When a file made before P10 opens in Blender (as P9.3's conversion does),
its Brick presets are mended in place: the value per brick no longer comes
from a Modulo node, so the black brick at each tile's corner goes. Every
material keeps its settings (Scale, Colour, Wear, Seed, the tile's size);
nothing else in the picture changes. The text "BGE-Remix conversion" says
which were brought up to date. It happens once: a mended file is left
alone the next time.

## Classic to RTX Remix keeps Diffuse Intensity (6 Nov)

Making a Classic material RTX Remix (Material: RTX Remix, or a file's
conversion) keeps its Diffuse Intensity: with a colour image, a Multiply
node by the intensity goes between the image and the RTX Remix node's
Albedo (the game draws it; Remix gets the image times the intensity, not a
bake); without one, the Albedo colour is the colour times the intensity, as
before. Materials made RTX Remix before keep what they have.

## RTX Remix's own texture tools

Remix's Toolkit has "AI Texture Tools": generative models that upscale
textures (up to 4x, later 8x) and make PBR maps (normal, roughness, height,
metallic) from a captured game's textures, including the community PBRFusion
models (a PBR model and a diffusion upscaler), and a REST API through which
other programs (ComfyUI, Blender) can drive the Toolkit while it runs. They
are machine learning, need the Toolkit (an Omniverse application) running
on a captured project, and work on Remix's captured assets rather than on a
Blender file's materials, so P10 does not use them ("not in P10": machine
learning). Our own derived channels stand in for them.

## Not in P10

Upscaling and learned fitting (machine learning); procedurals running
inside Remix (Remix only takes images).
