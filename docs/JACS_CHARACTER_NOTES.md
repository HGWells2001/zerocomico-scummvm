# Giovanni / JACS character notes

These notes record measurements made from the retail Zero Comico data while
building the ScummVM runtime. No game assets are included.

## Character assets

The playable character is declared by `Mp1/gameplay/char.isc` as
`Giovanni`, with animation body `Gio_Giovanni`. The retail asset set is:

- `Mpx/bodies/Giovanni/Giovanni.p3d`
- `Mpx/bodies/Giovanni/Giovanni.anj`
- `Mpx/bodies/Giovanni/Giovanni.seq`

`Giovanni.seq` maps the walk state `cammina` to start, loop and stop
clips. The continuous walk loop is `Camm1` / `Camm2`; the run state
`corsa` similarly uses `Corsa1` / `Corsa2`.

## Skin records

The P3D contains three skinned-parent meshes:

- `gio_gioc`: 270 vertices, 471 faces
- `gio_giob`: 120 vertices, 236 faces
- `gio_gioa`: 120 vertices, 236 faces

Their flesh records use flag `0x20000`. Each flesh vertex carries a parent
vertex index and a weight. Across these three parents every parent vertex is
covered, vertices have up to four influences, and the influence sums are
approximately 1.0.

The bind pose can therefore be reconstructed without guessing missing
vertices: accumulate every flesh position into its referenced parent vertex
using the stored weight, then normalize by the accumulated weight.

## Hierarchy and animation

The ANJ hierarchy rooted at `gio_giovanni` contains the bone/object parent
chain used by the flesh record names. For example `gio_gioa01`,
`gio_giob01`, `gio_gioc12` and the rigid `gio_giotesta` /
`gio_CAPPELLO` meshes are all named hierarchy targets.

F007 clips contain transform tracks with translation (3 components), scale
(3) and rotation (4). The rotation payload is axis-angle, not a quaternion:
the first three floats are the axis and the fourth is the angle in radians.
The Stay root is effectively identity rotation and carries the actor-local
height/offset.

The runtime currently builds a bone delta as:

`current_global * inverse(bind_global)`

where `Stay` frame 0 is the bind animation. Root motion is kept outside the
skeleton because gameplay movement owns the character's X/Z placement on the
BSP graph.

## Current runtime approximation

Transform keys are evaluated with Kochanek-Bartels interpolation using the
stored tension, continuity and bias values. Endpoint tangents fall back to the
adjacent segment slope. Axis-angle rotation components are sampled through the
same TCB path before the axis is normalized by the pose matrix builder.

Rigid child meshes are posed with the same hierarchy delta so the head and
hat follow the animated body.

## Pickup animation metadata

The retail character scripts do not leave pickup animation choice implicit.
Every shipped playable AnimSet defines:

```text
take_low: getdown 10
take_Mid: get 10
```

and the Giovanni, Aldo and Giacomo ANJ files all contain the corresponding
`GetDown` and `Get` clips. Their character scripts also declare animation
events `Get { 15#2 }` and `GetDown { 15#2 }`, showing that frame 15 carries
the pickup cue.

The original executable has three pickup slots. Its defaults are `pickdw`
(low), `pickmd` (mid) and `pickup` (high), each with parameter 10; the
retail scripts override low and mid but never declare `take_high`.

`CharacterScript` now preserves all three animation names and parameters per
AnimSet instead of discarding them. The runtime also keeps the **currently
active player AnimSet**: it starts from the character script's
`initialAnimSet` and changes only after a successful `SetAnimSet` body/ANJ/SEQ
swap. This matters because pickup metadata belongs to an AnimSet rather than to
the character globally.

The object-side `take_none/take_low/take_mid/take_high` token is now decoded
as the retail pickup classifier. The runtime selects the active AnimSet's
matching animation, transfers the inventory object at its declared event tick,
and dispatches the parsed `step_events` sample cue from the same character
metadata.

## CPU character instances

The retail `char.isc` files declare 53 CPU-player characters; **45** of them
have both a body AnimSet and an explicit `SetCharPos_Entity` or
`SetCharPos_Vector` spawn marker. Those 45 are now instantiated lazily when
their room becomes active rather than being merged by mesh name into the room
SceneModel.

CPU bodies render as independent actors in the room's shared z-buffer. This is
required for duplicate/model-related characters such as `Granchio` and
`Granchio01`, and it preserves each body's own texture directory. The loader
tries both chapter-local `MpX/bodies` assets and the shared `Mpx/bodies`
tree used by Aldo, Giovanni and Giacomo.

Each character's retail `initialize:` instruction range is retained and run
when that CPU instance is created. The currently shipped initializer vocabulary
(`SetCharPos_Entity`, `SetCharPos_Vector`, `SetWaitState`,
`BreakLifeToChar`, `play` and `playl`) therefore feeds the ordinary
runtime paths instead of being approximated separately. A `BreakLifeToChar`
issued before a later-room body has been loaded is remembered and applied when
that character is instantiated; a subsequent `GiveLifeToChar` clears it.

The autonomous per-frame `ControlCode:` scheduler is still separate work.

## Navigation

The Mp1 start marker `r11_Start` resolves to the retail world position and
is mapped to BSP navigation node 24 in the examined data. Mouse clicks are
projected to the Y=0 floor plane, snapped to the nearest graph node, routed
with Dijkstra, and traversed continuously. The current walk speed is derived at runtime from horizontal root displacement
across the decoded `1>1` walk clips and their frame range, with a conservative
fallback only when a clip carries no usable displacement. The animation state
machine now follows the retail `0>1`, `1>1` and `1>0` transition groups from
`Giovanni.seq`.
