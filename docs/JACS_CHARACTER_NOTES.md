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

Transform key values are currently interpolated linearly. Tension,
continuity and bias are retained by the decoder but exact
Kochanek-Bartels/TCB interpolation is not yet evaluated. This is sufficient
to exercise the decoded hierarchy and skinning pipeline, but it is not yet a
claim of pixel-identical animation.

Rigid child meshes are posed with the same hierarchy delta so the head and
hat follow the animated body.

## Navigation

The Mp1 start marker `r11_Start` resolves to the retail world position and
is mapped to BSP navigation node 24 in the examined data. Mouse clicks are
projected to the Y=0 floor plane, snapped to the nearest graph node, routed
with Dijkstra, and traversed continuously. The current walk speed is a
runtime approximation informed by the root displacement of the retail
`Camm1` clip; exact movement timing remains to be tied to the original
character/controller rules.
