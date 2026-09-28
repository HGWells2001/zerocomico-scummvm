# Zero Comico for ScummVM

Experimental ScummVM engine for **Zero Comico** (Windows, 2001, GMM Entertainment / Medusa Games).

This repository is an engine overlay, not a fork of the whole ScummVM tree. Copy `engines/zerocomico` into a current ScummVM source checkout, then configure with `--enable-engine=zerocomico` and rebuild.

## Current state

The engine now has a substantial native data/runtime foundation:

* exact detection for the Italian retail executable examined from `zero.zip`;
* native C++ decoder for the `JFX1` container and its LZHUF bitstream;
* native C++ decoder for `JGF5` images with the retail BGRA pixel layout;
* parser for all seven compressed text-script families (`.gsc`, `.isc`, `.mat`, `.par`, `.shp`, `.scr`, `.seq`), including the inline and slightly malformed brace patterns present in shipped data;
* script VM foundation with scalar variables, arrays, indexed references, `mov`, `if_e` / `else` / `endif`, labels and `jmp`;
* parser for the text `.bsp` walkable-floor BSP and pathfinding graph;
* lossless `.p3d` / `.anj` record parser with nested `0xF044` groups and ambiguous-end-marker backtracking;
* semantic decoding for P3D materials, cameras, lights, meshes, UVs, normals, material ranges, skin/flesh data and transforms;
* semantic decoding for JACS hierarchy records and `0xF007` animation clips, including TCB keyframes and visibility events;
* paired scene loader that builds a native material/camera/light/mesh/hierarchy/animation asset set;
* JFX1-wrapped FLIC playback support for animated-texture resources;
* room bootstrap driven by the real `Mp0/gameplay/room.isc` script instead of a hard-coded intro sequence;
* AVI/Indeo playback through ScummVM for `play_CD_film`;
* loading of the real main-menu `interfaccia.p3d` + `interfaccia.anj` asset pair during bootstrap;
* textured software rendering of decoded JapoTek meshes with camera projection and z buffering;
* live keyboard navigation of the original 3D main menu, including Help, Credits and Exit actions;
* parsing of main-place room definitions and scripted gameplay cameras from `room.isc` / `Camera.scr`;
* New Game now follows `ChangeMainPlace mp1`, loads the declared `room1_1` P3D/ANJ scene, the retail gameplay camera, and both BSP navigation maps, then renders the first-room preview.

The project is **not yet a completable port**, but the first live 3D runtime is now present. A CPU software renderer applies decoded object transforms, z buffering, perspective-correct UVs and JGF5 textures. The original `interfaccia.p3d` menu is rendered and can be navigated with the keyboard. Help opens the shipped help image, Credits plays `crediti.avi`, and Exit quits. New Game now crosses into Mp1 far enough to parse the start-room declaration, load `room1_1.p3d/.anj`, use the camera defined in `Camera.scr`, load the walk/camera BSP maps, and render a room preview. Character spawning, room startup/cutscene execution, interaction, and Load/Save are still pending.

The next major layer is turning that Mp1 preview into gameplay: execute the room startup/cutscene state, apply JACS visibility/animation continuously, evaluate material effects and animated FLC textures, spawn and move Giovanni on the parsed navigation graph, and expand opcode coverage for interaction, dialogue/audio routing, inventory and save/load.

## Game data layout

Point ScummVM at the installed `Zero Comico` data directory, the directory containing `Zero Comico.exe`, `Config.gsc`, `Mp0` ... `Mpx`, `images`, `Music`, `Sound` and `Speech`.

The original videos live on the CD under `Data/`. For intro/cutscene playback, copy that `Data` directory into the same game-data directory. The engine will still start without it and simply skip the requested film.

`tools/prepare_from_zip.py` can construct that layout from a complete `zero.zip` archive.

## Build

```sh
cp -a engines/zerocomico /path/to/scummvm/engines/
cd /path/to/scummvm
./configure --enable-engine=zerocomico
make -j4
```

On Windows, regenerate the Visual Studio project after adding the engine in the same way you do for any other out-of-tree ScummVM engine.

GitHub Actions continuously overlays this engine onto current ScummVM master and builds it, so compile regressions are caught against upstream.

## Verified retail files

The examined Italian release uses:

```text
Zero Comico.exe  size 1658880  MD5 741b42094a37b66ba96ece978e248d96
Config.gsc       size 74       MD5 6ebce334d2118fdf554345ce7fe6067e
```

## Reverse-engineering references

The format work was checked against the user-provided retail game data and against the public measurement repositories:

* https://github.com/vs-sr-dev/pc-zerocomico-doc
* https://github.com/vs-sr-dev/pc-bloodandlace-doc

Those repositories are used as format documentation. No game assets are committed here.
