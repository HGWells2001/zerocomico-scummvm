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

## Persistent loop cutscenes

Mp2's water-panel puzzle uses a distinct retail cutscene-control path around
`c231`:

```text
loop_cut c231
...
run_cut  c231
...
stop_cut c231
```

Reverse engineering confirms that these are not aliases of synchronous
`play_cut`. `loop_cut` marks and starts a persistent cut object,
`run_cut` advances/renders that object, and `stop_cut` terminates it.
The shipped `c231` timeline in `Videos.isc` contains no text/sample events,
so its asynchronous requirement is limited to geometry, visibility and camera
animation.

The runtime now keeps a dedicated loop-cut SceneModel. `loop_cut` loads the
P3D/ANJ pair and records its animation range; `run_cut` samples the current
frame from elapsed time at the retail 25 fps cadence, poses the scene, samples
its animated camera/target and presents one frame without blocking ScriptVM;
`stop_cut` clears the persistent cut. `if_is_playingcut` now reports this
state instead of always returning false.

This deliberately does not turn ordinary `play_cut` into an asynchronous
player, and does not yet generalize timeline audio/text events for future
looping cuts. No such events are present in the retail `c231` use case.

## SetPlace followed by SetMap

Retail scripts can request a room first and its map immediately afterwards,
before the destination room becomes current. For example, the measured opcode
ordering is conceptually:

```text
SetPlace <destination room>
SetMap   <destination room> <destination map>
```

Therefore `SetMap` cannot be validated only against the currently loaded
room. The reimplementation queues a destination-room walk map and applies it
when that room is committed.

A complete census of `Mp1..Mp5/gameplay/room.isc` shows that every gameplay
room declares **exactly one `cameramap:`**, including rooms with several
selectable `map:` entries. For example, `Room2_1` declares four walk maps but
only `r21_MapCam00.bsp`; `Room2_4` declares four walk maps but only
`r24_MapCam.bsp`; and `Room2_5` declares five walk maps but only
`r25_MapCam.bsp`.

Accordingly, `SetMap` changes only the walk/navigation BSP. The room-declared
MapCam remains active until the room itself changes. Filename pairing such as
`r24_Map00.bsp -> r24_MapCam00.bsp` is not a retail rule.

This also explains the two zero-byte files shipped on disc,
`r21_MapCam.bsp` and `r24_MapCam00.bsp`: neither is referenced by a retail
room declaration. They are orphaned editor artifacts, not empty camera maps
that the runtime must accept as valid active MapCam data.

## SetDialogCameras

The retail corpus contains **59** `SetDialogCameras` calls across Mp1..Mp5.
The callback stores its two camera-object arguments directly in globals used
by the dialogue renderer. They are not fixed "player camera" and "NPC camera"
slots: the executable runs a left/right actor-geometry test and then chooses
the first or second camera. This matches the scripts, where Mp2 deliberately
passes several pairs as `*_dx *_sx` while other levels commonly use
`*_sx *_dx`; nine calls intentionally pass the same camera twice.

The reimplementation retains the two requested camera names and switches the
rendered dialogue view per spoken line. The executable side-test at
`0x420a2e` has now been reconstructed down to its vector operations. For the
current speaker it obtains that actor's forward vector and the forward vector
of the dialogue initiator, computes:

```text
delta = normalize(initiatorForward - speakerForward)
delta.y = 0
sideY = cross(speakerForward, delta).y
```

and selects the **second** SetDialogCameras argument when `sideY <= 0`,
otherwise the first. When the speaker is the initiator itself, `delta` is
zero and the retail comparison selects the second camera deterministically.

For CPU characters spawned through `SetCharPos_Entity`, the runtime recovers
forward from the marker mesh's retail transform (local -Z). It now also parses
CPU `SetCharPos_Vector` initialize statements and derives forward directly
from the marker's A->B vector in `Shape.shp`. Both spawn forms therefore feed
the same executable-equivalent side test. If a custom character has neither
kind of orientation marker, its line safely falls back to the first camera.
Identical camera pairs remain naturally unchanged. Dialogue choice lists begin
on the initiator/second camera and keep the most recent dialogue view.

## Dialogue do blocks and SetNoCameraReset

The retail dialogue files contain eight `do ... end` post-dialog blocks:
seven in Mp3 and one in Mp5. None of the eight coexists with a dialogue choice
list. Their payloads are small but gameplay-critical: they set variables such
as `prima_combat`, `Insulti`, `First_time` and `Dare_occhio`, and four
Mp3 blocks also issue `SetNoCameraReset MainPlayer 1`.

The dialogue parser now preserves those instruction ranges and `playDialogue`
executes them through the same ScriptVM used by puzzle/room logic. This restores
the variable mutations that hand control from dialogue into the corresponding
combat/minigame branches.

Direct executable analysis of `SetNoCameraReset` shows the value stored at
character offset `+0x5B10`. In the character-update path at `0x45AB29`, a
non-zero value skips the camera-reset branch that normally runs when the
character's dialogue/activity object becomes inactive.

All eight retail occurrences address `MainPlayer`: four dialogue `do` blocks
set the flag to 1 immediately before the Mp3 combat/minigame hand-off, and the
matching puzzle loops restore it to 0. The runtime now mirrors the observable
camera effect. A completed dialogue normally queues the room default camera in
Placed mode; while `_playerNoCameraReset` is active, it instead queues the
last camera selected by `SetDialogCameras`, preserving that shot across the
handoff. Dynamic Subjective/Spot modes are left to their own runtime update.

## Retail user-effect state

Mp2 uses `e3d_parse "set_usereffect_state ..."` 25 times. Direct
`japotek3d.dll` disassembly shows that the command resolves the named 3D
material/object, follows its primary texture pointer at offset `+0x9C`, and
writes the supplied integer directly to the texture state field at offset
`+0xCC`. This state write is valid for any textured material; by itself it
does not prove that the texture owns a registered user-effect callback.

The shipped calls touch `r21_Scale`, `r21_ScaleG`, `r24_Scale`,
`r24_ScaleG`, `r25_Scale`, `r25_ScaleG` and `r21_Rotte`, using only
states 0 and 1. `r21_Rotte` occurs exactly once and is only assigned state 0.

The texture constructor clears its flags and user-effect index. In the MAT
parser, the `UserEffect` token resolves a named effect, stores its index at
`+0xD0`, and sets texture flag `0x4000`. The render/update path dispatches
that registered callback only when flag `0x4000` is present and the state at
`+0xCC` is non-zero.

A full decode of the 204 retail text/script files finds no `SCALAF` reference
at all. In particular, `Mp2/backgrd/room2_1.mat` registers `usereffect film`
for `LAGO.FLC`, `ARCOB.FLC`, `SCALE.FLC` and `SCALEG.FLC`, but never
for `SCALAF.TGA`. The P3D material `r21_Rotte` references that static
`SCALAF.TGA` texture, so it has no MAT-registered user-effect callback.
Its sole retail `set_usereffect_state r21_Rotte 0` therefore performs the
state write but cannot invoke a visual callback. There is no missing Rotte
animation to reproduce.

The six Scale/ScaleG materials do use the registered film effect. Their
`SCALE.FLC` and `SCALEG.FLC` resources live in `Mp2/backgrd` inside
JFX1 wrappers and decode as Autodesk FLC streams: **64x64, 16 frames,
40 ms/frame**. The runtime decodes them through `WrappedFlicDecoder`, caches
their BGRA frames, and accumulates animation time only while the matching user
effect is non-zero. Disabling and re-enabling Scale/ScaleG therefore pauses
and resumes the animation instead of restarting it.

## Retail pickup class

The object-side `take_low`, `take_mid`, `take_high` and `take_none`
tokens are an explicit pickup classification, not a height test performed at
pickup time. In `Zero Comico.exe` the object parser maps them to the internal
values `0x55`, `0x56`, `0x57` and `0` respectively.

All 357 `Object` declarations in the supplied `Mp1..Mp5/gameplay/puzzle.isc`
files contain exactly one of these tokens: **297 none, 27 low, 33 mid and
0 high**. The shipped character AnimSets independently provide the animation name and
inventory event frame for each class. The runtime therefore records the object
classification directly in `PuzzleObject` and resolves the matching animation
and event timing from the active AnimSet.

The second argument of each playable-character pickup declaration is also
now identified. The executable stores the animation id for low/mid/high at
indices `0x55`, `0x56` and `0x57`, and stores the numeric argument in the
parallel table addressed by the same index. `wait_take` increments a pickup
counter and calls the normal `addobjininv` path exactly when that counter
equals the stored value. It is therefore an **inventory event frame/tick**, not
an approach distance. The shipped playable AnimSets all use `10` for low and
mid pickup; high remains at the retail default `pickup 10` and is unused by
the 357 shipped puzzle objects.

Direct ANJ decoding confirms that `Get` and `Getdown` span frames **0..80**
for Giovanni, Aldo and Giacomo. Their character `step_events` independently
schedule sample 2 (`Take.mp3`) at frame 15. The runtime now plays the selected
pickup clip at its established 25 fps cadence, transfers the inventory object
at event frame 10, and keeps `wait_take` blocked until the clip reaches its
final frame. The character parser now also preserves the data-driven `Sample` table and
`step_events` rows. The syntax supports more than one event on a row, as seen
in retail CPU animations such as `Stay { 205#0 240#1 }`. During a pickup the
runtime matches the active `Get`/`GetDown` event against that table, resolves
sample id 2 through the current playable character's `Sample: 2 Take.mp3`
declaration, and plays it at frame 15. No pickup sound filename is hard-coded.

## Retail puzzle-object state

The retail `addobjininv` handler does more than insert a name in the
inventory list. Direct executable disassembly shows that it applies the literal
state string `pickable FALSE examinable FALSE assigned TRUE` to the resolved
inventory object before updating the inventory controller. Inventory objects
such as `obj_lepego`, `obj_batteria` and `obj_occhio` are ordinary
`puzzle.isc` Object declarations, so these flags are part of the same puzzle
state rather than separate UI metadata.

The runtime now mirrors those three side effects for both explicit
`addobjininv` and the inventory-transfer event inside `wait_take`.
`setobj` also updates every boolean Object field already decoded by
`PuzzleScript`, including `collision` and `operated`; the retail corpus
uses those two in addition to the previously handled `examinable` and
`enabled` forms.

## Retail depth cue

The retail script corpus uses `dcue_all` exactly once, during Mp5 startup:

```text
e3d_Parse "Master_Color 0 0 0 255"
e3d_Parse "Master_Color_Fade_In 200"
dcue_all 1, 8.5, 14.5
```

The executable callback applies the first argument as the depth-cue state and
converts the two ranges through the normal metre-to-world conversion. With
`GlobalScaling: 1`, the shipped range is therefore **850..1450 world units**.

The software renderer now carries that state on `RenderCamera` and performs
linear per-pixel depth attenuation across the verified range. The only retail
use selects black immediately beforehand, so RGB fades toward black while
alpha and z-buffer behavior remain unchanged.

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
