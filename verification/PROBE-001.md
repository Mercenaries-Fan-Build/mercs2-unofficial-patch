# PROBE-001 — console-parity diagnostic probe

**Status:** opt-in diagnostic. Enabled with `PROBE_001 = 1` in `unofficial_patch.ini`. **Default: off.**
**Behaviour change:** none. Every hook is read-only (log, tail-call the trampoline).
**Output:** `unofficial_patch.log` next to the plugin.

## Why this exists

Three PS3/Xbox-only visual features are missing on PC:

1. **Red laser-pointer line** — the laser designator shows a visible beam from the operator to the target point on console; on PC the designator works mechanically but has no visible beam.
2. **Bush burn-over-time** — on console, shooting or exploding a bush ignites it. Fire spawns, the bush burns for a few seconds, and leaves charred sticks. On PC the bush instantly explodes with no fire step.
3. **Winch magnet chain** — the vehicle winch has a visible chain between the winch head and the magnet on console. The magnet works on PC; the chain doesn't render.

Seven agent-rounds of static analysis across `/Volumes/Projects/mercenaries-game` falsified every obvious hypothesis for all three. See the stackmap note `mercs2-console-parity-three-feature-investigation-2026-10-15` for the full derivation. The common pattern: `exemap translate` returns no Xbox pair for the suspect PC function (`FUN_00666220`, `FUN_004B7B50`, `FUN_00443ff0`), which means PC and console build *different* engine implementations — not that console "has something extra" PC is missing.

Live x64dbg isn't available on this user's development machine (macOS). The probe captures the runtime signals needed to pick between the remaining hypotheses, from a single play session.

## What it hooks (feature #1 — laser)

| VA | Function | Why we hook it |
|---|---|---|
| `0x0066F300` | `FUN_0066F300` — WeaponFX class ctor | One-shot dump of the 12-byte dispatch array it fills at `&DAT_00ed3d20[DAT_01175d44]`. Each descriptor's render-slot address is logged. Comparing the slots to `FUN_00666220` (the candidate renderer) tells us whether the laser designator's render slot points at that function or a different one. |
| `0x00666220` | `FUN_00666220` — candidate render vmethod | Logs every call. If this hook never fires while a laser designator is active, `FUN_00666220` is NOT the laser's render path (`exemap translate` already returns no Xbox pair for it). If it fires but the beam ctor below never does, the type-3 branch at `0x006669BE` is rejecting. |
| `0x00494DD0` | `FUN_00494DD0` — beam-effect object ctor | Logs every call. This runs only when the type-3 branch takes AND the pool alloc returned non-NULL. If this fires in a laser-active scenario, the beam object IS being built — the fault lies in queueing or draw-state downstream. |

## What it hooks (feature #2 — bush burn)

| VA | Function | Why we hook it |
|---|---|---|
| `0x004D3E10` | `FUN_004D3E10` — SetState dispatcher | Every destruction-FSM transition logs entity id + state hash. Watch for `0x1D5575A1 Damaged`, `0x7687DF41 Destroyed`, `0x92791EBB StartDestroyed` on a bush after shooting it. If the bush jumps straight to Destroyed/StartDestroyed, the force-destroy path is firing and the burn chain never runs. If it goes through Damaged first, the fault is in whether `OnStateChange(FireDebrisState/FireDestroyedState/CollapseFireState)` resolves to burn FX on PC. |
| `0x004B7B50` | `FUN_004B7B50` — ignitor kind filter | Each call reads `(**(*this+0xE0))(this)` to classify the object. The hook logs `this`, its vtable, the +0xE0 vcall address, and the filter's return value. On a scrub/bush ignition, this tells us (a) whether the ignitor system ever sees bushes at all, and (b) what kind code bushes report. If bushes never reach this filter the gate is upstream in `FUN_004B7B00`'s overlap query. |

## What it does NOT hook (feature #3 — winch chain)

The two winch hooks live in `feature_003_winch_chain.c`, not here, so that `FEATURE_003` can be enabled without `PROBE_001`. See `verification/FEATURE-003.md`.

## Capture procedure

Enable in `scripts\unofficial_patch.ini`:

```ini
PROBE_001 = 1
FEATURE_003 = 1    ; optional — gives winch telemetry too
```

Launch the game. For each of the three features, run a short scenario and tag the log with when you did it:

1. **Laser**: in free-roam, call in a laser-guided airstrike support. Equip the designator. Point at an open area. Hold the designator for 2–3 seconds. Fire the strike. Return to inventory.
2. **Bush**: find a bush/foliage object (not a tree yet — a small bush). Shoot it with a small arm (not a rocket) to raise damage without instantly destroying. Watch and log for ~5 seconds. Then RPG a different bush and compare.
3. **Winch**: call in a copter drop that uses the magnet (any vehicle delivery). Watch the magnet attach, lift, and release.

Zip and return `unofficial_patch.log`. If you can also capture a short screen recording of the three scenarios, that lets us correlate the log timestamps to visible events.

## Rate limiting

The probe emits every hook's first 16 fires unconditionally, then one line per 256 fires. This keeps per-frame hooks (`FUN_00666220` is called often) from flooding the log. If a scenario needs the raw stream, change `SAMPLE_EVERY` in `probe_console_parity.c` and rebuild.

## What the output drives

- Laser: pick between `FUN_00666220` being the laser render path (hook fires) vs a different function (hook never fires). If a different function, dump the WeaponFX array to find which slot has a different VA.
- Bush: pick between the force-destroy path firing (jumps straight to Destroyed), the Damaged→FireDebris chain firing but Lua handlers not reaching burn FX, or the ignitor never querying bushes at all.
- Winch: see `verification/FEATURE-003.md` — the Havok chain log tells us segment count and `this` layout for the next iteration's mesh attach.
