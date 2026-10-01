# Zero Comico for ScummVM

Experimental ScummVM engine for **Zero Comico** (Windows, 2001, GMM Entertainment / Medusa Games).

This repository is an engine overlay, not a fork of the whole ScummVM tree. Copy `engines/zerocomico` into a current ScummVM source checkout, then configure with `--enable-engine=zerocomico` and rebuild.

## Current state

The engine now has a substantial native data/runtime foundation:

* exact detection for the Italian retail executable examined from `zero.zip`;
* native C++ decoder for the `JFX1` container and its LZHUF bitstream;
* native C++ decoder for `JGF5` images with the retail BGRA pixel layout;
* parser for all seven compressed text-script families (`.gsc`, `.isc`, `.mat`, `.par`, `.shp`, `.scr`, `.seq`), including the inline and slightly malformed brace patterns present in shipped data;
* script VM foundation with scalar variables, arrays, indexed references, arithmetic, numeric comparisons, labels/jumps, plus the retail `wjmp` scheduler boundary and keyboard conditionals used by the real-time minigames;
* parser for the text `.bsp` walkable-floor BSP and pathfinding graph, now preserving the retail per-cell `support` metadata instead of discarding it;
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
* native parsing of `Shape.shp` position/range markers and BSP graph shortest-path helpers;
* parsing of `Giovanni.seq`, including walk/run transition clips and commented-out retail sequence blocks;
* skeletal bind-pose reconstruction from P3D flesh weights plus runtime JACS bone-pose evaluation;
* native `Videos.isc` cutscene timelines with frame-indexed sound, speech/subtitle, fade and environment-event parsing;
* native JACS cutscene playback from the shipped P3D/ANJ data, including animated camera/target tracks, camera roll, character skeletons, rigid-object animation and visibility toggles;
* retail speech enumeration for cutscene lines (`Aldo0000.mp3`, `giovanni0000.mp3`, etc.) and timeline sound-effect playback;
* gameplay `PlaySample` sound-effect routing plus real `wait_frames` timing at the retail 25 fps script cadence;
* looping room music from the `Music:` declarations in `room.isc`, including the shipped per-room volume values and seamless volume changes when a track is reused;
* looping environment audio driven by `set_envsound` / `envsound_state`, including cutscene `Videos.isc` environment-sound events;
* persistent `playl` room-object animations and continuously sampled Giovanni idle animation during gameplay;
* native parsing of `puzzle.isc` object definitions, including entity bindings, examine text and executable `operate` ranges;
* screen-space picking of interactive room meshes through the gameplay camera, with right-click examination text;
* puzzle-driven `SetCharPos_Vector` / `chplace` room transitions, including the retail `d101_dor` door cutscene path for the first Mp1 doors;
* playable-character metadata from `char.isc`, including `AnimSet`, `InitialAnimSet` and the retail `combineobj` inventory block;
* inventory possession/selection and retail `ifobjselected`, `ifobjininv`, `ifallobjnoselected` and `ifcombine` conditions, with `take`, `addobjininv`, `subobjininv` and `SelectObjInInv` state changes;
* live inventory combination execution from the shipped character scripts, so recipes such as Mp1's fruit and potion chains update the same script variables and inventory objects as the original data;
* generic player-body loading from the declared animation set, including Aldo and Giacomo, plus runtime `SetAnimSet` swaps used by the later chapters;
* direct scripted camera overrides through `SetFocus`, `SetCamera` and `ResetCamera`, automatic `camera.gsc` / `camera.shp` trigger zones, and live Placed/Subjective/Spot camera modes; dynamic camera positions are projected back into the retail `MapCam` space and Spot mode uses the decoded retail distance/height/smoothing parameters;
* `ChangeMainplace` chaining and retail `setmap` switching, including the shipped `SetPlace` → `SetMap` ordering where the requested map is deferred until the destination room loads, with paired `MapCam` updates where declared;
* native Mp5 portal traversal using the declared room portal links and the physical `ge_Shape ... Portal` A/B segments from `Shape.shp`, including reciprocal logical portal names that share one physical boundary;
* persistent `Setp` scene assets and hierarchy-driven `playl` controllers, including Mp2's `r23_dummyossa` water controller and indexed tube/button transforms used by the shipped minigames;
* retail `CloneEntity` side effects reconstructed for Mp2's 18 `Star_Star` clones, with `SetEntityPos_Vector` marker placement/orientation, room-specific `InsertInBackground` persistence and helper-texture lookup;
* New Game now follows `ChangeMainPlace mp1`, executes Mp1's startup and runtime blocks, plays the native `c111` opening cutscene, displays the following `csay Giovanni "Mo' vado!"`, resolves `r11_Start`, loads the declared room scene/camera/BSP state, spawns Giovanni, and accepts mouse interaction plus BSP-routed movement.

The project is **not yet a completable port**, but the first live gameplay-shaped 3D runtime is now present. A CPU software renderer applies decoded object transforms, z buffering, perspective-correct UVs and JGF5 textures, with a persistent decoded-texture cache for animated scenes. The original `interfaccia.p3d` menu is rendered and can be navigated with the keyboard. Help opens the shipped help image, Credits plays `crediti.avi`, and Exit quits.

New Game now executes the real Mp1 runtime rather than jumping directly to a static room. The shipped `C111.p3d/.anj` cutscene is posed frame by frame at 25 fps using its animated camera and target, camera-roll channel, JACS character hierarchies, visibility streams and the `Videos.isc` timeline. Frame presentation is deadline-locked rather than sleeping after each render, preventing renderer cost from accumulating into audio/subtitle drift. The teleport sound, Aldo's enumerated `Aldo0000.mp3` speech and subtitle are routed during the sequence, after which the room runtime reaches Giovanni's `"Mo' vado!"` line and hands control to `room1_1`. The same timeline player is generic for the other Mp1 cutscene asset pairs and handles their mixed filename case.

Gameplay keeps the declared `playl` background animations alive, including `r11_Terra Gira` in the start room, and now starts the room's declared looping MP3 music at its retail volume. Giovanni's idle pose is sampled continuously. Mouse clicks are projected onto the ground plane, snapped to the retail BSP graph and routed with Dijkstra; the `cammina` sequence follows the retail `0>1` start, `1>1` loop and `1>0` stop transitions, with TCB interpolation and movement speed derived from JACS root motion. Interactive meshes are now picked in screen space: right-click displays the object's retail `examine_text`, while operable entities execute their `puzzle.isc` ranges. Inventory state is live rather than decorative: TAB selects acquired objects, C combines the selected pair through the level's real `combineobj` program, and possession/selection conditionals feed back into puzzle branches. Chapter-ending `ChangeMainplace` calls can continue into the next retail main place, and the active player body is selected from that chapter's `char.isc` data, including Aldo/Giacomo and later `SetAnimSet` changes. The project is still not a completable port. CPU-character instantiation, life scheduling, one-shot animation and data-driven step events are now active. The largest remaining runtime gaps are end-to-end playthrough validation of later-chapter puzzle/dialogue behavior, dynamic-light/shading fidelity, and final hardening of ZCOM v4 save/load. Retail sample distance attenuation and generic `PlaySample` class routing are now reconstructed from the executable and active in the runtime. Camera trigger regions, Subjective/Spot movement, keyboard-driven `wjmp` minigame loops, persistent `Setp` assets, Mp2 cloned-star backgrounds and Mp5 spatial portal traversal are now represented natively.

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
