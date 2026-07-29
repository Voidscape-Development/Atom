# Atom

Atom is an OBS Studio plugin for **atoms** — particles — and what you can do with them on stream.
It adds an **Atom Emitter** source plus an **Atom Designer** window for building the look and the
motion of an effect, with a live preview and a preset browser.

> Status: early. The simulation, renderer, property page, designer window and preset library are in
> place; see [Roadmap](#roadmap) for what is not.

## What it does

### Atom Emitter source

| | |
|---|---|
| **Source size** | Width and height of the emitter surface. |
| **Emitting location** | Single spot, bounding box (even or clustered), source edges, source center, circle/ring/arc, line, or polygon/star. Every shape has its own options. |
| **Atom Design** | Opened with the *Open Atom Designer…* button — see below. |
| **Atom Physics** | Gravity and gravity direction, initial speed, emit angle and spread, drag, turbulence, lifetime and lifetime falloff, spin, destination endpoint, and per-atom offsets. |

### Atom Physics

* **Gravity** — positive falls, negative rises like embers or smoke.
* **Gravity direction** — any angle, so "gravity" can pull sideways.
* **Lifetime + falloff** — how long an atom lives, and the curve its age follows (linear, ease in,
  ease out, smooth, exponential, or a curve you draw).
* **Destination endpoint** — nothing, a fixed point, or another source in the scene. Atoms can be
  attracted, made to arrive exactly as their life runs out, or put into orbit; they can be removed
  on arrival, and each atom can aim at a slightly scattered target.
* **Offset** — position, angle, speed, size, lifetime, rotation, hue, brightness, opacity and spawn
  phase variance. Zero everywhere means every atom emits identically.
* **Extra forces** — optional wind, vortex, wander and bounds (bounce / wrap / remove) layered on
  top, each with their own parameters.

### Atom Design

The designer is laid out like OBS' *Add Source* dialog: categories on the left, a preset grid or a
property page on the right, and a live preview underneath.

* **Color** — a single color, a lifetime gradient with as many stops as you like (drag to move,
  double-click to add or recolor, right-click to remove), or a random color picked per atom.
* **Bloom** — amount, radius and softness, from a tight light source to a diffuse puff of smoke.
* **Size** — base size, minimum size, and an editable size-over-life curve.
* **Trail** — streak, ribbon, comet or sparkle, with length, width, fade and segment count.
* **Fade path** — no fade, linear, ease out, hold, **flare** (firework-style ignite → travel →
  burnout), blink, pulse, or a curve you draw. Plus fade-in and flicker.
* **Atom shape** — soft circle, hard circle, ring, spark, star, square, procedural smoke, or your
  own image.
* **Layers** — a design can hold several kinds of atom, each with its own spawn weight, so one
  emitter can produce e.g. flames *and* sparks.

### Presets

Twelve built-in presets (Embers, Fire, Sparks, Firework Flare, Smoke Puff, Fog Drift, Magic Dust,
Rune Circle, Confetti, Bubbles, Snow, Rain) with thumbnails that are simulated live rather than
stored as images. Anything you build can be saved as your own preset; user presets are JSON files
under the plugin's config directory and can be shared by copying the file.

## Architecture

The plugin is deliberately split so that new capabilities are additive:

```
src/atom-core/   simulation, module registries, config model   (no OBS, no Qt)
src/obs/         source, properties, serialization, renderer   (OBS only)
src/ui/          designer window, editors, preview             (Qt only)
```

Two ideas do most of the work:

**Module registries.** Emitter shapes, forces, atom shapes, trail styles, fade paths, lifetime
falloffs and endpoint providers are all entries in a `Registry<Interface>`, each carrying a
`ModuleInfo` with a parameter schema. Registering one makes it appear in the OBS property page, in
the designer, and in saved settings — no UI or serialization code to touch. See
`registerBuiltinModules()` in `src/atom-core/atom-modules.cpp` for the pattern.

**Field tables.** Config structs describe themselves once, in `src/atom-core/atom-config.cpp`, as a
list of `FieldBinding`s. The property page, the designer widgets and the settings serializer are
all generated from those tables, so a new option is one binding plus one locale string.

Because `atom-core` knows nothing about OBS, the designer's preview runs the *same* simulation and
the same `DesignEvaluator` as the rendered source — what you see in the preview is what goes out.

## Building

Standard [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) build. Qt and the
frontend API are required (both are on by default) because of the designer window.

| Platform | Tooling |
|---|---|
| Windows | Visual Studio 17 2022, CMake 3.30.5 |
| macOS | Xcode 16, CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential` |

```sh
cmake --preset ubuntu-x86_64      # or windows-x64 / macos
cmake --build --preset ubuntu-x86_64
```

The build scripts in `.github/scripts` and the workflows in `.github/workflows` come from the
template and build the plugin on all three platforms.

## Roadmap

Known gaps, roughly in priority order:

* Bloom is a per-atom additive glow quad rather than a full-screen post-process, and trails are
  drawn as stretched sprite quads rather than a proper ribbon mesh.
* The endpoint "source in a scene" mode resolves the first scene that contains both the emitter and
  the target; an emitter used in several scenes at once picks the first match.
* Sub-frame emission is not interpolated, so very high rates at low frame rates emit in visible
  clumps.
* No audio reactivity, no collision against other sources, no sprite-sheet animation.
* Only `en-US` translations so far.

## License

GPL-2.0-or-later, matching OBS Studio. See [LICENSE](LICENSE).
