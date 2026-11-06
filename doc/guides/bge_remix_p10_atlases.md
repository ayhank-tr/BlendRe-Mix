# BGE-Remix P10.4: atlases (notes for the guide)

These notes are for the texture remaster's atlases. The lead merges them
into the guide at sign-off.

Only RTX Remix gets the atlases. The game's OpenGL draws the presets live,
with no repeat, so it needs no tiles. Your file's meshes and images are
never changed: everything happens in what the game sends to Remix.

## Tile sets (no repeat in Remix)

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

## Flipbooks (moving surfaces)

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

## Packing small images (an option, off by default)

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

## Picking a tile by hand

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

## Known limits

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
