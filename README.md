# BlendRe-Mix

BlendRe-Mix (project name: BGE-Remix) is a fork of Blender 2.79b that adds the
NVIDIA RTX Remix path tracer as an optional renderer for the Blender Game
Engine, the 3D view and final renders, plus OpenUSD import/export for RTX Remix
mods and captures.

Remix is reached only through the Remix SDK's C API, loaded at runtime from the
official NVIDIA runtime. OpenGL stays the default renderer, and every stock
2.79b feature keeps working when Remix is off or not installed.

## Status

| Phase | Content | State |
| --- | --- | --- |
| Base | Stock Blender 2.79b source | Imported |
| P0 | Visual Studio 2022 build, Windows CI, test harness | Done (bge-remix-p0) |
| P1 | Remix bridge DLL and runtime loader | Next |
| P2 | Game engine, static scenes | Planned |
| P3 | Materials and texture cache | Planned |
| P4 | Skinning, physics, particles, picking | Planned |
| P5 | "RTX Remix" render engine for the 3D view and F12 | Planned |
| P6 | OpenUSD mod export and capture import | Planned |
| P7 | Hardening and release | Planned |

## Platform

Windows 10/11, 64-bit, with an NVIDIA RTX GPU for Remix. Without an RTX GPU or
the Remix runtime, it behaves like stock Blender 2.79b.

## Upstream base

- Blender `v2.79b`, upstream commit `f4dc9f9d68bddaa206b692e1d077d1a1f2bb1528`
  (22 March 2018), from <https://github.com/blender/blender>.
- The 2.79b submodules are included as plain folders at their pinned commits:
  - `release/scripts/addons`: blender-addons `2609a1018e46`
  - `release/datafiles/locale`: blender-translations `1bbc9bdabe4e`
  - `source/tools`: blender-dev-tools `7695e14cfc58`
- `release/scripts/addons_contrib` is left out, as in official 2.79b releases.

## License

Blender is licensed under the GNU GPL v2 or later (see `COPYING` and
`doc/license/`). New code in this fork uses the same license. The RTX Remix
runtime is not included in this repository. It is downloaded separately from
NVIDIA's official releases.
