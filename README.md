# Zero Comico for ScummVM

Experimental ScummVM engine for **Zero Comico** (Windows, 2001, GMM Entertainment / Medusa Games).

This repository is an engine overlay, not a fork of the whole ScummVM tree. Copy `engines/zerocomico` into a current ScummVM source checkout, then configure with `--enable-engine=zerocomico` and rebuild.

## Current state

The first implementation milestone is now code rather than a placeholder:

* exact detection for the Italian retail executable examined from `zero.zip`;
* native C++ decoder for the `JFX1` container and its LZHUF bitstream;
* native C++ decoder for `JGF5` images, including the corrected **BGRA** pixel order;
* decoder for the text-based script resources (`.gsc`, `.isc`, `.mat`, `.par`, `.shp`, `.scr`, `.seq`);
* parser for the text `.bsp` walkable-floor BSP and pathfinding graph;
* lossless parser for the known `.p3d` / `.anj` record envelope, including nested `0xF044` groups and ambiguous end-marker backtracking;
* optional playback of `Data/Intro.avi` through ScummVM's AVI/Indeo 5 decoder;
* bootstrap runtime that displays an original decoded menu/interface texture.

This is **not yet a completable port**. The remaining critical layer is the actual 3D runtime: `.p3d` model bodies, `.anj` animation semantics, material/animated-texture behavior, camera/entity binding and the script VM/opcode implementation. The P3D/ANJ record envelope is already understood well enough to parse all known Zero Comico files, but the record bodies still need semantic mapping before faithful rendering is possible.

## Game data layout

Point ScummVM at the installed `Zero Comico` data directory, the directory containing `Zero Comico.exe`, `Config.gsc`, `Mp0` ... `Mpx`, `images`, `Music`, `Sound` and `Speech`.

The original videos live on the CD under `Data/`. For intro/cutscene playback, copy that `Data` directory into the same game-data directory. The engine will still start without it and simply skip the intro.

`tools/prepare_from_zip.py` can construct that layout from a complete `zero.zip` archive.

## Build

```sh
cp -a engines/zerocomico /path/to/scummvm/engines/
cd /path/to/scummvm
./configure --enable-engine=zerocomico
make -j4
```

On Windows, regenerate the Visual Studio project after adding the engine in the same way you do for any other out-of-tree ScummVM engine.

## Verified retail files

The examined Italian release uses:

```text
Zero Comico.exe  size 1658880  MD5 741b42094a37b66ba96ece978e248d96
Config.gsc       size 74       MD5 6ebce334d2118fdf554345ce7fe6067e
```

## Reverse-engineering references

The format work was checked against the user-provided retail game data and against the excellent public measurement repository:

* https://github.com/vs-sr-dev/pc-zerocomico-doc
* https://github.com/vs-sr-dev/pc-bloodandlace-doc

Those repositories are used as format documentation. No game assets are committed here.
