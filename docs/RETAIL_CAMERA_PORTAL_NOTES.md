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

The fifth value is stored directly. These values are now retained by the
runtime. Exact Subjective/Spot motion and `MapCam` constraint behavior still
need to be reproduced before those modes are considered complete.

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
