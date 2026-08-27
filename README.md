# Mercenaries 2 Unofficial Patch

Bug fixes for **Mercenaries 2: World in Flames** (PC). Every fix is traced to specific code — a line
in the decompiled scripts or an address in the executable — and nothing ships on a guess.

Built and installed with [the Quartermaster][qm] (`qm`).

## What it fixes

| | fix | tier |
|---|---|---|
| **BUG-001** + **BUG-003** | the toolbox counter climbs on its own across save/reload, paying out cash and milestone vehicles for pickups that never happened | Lua |
| **BUG-006** | a hero swap hands back a full magazine and full health, whatever you swapped out with | Lua |
| **BUG-004** | changing cash, fuel capacity, character or costume **alone** is silently not autosaved | code |
| **BUG-007** | three PDA blip binders dereference NULL — a crash, on a stale or out-of-range blip id | code |
| **BUG-008** | `ShowLoadingHints(false)` never turns loading hints off; the flag can be set and never cleared | code |
| **BUG-009** | a co-op **client** crashes if a layer unloads while no game state is current | code |

Each has a card in [`verification/`](verification/) with how to reproduce the bug, what to expect
after, and what would count as the fix overreaching.

## What it deliberately does *not* fix

- **BUG-005** (weapons lost on a loadout restore) and **BUG-011** (the DLC infinite-ammo toggle is a
  no-op) are real and confirmed — and neither has a fix derivable from static evidence. Each is
  blocked on one named observation, recorded in [`native/src/fixpack.h`](native/src/fixpack.h). A
  detour built on a guess corrupts a live process rather than merely failing.
- **BUG-010** (the PDA support quick-slot) turned out **not to be a bug**: the feature works by
  another path, and the obvious one-line "repair" would have fired an airstrike the moment the player
  *selected* one. See [`verification/BUG-010.md`](verification/BUG-010.md).
- **The text pass** is triaged but unshipped — see [`verification/T1-text.md`](verification/T1-text.md).
  Of 37 console-platform strings found in the PC table, only 6 are referenced by anything at all. The
  rest are dead, and correcting text nothing displays is the trap that tier is famous for.

## Install

Grab `qm` from the [tools release page][releases], then:

```sh
git submodule update --init --recursive
make -C sdk build          # m2-sdk.dll + its import library
make -C native             # unofficial_patch.asi
qm build . --out build --corpus path/to/workshop_data/lua
```

Four files go to the game, and **all four are required**:

| file | destination |
|---|---|
| `build/mercs2-unofficial-patch.wad` | `<game>/data/vz-patch.wad` |
| `build/unofficial_patch.asi` | `<game>/scripts/` |
| `build/unofficial_patch.ini` | `<game>/scripts/` |
| `m2-sdk.dll` ([release][sdk]) | `<game>/scripts/` |

Full detail, and how to confirm the hooks actually armed, in
[`verification/README.md`](verification/README.md).

> ⚠ The WAD **rewrites** `data/vz-patch.wad`. The engine mounts exactly one `<stem>-patch.wad` per
> base WAD, so any other mod in that slot is replaced, not merged with.

> ⚠ `m2-sdk.dll` is a **load-time import**. Without it the plugin does not load at all and cannot
> report why — `LoadLibrary` fails before any of its code runs.

Every fix can be switched off individually in `unofficial_patch.ini`, without a rebuild. That is what
makes "which fix broke it" answerable when a pack misbehaves on a machine none of us has.

## How it is built

```
manifest.yaml     what this Shipment contributes
src/lua/          one Lua append per bug, appended to a game script
native/src/       one C file per bug, compiled into one .asi
sdk/              the shared m2 layer (submodule)
verification/     how to reproduce each bug, and what the fix must not break
```

One file per bug, on purpose: someone reading a register entry should be able to open exactly the
file named in it.

**Lua fixes are appends, not replacements.** Every script in a block loads together, so shipping a
finished block would silently erase every other mod's Lua. `qm` composes the appends instead.

**Code fixes are runtime detours, never byte patches to the shipped exe.** `.rdata`
registration-table writes trip SecuROM anti-tamper — a slot patch crashed early init — so everything
goes through `.text` MinHook, which the cracked build tolerates. The plugin also refuses to arm on
any build but the one its addresses were verified against: on another build MinHook would splice a
JMP into whatever happened to live at that address, so the failure mode of guessing is corruption
rather than a fix that does nothing.

## License

MIT.

[qm]: https://github.com/Mercenaries-Fan-Build/mercs2-wad-simulator
[releases]: https://github.com/Mercenaries-Fan-Build/mercs2-wad-simulator/releases
[sdk]: https://github.com/Mercenaries-Fan-Build/mercs2-sdk/releases
