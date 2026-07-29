# Verifying the fix pack

One card per bug. Each leads with **reproducing the bug**, not with installing the fix — the
register's bar is "reproduced before, observed fixed after", and a fix for a failure nobody watched
happen is not a verified fix.

| card | tier | status going in |
|---|---|---|
| [BUG-001](BUG-001.md) | T2 | built — toolbox count inflates on reload (with BUG-003) |
| [BUG-004](BUG-004.md) | T3 | built — **read this one first; it may be inert** |
| [BUG-006](BUG-006.md) | T2 | built — hero swap refills the magazine and full-heals |
| [BUG-007](BUG-007.md) | T3 | built — three PDA blip binders crash |
| [BUG-008](BUG-008.md) | T3 | built — loading hints cannot be turned off |
| [BUG-009](BUG-009.md) | T3 | built — co-op client crash |
| [BUG-010](BUG-010.md) | — | **NOT A BUG.** Nothing ships. Recorded so it is not re-derived. |
| [T1-text](T1-text.md) | T1 | tooling ready, **nothing shipped** — each item blocked on one observation |

Not shipped: **BUG-005** and **BUG-011**. Both real, neither derivable from static evidence — see the
note in [`native/src/fixpack.h`](../native/src/fixpack.h) for the single observation each needs.

## Build

```sh
git submodule update --init --recursive
make -C sdk build          # m2-sdk.dll + import library
make -C native             # unofficial_patch.asi
qm build . --out build --corpus <workshop_data/lua>
```

## Install

Three files, and **all three are required**:

| file | goes to | why |
|---|---|---|
| `build/mercs2-unofficial-patch.wad` | `<game>/data/vz-patch.wad` | the T2 Lua fixes |
| `build/unofficial_patch.asi` | `<game>/scripts/` | the T3 detours |
| `build/unofficial_patch.ini` | `<game>/scripts/` | per-fix switches |

plus **`m2-sdk.dll`** from the [mercs2-sdk release](https://github.com/Mercenaries-Fan-Build/mercs2-sdk/releases),
next to the `.asi`.

> ⚠ `m2-sdk.dll` is a **load-time import**. Without it the plugin does not load at all and *cannot
> report why* — `LoadLibrary` fails with `0x8007007E` before any of its code runs. pmc_bb logs only
> `[FAILED] unofficial_patch.asi (error: 0x...)`. If you see that, this is the first thing to check.

> ⚠ Deploying the WAD **rewrites** `vz-patch.wad`. The engine mounts exactly one `<stem>-patch.wad`
> per base WAD, so any other mod occupying that slot is replaced, not merged with.

## Check it armed before testing anything

`scripts/unofficial_patch.log`, written next to the `.asi`:

```
Mercenaries 2 unofficial patch, m2 0.1.0
BUG-004  armed   — profile changes to cash/fuel-capacity/character/costume are not autosaved
BUG-007  armed   — three PDA blip binders crash on a stale or out-of-range blip id
BUG-008  armed   — Gui.ShowLoadingHints(false) never turns loading hints off
BUG-009  armed   — Pg.UnloadLayer crashes a co-op client with no current game state
4 armed, 0 failed, 0 disabled
```

Anything else and stop — the results below mean nothing if the hooks did not take:

- **`FATAL: this is not the build the addresses were verified against`** — the pack refuses to arm on
  any build but the cracked de-SecuROM'd retail one (SizeOfImage 53,485,568 · CheckSum `0x00A5438D` ·
  ImageBase `0x00400000`). This is deliberate: every address was read out of that one image, so on
  another build MinHook would splice a JMP into whatever happens to live there. Corruption, not a
  no-op.
- **`FAILED`** on a line — that fix is not installed; the others may be. The id tells you which.
- **No log at all** — the `.asi` never loaded. See the `m2-sdk.dll` warning above.

## The fast loop

Logan's [`lua-bridge`](https://github.com/loganw234/Merc2-Mods-Exp) exposes the live Lua VM as a REPL
on `127.0.0.1:27050`. Several repros below are one-line pastes into it rather than gameplay
sequences, and it is the difference between confirming a fix in seconds and hunting for a
reproduction case.
