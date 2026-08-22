# Loose game data

INI the game loads from disk rather than from the `.big` archives, one tree per game, each
laid out exactly as it must be deployed:

```
mod/GeneralsMD/Data/  ->  <Zero Hour install>/Data/
mod/Generals/Data/    ->  <Generals install>/Data/    (nothing here yet)
```

**`cmake --install` deploys these** along with the executable and the compiled shaders —
see the `install(DIRECTORY ...)` rules in `GeneralsMD/CMakeLists.txt` and
`Generals/CMakeLists.txt`. Nothing has to be copied by hand.

The split is not tidiness. A patch must name definitions the game actually declares and the
loader stops the load if one does not exist, so the trees cannot be shared blindly: the Zero
Hour patch below names Generals-Challenge objects from `NukeGeneral.ini`, which base
Generals has never had. A change that genuinely applies to both games belongs in both trees.

Loose files beat the archives — `W3DFileSystem.cpp:158` resolves names through
`TheFileSystem`, which searches LocalFile before the big files. There are two ways to use
that, and they are not interchangeable.

## Overrides replace; patches edit

A loose file at an archived path replaces its namesake **wholesale**. There is no merge:
changing one line of `CivilianUnit.ini` that way means shipping a full 20,548-line copy of
it, and the change is then invisible inside 20,547 lines of vendor content that also has to
be re-diffed by hand every time the stock data moves.

`Data/INI/Patch/` is the alternative. Files there load with `INI_LOAD_PATCH` (see
`Core/GameEngine/Include/Common/INI.h`) and write only the settings they name onto
definitions the game has already built; every field a patch does not mention keeps whatever
the shipped data gave it. The hazard-field change below is 502 lines as a patch and was
82,064 lines as overrides.

Prefer a patch. Reach for a wholesale override only when a patch genuinely cannot express
the edit — see the limits at the end.

## How patches load

`GameEngine.cpp` reads `Data\INI\Patch` recursively, immediately before `xferCRC.close()`:

- **Last**, after every subsystem has read its own INI, so a patch can edit anything the
  game defines and refer to any name it has learned.
- **Inside the CRC.** That CRC is stamped into replay headers and checked on playback
  (`Recorder.cpp`), so a patch that changes the game changes the CRC: a mismatched client or
  a stale replay is reported instead of quietly desyncing.
- **Before `postProcessLoadAll()`**, so patched templates go through the same name
  resolution as everything else.
- Files load in sorted name order — the order `INI::loadDirectory` already relies on to keep
  network games consistent between machines. Ordering between patch files is therefore
  filename-lexical, and worth remembering if two ever touch the same field.

The directory is optional; its absence is not an error.

## What a patch may say

Ordinary blocks name a thing and list the fields to change:

```
ParticleSystem RadiationFieldSmall
  BurstCount = 0.00 0.00  ; was 1.00 1.00
End
```

Modules go through `AddModule` / `RemoveModule` / `ReplaceModule` — the grammar `map.ini`
already uses:

```
Object RadiationFieldSmall
  AddModule
    ClientUpdate = HazardFieldDecalClientUpdate ModuleTag_09
      ...
    End
  End
End
```

This is required, not stylistic. A bare `ClientUpdate =` would leave the original module in
place and **append** a second one beside it, because the clear-on-redeclare path only clears
modules still marked as copied from default, which a fully parsed template's are not.

Two rules the loader enforces, both to turn a silent mistake into a loud one:

1. A block must name something that already exists. A misspelled name is an error, not a new
   and half-specified definition that fails much later and somewhere else.
2. Modules must use the Add/Remove/Replace grammar, as above.

Breaking either stops the game during load, the way any other bad INI does, and names the
file, the line and the offending token:

```
ASSERTION FAILURE: [LINE: 1 in 'Data\INI\Patch\zz_typo_test.ini'] Patch names Object
MiltiaTank, but no INI loaded so far declares it.
ASSERTION FAILURE: Error parsing block 'Object' in INI file 'Data\INI\Patch\zz_typo_test.ini'
```

## What patches cannot do

- **Add a new object.** They do not need to: a name nothing else declares is not a duplicate,
  so a plain loose file in `Data/INI/Object/` already works for that.
- **Edit one field inside a module.** The finest granularity is `ReplaceModule`, which
  restates the whole module. `ModuleData` has no virtual clone and `ModuleInfo::Nugget`
  holds a raw `const ModuleData*` that is pointer-copied into every override and every
  `ObjectReskin` child, so patching a module's fields in place would silently patch every
  sibling sharing that pointer.
- **Reach objects reskinned from the patched one.** `ObjectReskin` copies at parse time and
  patches load afterwards, so a patch applies to the object it names and not to its reskins.
- **Replace a `WeaponSet` or `ArmorSet`.** Those append rather than replace; use a patch to
  add a set, not to edit one.

## What is here

`Data/INI/Patch/HazardFields.ini` configures `HazardFieldDecalClientUpdate`
(`Core/GameEngine/**`), which replaced the 2003 hazard-field effect. Every hazard field —
anthrax, poison, radiation — used to be a scatter of ground-aligned sprites sharing one
128×128 texture, additively blended, so contamination could only ever *brighten* the ground
it fell on. They are now terrain-conforming projected decals that stain, glow, grow and
animate.

- **6 objects** gain decal layers: `PoisonFieldAnthraxBomb` stacks three, the
  `RadiationField*` set and `Nuke_RadiationFieldSmall` stack a scorch plus additive glows.
  Every timing and colour value lives there.
- **10 particle systems** are retuned: `BurstCount = 0.00 0.00` disables the old sprite
  emitters (a clean disable — the burst loop is `for (i < count)`, so nothing is created and
  no per-particle cost remains, and the systems stay defined for anything referencing them),
  while `AnthraxField*` is retuned rather than switched off because the drifting ground mist
  above the stain is still wanted.

`Data/INI/GameData.ini` also exists as a loose override on the development install, but it
predates this work and is unrelated (it pins `CloudShadowStrength`). It is deliberately not
committed here.

## Textures

Not here. They live in the **generalsassets** repo
(`git@gitlab.houdini.traber-web.ch:atraber/generalsassets.git`), branch
`hazard-field-textures`, commit **1341e546**, as
`texture_pipeline/hazard_fields/generate_hazard_textures.py` — the binaries are deliberately
untracked there and regenerated from that seeded script, which reproduces them byte for
byte. Its README documents the radiation set, which has no generator yet.

So a full deployment is `cmake --install` for the INI half, plus a copy for the textures:

```
generalsassets/modified/Art/Textures/ex*  ->  <game install>/Art/Textures/
```
