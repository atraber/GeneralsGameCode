# Generals Game Code - Renderer Experiments

> [!WARNING]
> **This is an experimental fork.** It is not a stable or supported build, branches are rewritten, and things break. For a
> production-quality community build, use upstream.

This fork is built entirely on top of [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)
(TSH), which does the hard work of making *Command & Conquer: Generals* and *Zero Hour* build and run on modern toolchains.
Everything below is layered on that foundation and all credit for the base goes to TSH.

**In short:** this fork makes the 2003 game look like a modern one, using the original, unmodified assets. Units and
buildings cast real shadows that follow the terrain, and so do smoke and helicopter rotors. Metal catches the light and
reflects its surroundings. Explosions and fire glow, headlights and street lamps light up the ground around them, and fog
drifts through the light. The sun moves across the sky, so a match runs from daylight into dusk and night. Under the hood
the renderer was moved from Direct3D 8 onto Direct3D 11, which is what makes all of this possible.

---

## What changes in game

### Shadows
- **Everything casts a proper shadow.** Units, buildings, trees and hills cast shadows that fall across the terrain and
  onto each other, instead of the flat blobs and hard-edged shadow volumes of the original.
- **Soft edges.** Shadows soften the way real ones do instead of looking cut out.
- **Smoke, dust and rotors cast too.** A smoke trail darkens the ground under it, and a helicopter's rotor throws a
  flickering disc.
- **Cloud shadows** drift across the map, over the ground and the buildings standing on it.

### Light
- **Materials look like what they are.** Metal is shiny and reflects its surroundings, while cloth and concrete stay matte.
- **Local lights light the world.** Vehicle headlights, building lights and street lamps light up the ground and nearby
  units instead of being painted-on glow.
- **Day and night.** The sun and moon cross the sky over the course of a match. Colours shift from noon to sunset to night,
  shadows lengthen, and lights switch on as it gets dark.
- **Bright things look bright.** Explosions, muzzle flashes and sunlit highlights glow and bleed light into their
  surroundings, without washing the rest of the picture out.
- **Light hangs in the air.** Fog and haze catch sunlight and headlights, so light beams become visible.

### World
- **Terrain looks less like a tiled floor.** The repeating tile pattern is broken up and the ground gets finer surface
  detail.
- **Water has depth.** It shows the sky, gets darker further from the shore, and breaks against the coastline.
- **Sharper textures** at a distance, and smoother edges with antialiasing.
- **Particles blend into the scene** instead of cutting a hard line where they meet the ground or a building.

### For developers
- **Unattended replays.** A replay can play from the command line with a scripted camera, save screenshots and then quit
  (`-replay <file> -cameraScript <file> -quitAfterReplay -dumpFrames`), so two builds can be compared picture by picture.
- **Debug views and timing.** In debug builds **F10** cycles through views that show what the renderer is doing (the
  shadows, depth, glow, lights), and **Ctrl+Shift+F10** shows where each frame's time goes.
- **Small INI mods.** A file in `Data\INI\Patch` can change a few individual settings without replacing a whole game file.

## Options

New `Options.ini` settings (all on by default unless noted):

| Setting | What it does |
|---|---|
| `UseShadowMapping` | Real shadows (the "3D Shadows" checkbox) |
| `UseParticleShadows` | Smoke and dust cast shadows. Turn this off first on a slow machine |
| `UseHDR`, `UseBloom` | Bright highlights and glow |
| `UseVolumetricFog` | Fog and visible light beams |
| `TerrainTileVariation`, `TerrainDetail` | Break up the tile pattern and add ground detail |
| `AnisotropyLevel`, `AntiAliasing` | Sharper distant textures (2x-16x) and smoother edges |
| `DayNightCycleDuration` | Minutes for a full day (default 20, 0 turns the cycle off) |

## Building

This fork builds the same way as upstream TSH. See the TSH repository for instructions.
