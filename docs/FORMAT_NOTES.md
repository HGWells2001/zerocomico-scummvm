# Zero Comico format notes used by the ScummVM engine

These are the concrete invariants implemented by the engine.

## JFX1

```text
+0   char[4]  "JFX1"
+4   u32 LE   decoded size
+8   u32 LE   encoded size
+12  byte[]   LZHUF stream
```

The file size must be `12 + encoded size`.

## JGF5

The files are normally named `.tga`, but they are not Targa files.

```text
+0   char[4]  "JGF5"
+4   u32 LE   0
+8   u32 LE   3
+12  u32 LE   width
+16  u32 LE   height
+20  u32 LE   decoded size
+24  u32 LE   encoded size
+28  byte[]   LZHUF stream
```

For every examined retail image, `decoded size == width * height * 4`. Pixels are BGRA.

## LZHUF

The payload codec is the classic Okumura-style LZHUF arrangement:

* 4096-byte LZSS ring buffer, initially filled with spaces;
* maximum match length 60;
* threshold 2;
* adaptive Huffman alphabet of 314 symbols;
* static prefix coding for the upper six bits of match positions;
* the container supplies the decoded size, so there is no inner length prefix.

The decoder was validated against the retail `Config.gsc`, scripts, models and JGF5 images from the provided archive before being translated to C++.

## Script text

The seven script extensions share the same line-oriented language after JFX1 decompression. Braces can be block delimiters or balanced inline payload delimiters, for example:

```text
array if_BMap { 0 1 2 3 4 5 6 }
Texture arcob.flc { usereffect film "loop delay 8" }
```

The shipped data also contains two generator quirks that the retail engine accepts: one material with a surplus closing brace and one scene file whose `End.` acts as an implicit final closure. The ScummVM parser deliberately remains tolerant of those cases.

The VM currently implements scalar variables, arrays, indexed references, `mov`, `if_e` / `else` / `endif`, labels and `jmp`. Engine-specific opcodes are delegated to the runtime host.

## BSP

The `.bsp` files are plain CRLF text and describe a two-dimensional walkable floor, a BSP partition and a navigation graph. They are not rendering BSPs. The parser recognizes the complete `scene -> bsp -> pathfinding` sequence, including tree preorder null markers and the support block.

## P3D / ANJ envelope

After JFX1 decompression, `.p3d` and `.anj` use the same record envelope. Known file headers are `02 00 3D 0E` and `01 00 3D 0E`. Normal records start with `BB AA` plus a little-endian type. Type `0xF044` is a group with a 32-bit length at +4. Named records carry a 32-byte NUL-terminated name and end at `ED FF FF`; the file trailer is `00 ED FF FF`.

The parser backtracks when `ED FF FF` occurs inside body data and accepts only a record boundary that tiles the enclosing group/file exactly. This closes all 526 retail P3D/ANJ files examined.

## P3D semantic records

The current decoder maps the core retail record bodies:

* `0xF000`: material state, colors and optional texture name/parameters;
* `0xF001`: camera position, target, field of view and optional clip range;
* `0xF002`: light state, color, position/direction, parameters and optional links;
* `0xF003`: object transform, vertices, triangle indices, material face ranges, UVs, normals and skin/flesh data.

The 3x3 object matrix is transposed while loading, matching the retail loader behavior observed during reverse engineering. Vertex positions are stored in the same local representation used by that loader, with the translation-minus-pivot offset applied.

## ANJ / JACS animation

`0xF007` records contain JACS animation clips. They carry named target tracks with Tension/Continuity/Bias keys for transform, light, camera and target channels. Object tracks can also include integer-frame visibility events. Hierarchy records use several JACS record types and map named objects to parents.

## Wrapped FLC animated textures

The retail `.flc` files are JFX1 containers. After decompression, the payload is a standard Autodesk FLC stream:

```text
+0   u32 LE  decoded FLC file size
+4   u16 LE  0xAF12
+6   u16 LE  frame count
+8   u16 LE  width
+10  u16 LE  height
+12  u16 LE  pixel depth
...
```

This was checked directly across the provided retail `.flc` set. The engine exposes them through ScummVM's `Video::FlicDecoder`.

The remaining 3D work is no longer binary-format discovery. It is runtime integration: applying transforms, camera projection, material/texture state, animation, visibility and script-driven entity behavior in the renderer.
