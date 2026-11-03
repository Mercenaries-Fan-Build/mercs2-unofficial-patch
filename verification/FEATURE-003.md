# FEATURE-003 — winch magnet chain visual (seed form)

**Status:** opt-in, SEED. Enabled with `FEATURE_003 = 1` in `unofficial_patch.ini`. **Default: off.**
**This release does NOT yet make the chain visible.** It records the Havok chain geometry each time the game builds one, so the next iteration can attach a mesh per link without re-deriving the shape of `FUN_00443ff0`'s output.

## The missing behaviour

On PS3/Xbox, the vehicle winch magnet renders a visible chain between the winch head and the magnet attachment while a vehicle is being lifted. On PC the magnet works mechanically but no chain renders.

## Why it's missing

PC and console builds implement the winch chain two completely different ways. See the stackmap note `mercs2-console-parity-three-feature-investigation-2026-10-15` for the full derivation; summary:

- **PC** builds a Havok constraint chain: `hkpBallSocketChainData` + `hkpConstraintChainInstance` with N rigid-body links, via `FUN_00443ff0` (1,635 bytes), called from `FUN_00443450`'s per-link loop. 14 references to `hkp*Chain*` in the PC decomp. No attached visual.
- **Xbox** has zero references to `hkp*Chain*` / `hkpBallSocketChainData` across 545,861 lines of decomp. Xbox `AttachCargoToWinch = FUN_82333460` is a 70-byte function that only sets a bone-transform offset. Xbox drives the chain as a **skinned bone chain posed from Lua** via a `WinchBone` cfunc at Xbox `0x823EC538` fanning 10 scalars into `FUN_823D4030(0xffffffff83652008, …)` (the `Winch` class handle). **PC has no `WinchBone` Lua cfunc.**
- `exemap translate` confirms `FUN_00443450` / `FUN_00443ff0` / `FUN_008dc7a0` / `FUN_008dc8b0` have no Xbox pair across 1,957 transitive paths. Their immediate neighbours DO pair, so this is structural absence, not a pairing failure.

So PC has physics with no visual; console has a posed visual with no Havok physics. The chain mesh/bone source each uses is a different asset.

## This release's approach

Ride on top of PC's existing Havok chain rather than port the console path. The Havok chain already produces N rigid bodies with per-frame transforms — exactly what a per-link mesh needs for placement.

Two hooks are armed when `FEATURE_003 = 1`:

| VA | Function | What this release does |
|---|---|---|
| `0x00443FF0` | `FUN_00443ff0` — Havok rigid-body chain builder | Logs `this`, segment count at `[this+0x50]`, and build counter on every return. Records state for the next iteration. |
| `0x005D1FE0` | `FUN_005D1FE0` — Lua cfunc `AttachCargoToWinch` | Logs timing of the Lua winch trigger so Havok-chain builds can be correlated to winch deploys. |

Both hooks preserve game behaviour (log + tail-call the trampoline). No mesh is spawned yet.

## What this release's output enables for the next iteration

Once a capture confirms:
1. **Segment count** at `[this+0x50]` under a real `_DeployWinch`. If this is 0, `FUN_00443ff0` isn't being reached under the winch and the whole PC Havok-chain story is wrong.
2. **Rigid-body array layout.** The agent inferred the per-link bodies are at `(this+0x54)` or similar — confirmed by matching segment-count-from-log with array stride on live memory.
3. **A reusable mesh asset.** Candidates in the PC WAD based on the Xbox string table: `global_fencechain01`, `global_fencechain02`, `global_ropeties`. One iteration test would attach any of them per-link and see if the proportions work.

Then the mesh-attach code lands in this same file. The hook contract and the state record (`ChainRecord`) are already in place.

## Fallback (deferred)

Approach B — port the console path by registering a native `WinchBone` cfunc via the m2-sdk and shipping a skinned chain mesh — is held in reserve. It depends on:
- Whether the m2-sdk exposes a Lua binding registration path for new cfuncs (needs checking in `m2_lua.h`).
- Whether the magnet skeleton on PC has the bone names the console `WinchBone` cfunc writes to.
- Shipping a skinned chain mesh into the patch WAD if none is present.

That's three unknowns stacked. Approach A has one unknown (which mesh to pick), so Approach A ships first.

## Known limitations of the seed

- The hook only records the **most recent** Havok chain built. Two concurrent winches would overwrite the first record. Most vehicles have one magnet active at a time, so this is fine for the seed.
- The segment-count offset `+0x50` was identified by the chain-investigation agent and is INFERRED (static read of the per-link loop in `FUN_00443ff0`). If the log shows nonsense values, that offset is wrong and the next iteration patches it from live memory.
- `FEATURE_003` ships OFF because it writes to the log on every winch build. Enable it only when you're helping iterate.

## Hazards

The `0x00443FF0` hook races against `PROBE_001`'s earlier design (now removed) that also wanted this VA. The current layout keeps the Havok hook here only — but if a future PROBE pass adds another hook on `0x00443FF0`, this feature's install will fail with a MinHook error.
