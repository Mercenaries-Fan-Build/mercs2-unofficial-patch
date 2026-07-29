# Examples

One runnable Shipment per capability. Lint any of them with no game install:

```sh
qm lint examples/01-overwrite-a-texture
```

CI lints every one on each push, and checks that all seven contribution kinds still have an example —
so this material cannot quietly drift behind the format.

| | kind | teaches | builds? |
|---|---|---|---|
| [01-overwrite-a-texture](01-overwrite-a-texture) | `replace_texture` | replacing existing content | yes |
| [02-add-a-model](02-add-a-model) | `add_model` | adding a new asset, and donors | yes |
| [03-add-an-outfit](03-add-an-outfit) | `add_outfit` | the composed kind — geometry + wardrobe | yes |
| [04-bundle-lua](04-bundle-lua) | `patch_lua` | shipping Lua as an append | yes |
| [05-two-mods-coexisting](05-two-mods-coexisting) | `patch_lua` ×2 | two mods on one script, both surviving | via `qm link` |
| [06-ship-a-plugin](06-ship-a-plugin) | `native_hook` | native code, and file placement | yes |
| [07-raw-bytes](07-raw-bytes) | `raw` | shipping bytes with a declared blast radius | yes |
| [08-not-supported-yet](08-not-supported-yet) | `edit_state_machine` | lint ≠ build | **no, on purpose** |
| [09-what-the-linter-catches](09-what-the-linter-catches) | — | the failures, on purpose | fails lint |

All nine build with the pinned `qm`; `raw` and `native_hook` need v0.10.2 or later.

## The two halves

**01** overwrites: same asset hash, your bytes. Your overlay is mounted last and wins the lookup, and
the base WAD is never touched — uninstalling is deleting one file.

**02** adds: a new hash that did not exist. Nothing in the base game changes, and two Shipments
adding different names can never conflict.

Most real mods do both. The root [`manifest.yaml`](../manifest.yaml) does.

## The interesting one

**05** is the case this whole format exists for.

Scripts do not load individually — all 114 live in one block. If each mod shipped its own copy, the
last installed would erase every other mod's Lua, silently. So a Shipment declares an *append*, and
`qm link` composes every installed Shipment's appends onto one base and compiles once:

```sh
qm link examples/05-two-mods-coexisting/alpha \
        examples/05-two-mods-coexisting/beta \
        --out /tmp/link --corpus <corpus>
```

```
linking 2 mutation(s) from 2 Shipment(s)
linked wifpmcinterior: 58828 → 60297 B source, from ["alpha-mod", "beta-mod"]
```

Both survive, and neither mod knows the other exists.

Ordering is by **Shipment name, not install order**. A saved costume is stored as a *position* in the
outfit list, so if install order set the indices, reinstalling mods in a different order would
silently re-dress the player — or leave a saved index pointing at nothing and wedge the load.

## Where the guard rails are

**07** is the escape hatch: opaque bytes plus a blast radius you declare by hand. The Quartermaster
cannot read intent out of bytes it cannot parse, so it takes your word — and then holds you to it.
`touches` must match the payload's entry table exactly *in both directions*, because claiming
something absent and carrying something unclaimed are both silent failures in the game.

**06** is the only kind that does not go into a WAD at all. It places a file, and emits a record of
what went where with its digest — because deleting one overlay WAD undoes a mod, but a file drop
cannot be reversed unless something wrote down what was placed.

**08** lints clean and refuses to build. Passing the linter means your manifest is well-formed, not
that everything in it can be produced. The refusal names four specific gaps and points at `raw` as
today's workaround — because "not implemented" tells you nothing you can act on.

## The failures are the point

**09** contains Shipments meant to be broken, and CI asserts they still fail. That check matters more
than it looks: without it, every passing example above would stay green with the linter entirely
disabled.

Each one is a real mistake with no in-game symptom. The game does not report asset errors — it hangs
on a loading screen, or loads fine and quietly does nothing.
