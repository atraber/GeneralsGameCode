# Tread animation: what the data still needs

Companion to the engine fix `23fd0518c` (`unit_vs` / `unit_prelit_vs` padded a 2-D mesh UV
as `(u,v,0,1)` instead of D3D's `(u,v,1,0)`, so every 2-D mapper's translation was
multiplied by zero). With that in, **every unit whose art and INI are wired up now
scrolls**. What follows is the set that still does not, and why — all of it data, none of
it code.

## The chain a unit has to satisfy

`W3DTankDraw::updateTreadObjects` (`Core/GameEngineDevice/.../Draw/W3DTankDraw.cpp`) walks
the model once and keeps a sub-object only if **all** of this holds:

1. The object's INI draw module sets `TreadAnimationRate` (non-zero). Parsed by
   `INI::parseVelocityReal`, so the value is **per second** and is divided by 30 to reach
   a per-logic-frame delta: `TreadAnimationRate = 2.0` steps U by `0.0667` a frame.
2. The sub-object is a **direct** child of the container (`Get_Num_Sub_Objects()` is not
   recursive) and is a mesh.
3. Its name, after the `.`, begins with `TREADS` — case-insensitive, exactly 6 chars.
4. It carries a vertex material whose mapper is `MAPPER_ID_LINEAR_OFFSET`.
5. The **7th** character picks the side: `L`/`l` → left, `R`/`r` → right. Anything else
   leaves the tread as `TREAD_MIDDLE`, which `updateTreadPositions` cannot handle — it
   `DEBUG_CRASH`es and holds the offset at 0. So `TREADSL01`, not `TREADS01`.

Two limits worth knowing before authoring: `MAX_TREADS_PER_TANK = 4`, and the counter
increments **per vertex material**, not per mesh — a tread mesh carrying two
`LINEAR_OFFSET` materials burns two of the four slots.

Straight-line driving only scrolls above `TreadDriveSpeedFraction` of max speed (deliberate
— slow scrolling read as sliding). Pivot turns drive left and right in opposite directions,
which is the only path that reaches the sidedness check.

## Census

All 9,302 vanilla `.W3D` files parsed for tread sub-meshes and their mapper, cross-
referenced against the shipped `INIZH.big` object definitions.

| | count |
|---|---|
| objects using a tread-capable draw module | 87 |
| fully wired, animate correctly | 70 |
| not wired | 17 |

**The 17 are exactly the 17 objects with no `TreadAnimationRate`** — the two conditions
coincide, which is not a coincidence: nobody set a rate on a hull with no tread mesh to
scroll. So every unit below needs *both* halves, art and INI, except `MilitiaTank`, which is
the one object that has the geometry already and only wants the rate.

They split into two very different groups.

### Not actually treaded — correct as-is, no work wanted

`W3DTankDraw` is used as a generic chassis module, not a promise of tracks. These have no
tread geometry because they have no tracks:

`AmericaVehicleGuardianDrone` (`AVGuardDr`), `AmericaVehicleRepairDrone` (`AVRepairDr`),
and the four battleship objects (`AVBattleSh`, `AVBattShip`) — drones hover, battleships
float.

### Genuinely missing tread geometry — 3 units, 8 model files, 11 objects

Tracks are painted into the hull texture, so there is nothing to scroll. Ordered by how
visible the fix would be.

| model file | unit(s) | condition state | notes |
|---|---|---|---|
| `NVBtMstr.W3D` | China Battlemaster, Tank General Battlemaster | DEFAULT | the big one — most-built tank in the game |
| `NVBtMstr_D.W3D` | as above + `CINE_ChinaTankBattleMaster` | REALLYDAMAGED, RUBBLE | |
| `NVBtMstrNG.W3D` | Nuke General Battlemaster | DEFAULT | separate model, same hull |
| `NVBtMstrNG_D.W3D` | Nuke General Battlemaster | REALLYDAMAGED, RUBBLE | |
| `NVTCrawler.W3D` | China Troop Crawler | DEFAULT | |
| `NVTCrawler_D.W3D` | China Troop Crawler | REALLYDAMAGED, RUBBLE | |
| `avtomahawk.W3D` | Tomahawk ×5 (base, AirF, Lazr, SupW, Boss) | DEFAULT | one model serves all five |
| `avtomahawk_d.W3D` | Tomahawk ×5 | REALLYDAMAGED | |

The RUBBLE states are stationary wrecks, so those four `_D` models only matter for the
REALLYDAMAGED-but-still-driving case. If you want the cheapest visible win, `NVBtMstr.W3D`
alone covers the majority of tanks on screen in a typical game.

**Each of these also needs `TreadAnimationRate` added**, once per *object*, not per model —
and several objects share one model. The 11 objects, verified against the shipped INI:

| model | objects that need the rate |
|---|---|
| `NVBtMstr` | `ChinaTankBattleMaster` (ChinaVehicle.ini), `Tank_ChinaTankBattleMaster` (TankGeneral.ini) |
| `NVBtMstrNG` | `Nuke_ChinaTankBattleMaster` (NukeGeneral.ini) |
| `NVTCrawler` | `ChinaVehicleTroopCrawlerEmpty` (ChinaMiscUnit.ini), `CINE_ChinaVehicleTroopCrawlerEmpty` (ChinaCINEUnit.ini) |
| `avtomahawk` | `AmericaVehicleTomahawk`, `AirF_`, `Lazr_`, `SupW_` (per-general INI), `Boss_VehicleTomahawk` |

`CINE_ChinaTankBattleMaster` is the exception and the proof: it already sets
`TreadAnimationRate = 2.0` **and** draws `CINE_BttlMstr`, so it is the one Battlemaster whose
treads scroll in the shipped game today. Whatever the donor model does is therefore known to
work in-engine on this exact chassis.

**There is a donor for the Battlemaster.** `CINE_BttlMstr.W3D` — the cinematic
high-detail model — already carries `TREADSL01`, `TREADSL02`, `TREADSR01`, `TREADSR02`,
all `LINEAR_OFFSET` at `UPerSec=0.1`, correctly sided. Its tread band, material and UV
layout can be retargeted onto the in-game hull rather than authored from scratch.

### INI gap only — 1 unit, 1 line

`MilitiaTank` (`CivilianUnit.ini`). `CVTank` and `CVTank_D` both already have
`TREADSL01`/`TREADSR01` on a `LINEAR_OFFSET` material at `UPerSec=0.1`. The draw module
just never sets a rate:

```
  Draw = W3DTankDraw ModuleTag_01
    ...
    TrackMarks           = EXTnkTrack.tga
    OkToChangeModelColor = Yes
    TreadAnimationRate   = 2.0   ; <- the whole fix
  End
```

That one line is enough. `TreadPivotSpeedFraction` and `TreadDriveSpeedFraction` default to
`0.6` and `0.3` in the module data constructor, which is exactly what the 37 objects that
spell them out write anyway; the GLA Scorpion — the closest comparable light tank — sets
only the rate, also at `2.0`. Across the shipped data the rate is `2.0` (52 objects) or
`4.0` (18, the heavier vehicles).

## Deployment: no `.big` repack needed for any of it

`GameFileClass::Set_Name` (`W3DFileSystem.cpp:158`) resolves W3D and texture names through
`TheFileSystem`, which searches **LocalFile before the archives**. A loose file therefore
beats its packed namesake:

```
<game install>/Art/W3D/NVBtMstr.W3D        overrides the copy in W3D.big
<game install>/Data/INI/Object/…​.ini        overrides the copy in INIZH.big
```

W3D also gets a localisation slot checked *first* — `Data/<lang>/Art/W3D/` — if you want
overrides that do not sit in the main art folder.

A loose override at an archived path replaces its namesake wholesale, with no merge, so the
one-line `TreadAnimationRate` fix used to mean shipping a full 20,548-line copy of
`CivilianUnit.ini`. It no longer does: `Data/INI/Patch/` takes files that write only the
settings they name onto definitions the game has already built.

`TreadAnimationRate` is draw-module data rather than an object field, and the finest patch
granularity is a whole module, so the MilitiaTank fix is a `ReplaceModule` restating its
draw block with the one line added -- about 24 lines instead of 20,548:

```
Object MilitiaTank
  ReplaceModule ModuleTag_01
    Draw = W3DTankDraw ModuleTag_01_Treads
      DefaultConditionState
        Model               = CVTank
        Turret              = Turret01
        WeaponFireFXBone    = PRIMARY MuzzleFX
        WeaponRecoilBone    = PRIMARY Barrel
        WeaponMuzzleFlash   = PRIMARY MuzzleFX
        WeaponLaunchBone    = PRIMARY MuzzleFX
      End
      ConditionState       = REALLYDAMAGED
        Model              = CVTank_D
      End
      ConditionState       = RUBBLE
        Model              = CVTank_D
      End
      TrackMarks           = EXTnkTrack.tga
      OkToChangeModelColor = Yes
      TreadAnimationRate   = 2.0
    End
  End
End
```

`ReplaceModule` requires a new unique tag for the replacement, which is why the tag changes.
See `mod/README.md` for the mechanism and its limits. W3D files are self-contained, so the
model overrides never carried that cost either way.

Authoring path for the geometry: the `OpenSAGE.BlenderPlugin` checkout at the repo root
imports and exports `.W3D`. The work per model is to split the tread band off the hull
into its own mesh, name it `TREADS` + `L`/`R` + index, give it a vertex material with a
`LinearOffset` mapper, and lay the UVs out so **U runs along the track** (the offset is
applied to U only — `updateTreadPositions` sets `Vector2(offset_u, 0)`).

## Correction to an earlier finding

I previously flagged `NVGattTank_D1` and `UVScorpion_D` as tripping the `TREAD_MIDDLE`
`DEBUG_CRASH` through unsided `TREADS01`/`TREADS02` names. Both are unreachable from
shipped data and need no fix:

- `NVGattTank_D1` is referenced only by `DeadChinaGattlingTankHulk` (`Hulk.ini:927`), which
  draws with `W3DModelDraw` — the tread code never runs on it.
- `UVScorpion_D` is not referenced by any object. The GLA Scorpion uses `UVLiteTank`, whose
  treads are correctly sided.

`AVCrusader*` and `UVMarauder_D1` likewise have unsided `TREADS01-04` but on plain-UV
materials, so they fail the mapper test at step 4 and are never registered. The in-game
Crusader is `AVLeopard`, which is fine.
