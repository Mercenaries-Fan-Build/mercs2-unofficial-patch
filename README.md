# Mercenaries 2 Shipment template

A starting point for a Mercenaries 2 mod — and a worked explanation of how modding this game
actually functions.

The Shipment at the root builds and loads as-is. It does the two things every mod does: it
**overwrites** existing content (Mattias's skin) and **adds** new content (an outfit). Change one
field at a time and re-run `qm lint .`.

[`examples/`](examples) has a runnable Shipment for each capability, including ones that fail on
purpose.

## Why any of this exists

**Mercenaries 2 does not report asset errors. It hangs on a loading screen.**

Declare one page too few for a texture and the engine overruns its heap. Point a LOD row at a block
that is not in your WAD and the streamer sizes a buffer from a garbage index and asks the OS for
roughly 549 GB. Get a wardrobe count wrong and your outfit is in the WAD, in the table, and
unreachable.

None of those produce a message, a log line, or a crash dump. `qm lint` knows about them, so you
find out in a second rather than after an hour of bisecting a WAD.

## Get started

Grab `qm` from the [tools release page][releases] — there is nothing to compile.

```sh
qm lint .                        # check it; no game install needed
qm build . --out build           # produce the overlay WAD
qm rules                         # everything that gets checked
```

`qm build` needs the game and finds it automatically; pass `--game <dir>` if yours is somewhere
unusual. Install the WAD it produces with [Modkit][modkit].

Modkit installs and updates `qm` for you, and builds through it. To grab it by hand instead, take
the asset for your platform from the release page — `qm-windows-x86_64.exe`, `qm-linux-x86_64`,
`qm-macos-arm64`, `qm-linux-arm64`, … — and put it on your `PATH`.

### Building anything with Lua

`qm build` needs `--corpus`, the decompiled game scripts your append is added to. Download
`mercs2-workshop-data.zip` from the same release page, unzip it, and point at `workshop_data/lua`:

```sh
qm build . --out build --corpus path/to/workshop_data/lua
```

The root Shipment here needs it, because `add_outfit` writes a wardrobe entry.

## How a mod reaches the game

Your Shipment builds into **one overlay WAD**. The game mounts it after its own, and the last mount
wins — so the base files are never modified and uninstalling is deleting a file.

That is the simple half. The rest of the format exists because the engine does **not** resolve
everything that way:

| subsystem | who wins |
|---|---|
| WAD stack | last mounted |
| runtime chunk registry | **first** writer |
| string databases | last registered, capped at 8 |
| ASI plugins | nothing arbitrates at all |

Four subsystems, four different answers, running simultaneously. "Just set your load order" is not a
strategy that can work against that, which is why a Shipment declares *what it touches* and lets the
Quartermaster resolve conflicts before anything is built.

## What you can contribute

| kind | does | example |
|---|---|---|
| `replace_texture` | overwrite an existing texture | [01](examples/01-overwrite-a-texture) |
| `add_model` | add new geometry | [02](examples/02-add-a-model) |
| `add_outfit` | add a wearable outfit (geometry + wardrobe entry) | [03](examples/03-add-an-outfit) |
| `patch_lua` | append to a game script | [04](examples/04-bundle-lua) |
| `native_hook` | ship an ASI plugin into the game folder | [06](examples/06-ship-a-plugin) |
| `raw` | opaque bytes plus a declared blast radius | [07](examples/07-raw-bytes) |
| `edit_state_machine` | rewrite a destruction state machine | [08](examples/08-not-supported-yet) — **refused** |

Six of the seven build. `edit_state_machine` lints but refuses to build, and says exactly why: the
chunk family can be read but not written, and `states:` has no schema yet. It is left refusing rather
than half-implemented, because a dropped or guessed contribution produces a WAD that looks correct
and does nothing — which the game will never tell you about. Ship a hand-built block through `raw`
in the meantime.

`raw` and `native_hook` need `qm` v0.10.2 or later.

## The part that bites: Lua

Scripts do not load individually. All 114 live in a single block, so editing one means re-emitting
all of them — and if two mods each shipped their own copy of that block, the last one installed
would erase the other's Lua with no error anywhere.

So you ship an **append**, not a script, and `qm link` composes every installed Shipment's appends
onto one base and compiles once. [`examples/05-two-mods-coexisting`](examples/05-two-mods-coexisting)
demonstrates it.

Only scripts whose composition has been reverse-engineered can merge. For anything else the
Quartermaster **fails closed**: your Shipment still builds, but it is marked exclusive and refuses to
co-install with another mod touching that script. Refusing to install is visible and recoverable;
two mods silently erasing each other is not.

## Reading the output

```
[M0110] error: contributions[0]: (replace_texture) field `image`: src/skin.png does not exist
  — see https://…/manifest_format.md#folder-layout
```

Every finding has a code and a link explaining the trap.

| severity | effect |
|---|---|
| `info` / `warning` | printed; the build continues |
| `error` | the build fails |
| `HANG` | the build fails — this class freezes the game with no message |

Builds are gated on the **exit code**, never on a printed count, so a script that discards output
still cannot ship a broken Shipment. Exit `1` means findings; exit `2` means `qm` could not run at
all (no manifest, no game install) — CI needs to tell those apart.

`qm rules` also lists rules that are known but **not yet implemented**. Those are shown deliberately:
a linter that silently omits its most dangerous checks reads as a clean bill of health.

## Layout

```
manifest.yaml     what this Shipment contributes
src/              your files — textures, models, Lua
examples/         runnable examples; delete if you don't want them
build/            qm output; gitignored, never commit it
```

Every path in the manifest resolves under `src/`. A path escaping the root — via `..` or a symlink —
is refused, because a Shipment has to mean the same thing on someone else's machine as on yours.

The full field reference is in [manifest_format.md][format].

## License

The template is MIT. Your mod is yours — replace this section.

[releases]: https://github.com/Mercenaries-Fan-Build/mercs2-wad-simulator/releases
[modkit]: https://github.com/Mercenaries-Fan-Build/mercs2-modkit
[format]: https://github.com/Mercenaries-Fan-Build/notes-on-the-released-game/blob/main/docs/modding/manifest_format.md
