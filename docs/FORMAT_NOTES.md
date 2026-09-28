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

The decoder was validated against the retail `Config.gsc` and JGF5 images from the provided archive before being translated to C++.

## BSP

The `.bsp` files are plain CRLF text and describe a two-dimensional walkable floor, a BSP partition and a navigation graph. They are not rendering BSPs. The parser recognizes the complete `scene -> bsp -> pathfinding` sequence, including tree preorder null markers and the support block.

## P3D / ANJ envelope

After JFX1 decompression, `.p3d` and `.anj` use the same record envelope. Known file headers are `02 00 3D 0E` and `01 00 3D 0E`. Normal records start with `BB AA` plus a little-endian type. Type `0xF044` is a group with a 32-bit length at +4. Named records carry a 32-byte NUL-terminated name and end at `ED FF FF`; the file trailer is `00 ED FF FF`.

The envelope can be walked losslessly, but the semantic layout of model/animation record bodies is still the largest blocker for faithful 3D gameplay.
