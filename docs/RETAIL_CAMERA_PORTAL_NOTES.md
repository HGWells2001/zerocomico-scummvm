# Retail camera and portal notes

These notes capture behavior measured from the user-supplied Italian retail data
and `Zero Comico.exe`. They document the evidence used by the reimplementation;
no game assets are included here.

## Verified split archive

The five supplied `zero.zip.001` ... `zero.zip.005` segments reconstructed a
valid ZIP with 2,959 entries. Their SHA-256 values match the project's previous
handoff manifest exactly:

```text
001  8384a7b42458eb062163bfce518b9da12a76e93b7e9aaeadba818cced3f94f30
002  8beba6367f88132da7e86e40c0413664d0717d584959ee167f5013ed38dc2256
003  47ac700f0be474ceb43c853225b0662d1e8e294a1a2c4ea0bf619561d153d5d1
004  0566996913c0adbd15e44de4afbae0852de511ef82386900605de8d377323ef1
005  7cc4be2a4bda2fc58f6f9d692b0e30774240a45e83223affbea0e53d13551d9d
```

The reconstructed archive SHA-256 is
`5ad88965ba2e1e5362e72e1f3f8d15df329f34fd82abaacdddbac3360808d703`.

## Automatic placed-camera regions

Across `Mp1..Mp5/gameplay/camera.gsc` the retail data contains 112 objects
whose `AUTOCAMERA` state is true:

```text
Mp1  24
Mp2  19
Mp3  19
Mp4  20
Mp5  30
```

Every measured automatic-camera object has an `in:` range that ultimately
issues `SetFocus` for its exported camera. The region is named through either
`polygon:` or `rangeshape:`; the matching geometry lives in
`camera.shp` as `ge_Polygon` vertices or a `ge_Shape ... Range` A/B pair.

This system is distinct from `MapCam`. The fixed-camera trigger chooses a
placed camera when the player enters a region. `MapCam` is the navigation
space used by camera modes that move.

## Camera mode numbers

The retail script callback registered for `SetCameraMode` maps the script
integer to the engine's zero-based mode passed to the camera runtime:

```text
script 1 -> internal 0 -> Placed camera
script 2 -> internal 1 -> Subjective camera
script 3 -> internal 2 -> Spot camera
```

The executable presents the same three labels in that order: Placed camera,
Subjective camera, Spot camera. Retail scripts examined here use
`setcameramode 1`; the other modes remain available to the engine/UI.

`LockCameraMode` and `UnLockCameraMode` write a separate global flag. The
retail `SetCameraMode` callback itself does not test that flag, so a scripted
mode change remains authoritative; the lock is therefore tracked separately
for the manual camera-control path.

## Spot camera parameters

The command signature embedded in the executable is:

```text
SpotHeight,MaxSpotDeltaY,SpotDistance,SpotMinDistance,SpotSmooth
```

The retail scripts use:

```text
SetSpotCameraParameters 0.85,0.3,3.5,0.25,30
```

The first four callback arguments are passed through the executable's world
conversion routine, which multiplies by `100 * GlobalScaling`.
The examined `Config.gsc` declares `GlobalScaling: 1`, producing:

```text
SpotHeight       85
MaxSpotDeltaY    30
SpotDistance    350
SpotMinDistance  25
SpotSmooth       30
```

The fifth value is stored directly. These values are retained by the runtime.

### Spot / MapCam clipping

The Spot update in the retail executable first builds the focus at character
Y + `SpotHeight`, then the desired camera point at `SpotDistance` in the
actor-relative camera direction. If a camera map is active, the focus point is
tested against that `MapCam`.

When the focus is legal, the executable constructs a **2D segment between the
desired camera point and the focus** and passes that segment through the BSP
camera-map clipping routine. The resulting camera point therefore stays on the
same boom/ray from the character and stops at the first MapCam boundary it
crosses. It is not an Euclidean nearest-point projection to an arbitrary edge.

If the focus itself is outside the active MapCam, the retail code does not
search for a nearest legal boundary. It collapses the boom to the same
actor-relative direction at **10 world units**. The runtime now mirrors both
behaviors before applying the existing distance-dependent vertical correction,
Spot smoothing, `SpotMinDistance` padding, and 150-unit look-ahead.

The supplied archive contains 42 MapCam files, 40 non-empty. Eighteen non-empty
camera maps contain holes and most outer outlines are concave, making
direction-preserving segment clipping materially different from nearest-edge
projection.

### Verified BSP tree semantics

Session 4 checked the actual BSP trees from all 40 non-empty retail MapCam
files. They contain **901 convex cells / leaf labels** in total. For every
single cell, a point formed from its boundary geometry is classified back to
the same cell with this rule:

```text
edge front side -> node leaf, when present; otherwise right child
edge back side  -> left child
null branch     -> outside the legal map
```

All 901 leaf-bearing nodes also refer to an edge whose `front` is that exact
leaf/cell and whose `back` is `-1`. This makes those edges the solid
MapCam boundary. Edges with both `front` and `back` set are internal portals
between convex cells and must not clip the Spot-camera boom.

The runtime therefore now validates the focus through the retail BSP tree and
clips the focus-to-camera segment against the first **solid `bsp_edge`** that
actually exits BSP space. Polygon outlines remain useful for general
walkability helpers, but Spot camera constraint no longer depends on them.

### Vertical correction and smoothing

The retail Spot routine computes the horizontal distance after MapCam clipping,
divides it directly by `SpotDistance`, and does **not** clamp that ratio. It
then applies:

```text
correction = (1 - distance / SpotDistance) * MaxSpotDeltaY
camera.y  += correction
focus.y   += correction / 2
```

The executable also confirms that `SpotSmooth` is a divisor, not a percentage
or a time-based coefficient:

```text
current += (desired - current) / SpotSmooth
```

This smoothing uses Spot-specific persistent position state. A room/scene load
sets a one-shot reset flag, causing the next Spot update to copy the desired
position directly; subsequent frames smooth from that stored Spot position.
Changing camera mode alone does not destroy that Spot history. The
reimplementation now mirrors that separation instead of sharing the smoothing
state with Subjective mode.

### SpotMinDistance and look-ahead order

The final Spot-camera operations were verified directly in `Zero Comico.exe`.
After MapCam clipping, vertical correction and smoothing, the engine computes
the horizontal unit vector:

```text
look = normalizeXZ(focus - smoothedCamera)
```

It then performs the two operations in this order:

```text
camera = smoothedCamera - look * SpotMinDistance
target = focus          + look * 150
```

The Y component is explicitly cleared before normalizing the look vector, so
both offsets are horizontal. The current runtime already matched this order and
orientation; no gameplay-code correction was required.

The executable also pushes the literal float `48.0` when selecting the active
camera through a generic renderer helper. Cross-references show the same value
for Placed and other camera selections, not only Spot, and the examined helper
does not consume that second argument on this path. It is therefore not treated
as a Spot FOV or another missing Spot parameter.

## SetPlace followed by SetMap

Retail scripts can request a room first and its map immediately afterwards,
before the destination room becomes current. For example, the measured opcode
ordering is conceptually:

```text
SetPlace <destination room>
SetMap   <destination room> <destination map>
```

Therefore `SetMap` cannot be validated only against the currently loaded
room. The reimplementation queues a destination-room map and applies it when
that room is committed, pairing the corresponding `MapCam` when declared.

## Mp5 portals

Mp5 declares ten directed logical room links but stores only five physical
`ge_Shape ... Portal` boundaries in `Shape.shp`. Reverse logical links reuse
the same physical boundary rather than duplicating A/B geometry.

The original executable copies the named Portal shape's A/B endpoints into the
runtime portal object. During character movement it retains the previous
character position, projects the test to two dimensions and performs a
segment/segment crossing test between:

```text
previous character position -> current character position
portal A                     -> portal B
```

A valid interior intersection resolves the portal destination and changes the
current room. This is not a distance-threshold test.

The retail BSP navigation graphs were checked against the five physical Mp5
portal boundaries: normal graph arcs cross each boundary, so the existing
Dijkstra movement path naturally supplies the movement segment needed by the
portal test.

`portals_off` and `portals_on` gate this traversal through a global runtime
state in the original executable; that state is mirrored by the engine.
