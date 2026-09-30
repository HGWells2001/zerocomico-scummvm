# Cutscene runtime notes

These notes record measurements from the retail Zero Comico data used by the
ScummVM engine. No game assets are included.

## Mp1 startup sequence

The Mp1 `room.isc` runtime starts persistent room animations, hides
Giovanni's hat, applies master-color fade commands, plays `c111`, waits for
it, restores the gameplay camera mode and then executes:

`csay Giovanni "Mo' vado!"`

The engine now follows that runtime block instead of jumping directly from
the menu to a static `room1_1` preview.

## C111

The shipped opening cutscene is a native JapoTek scene, not an AVI:

- `Mp1/videos/C111.p3d`
- `Mp1/videos/C111.anj`
- `Mp1/videos/c111.mat`
- `Mp1/videos/C111.par`

Its ANJ source spans frames 0 through 300. It contains animated JACS
hierarchies for Giovanni, Aldo and Giacomo, rigid scene objects, visibility
event streams, and the animated camera `c111_cam_cut01` plus its
`.target` track.

The software runtime samples those tracks at 25 fps, preserves cutscene root
motion, deforms skinned meshes, poses rigid children, evaluates visibility
toggles and renders the animated camera through the same z-buffered rasterizer
used for gameplay.

## Videos.isc

`Mp1/videos/Videos.isc` provides frame-indexed presentation events. The
current parser recognizes:

- `sample`
- `text`
- `stop_text`
- `fade_out`
- `set_envsound`

For `c111` the timeline starts `teletrasporto` at frame 0, shows Aldo's
line at frame 35, and removes the subtitle at frame 282.

## Speech enumeration

The retail data confirms that cutscene speech is resolved by convention,
per speaker, in the order that `text` entries occur in `Videos.isc`.
For Mp1 this mapping closes exactly over the shipped speech files:

- Aldo: `Aldo0000.mp3`
- Giovanni: `giovanni0000.mp3` through `giovanni0005.mp3`
- Operaio: `Operaio0000.mp3` through `Operaio0002.mp3`

This matches the six Giovanni timeline lines and three Operaio timeline
lines exactly. The room-runtime `csay Giovanni "Mo' vado!"` is separate
from that Videos.isc enumeration.

The engine now computes the per-speaker enumeration index while parsing the
timeline and routes available MP3 files through ScummVM's speech mixer.

## Performance

Animated scenes used to decode the same JGF textures on every rendered frame.
`SoftwareRenderer` now caches decoded textures for its lifetime. The
cutscene keeps one renderer alive for the full sequence and gameplay keeps a
persistent renderer so room loops and idle animation can redraw without
re-running LZHUF/JGF decoding every frame.

## Room loops

The Mp1 runtime issues several `playl` commands. In the first room the
relevant one is `playl r11_Terra Gira`. The room ANJ contains that exact
target/source pair with a 0..4200 animation range. `playl` state is now
kept by the engine and independent rigid room meshes are sampled continuously
at 25 fps alongside Giovanni's idle animation.

## Still incomplete

Master-color `e3d_Parse` fades are not yet faithfully reproduced, camera
roll is parsed but not applied, and `set_envsound` is parsed but not yet
connected to persistent environmental audio. Later room/puzzle interaction
also remains to be wired to the object scripts.


## Master Color fades

The JapoTek parser exposes `master_color`, `master_color_from`,
`master_color_to`, `master_color_factor` and
`master_color_blendsteps`. Direct DLL disassembly shows:

- `Master_Color R G B A` stores RGBA normalized by 1/255.
- `Master_Color_Fade_In N` interpolates from the current color to white.
- `Master_Color_Fade_Out N` interpolates from the current color to black.
- the blend factor advances by `deltaTicks / N`.
- the engine timer is configured at 70 Hz, so N is measured in 70-Hz ticks.

The runtime now mirrors this for the shipped Master Color commands and applies
the color multiplier to both gameplay and 3D cutscene framebuffers before UI
text/subtitles are drawn.
