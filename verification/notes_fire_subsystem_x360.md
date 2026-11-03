# Fire-propagation subsystem — X360 retail map

## Build identity

- Decomp (primary, large/full-named):
  `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail_xenon/xenon_retail_decomp_named.c`
  (44 MB, 1 619 000+ lines; retail xex image, pandemic engine)
- Decomp (small, strings easier to resolve):
  `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail/xenon_retail_decomp_named.c`
- Raw loaded image (PROVEN): `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail/default.pe_ghidra.bin`
  (26 607 616 bytes; PE loaded at image base `0x82000000`;
  verified by file-offset = VA − 0x82000000 recovery of the five class vtables listed below.)
- PPC-xenon is big-endian; every vtable slot I quote was read as a BE 32-bit
  word from this image at that VA.

## Class registry (confirmed cross-ref anchors)

PROVEN — I re-dumped each registrar body plus its vtable slots from the raw
image.

| Class            | Registrar VA / decomp line (retail-xenon)      | Vtable VA     | Instance size (slot 0x30) | Flags (+0x08 of record) | Class record base | Name string field (+0x3c) |
|------------------|------------------------------------------------|---------------|----------------------------|-------------------------|-------------------|---------------------------|
| Explosive        | `FUN_823a6210` / line 415247                   | `0x820dcb60`  | `0x24` (`FUN_823b7070`)    | `0`                     | `0x836528c4`      | "Explosive" line 415270   |
| Flammable        | `FUN_823bb410` / line 431530                   | `0x820df328`  | `0x04` (`FUN_823b4d18`)    | `0`                     | `0x8365500c`      | "Flammable" (sibling)     |
| Ignitor          | `FUN_823bb5c8` / line 431615                   | `0x820df378`  | `0x0c` (`FUN_823a99b8`)    | `0`                     | `0x83655058`      | "Ignitor"  line 431638    |
| RuntimeExplosion | `FUN_823c1c50` / line 436547                   | `0x820e0860`  | `0x40` (`FUN_822252c8`)    | `8`                     | `0x836564f4`      | "RuntimeExplosion" line 436570 |
| RuntimeIgnitor   | `FUN_823c3950` / line 438089 (via dispatch tbl)| `0x820e0e98`  | `0x1c` (`FUN_823abf98`)    | `0x1c` (from `DAT_83656b30`) | `0x83656b0c` | "RuntimeIgnitor" line 438112 |

Note the Explosive-flags value (`DAT_836528cc = 0`) differs from
RuntimeExplosion's (`DAT_836564fc = 8`). Many hot paths gate on
`((flags >> 3) & 1) == 0` — i.e. flags-bit-3 clear ⇒ use the stale-but-fast
lookup `FUN_823daf68`, otherwise call the slow `FUN_823db8f0`. For
RuntimeExplosion flags-bit-3 is **set** (flags=8), i.e. the generation counter
is treated as potentially stale and every lookup re-walks.

## Vtable: RuntimeExplosion (0x820e0860)

PROVEN — read 16 big-endian slots directly from
`default.pe_ghidra.bin` at file offset `0xe0860`.

| Slot | +off | Pointer         | Function role (semantics explained below)           |
|------|------|-----------------|------------------------------------------------------|
| 0    | 0x00 | `0x828280b0`    | common no-op "class-level ctor placeholder"          |
| 1    | 0x04 | `0x8225e998`    | common 4-byte stub (returns an undefined4)           |
| 2    | 0x08 | `0x823d13f8`    | **per-class record-ctor**: `*p = &PTR_FUN_820e4350`, `p[6] = &PTR_FUN_820e4448`, size 84 (line 448374) |
| 3    | 0x0c | `0x828280b0`    | common no-op                                         |
| 4    | 0x10 | `0x823c1d30`    | **per-class instance allocator** (`FUN_82861bdc`+`FUN_823db8f0`+`FUN_823f7b88`, size 88, line 436577) |
| 5    | 0x14 | `0x823aeda0`    | common "release/tick" (`FUN_823db8f0(this+0x18)`, size 8, line 421971) |
| 6    | 0x18 | `0x823c1d88`    | **per-class install-from-template**: `FUN_823d6d48(this, entity, template)` (size 8, line 436595); the "add RuntimeExplosion to pool" callback |
| 7    | 0x1c | `0x823c1d90`    | thunk → `FUN_822931b0` (destructor-ish, size 104)   |
| 8    | 0x20 | `0x823ac4c8`    | common `FUN_823db558(this+0x18)`, size 8 (line 419997) |
| 9    | 0x24 | `0x823bc888`    | common "get +0x1c" of pool (`return p[0x1c]`), size 8 |
| 10   | 0x28 | `0x823b9fe0`    | common "pool-slot pointer formula" using +0x20/+0x24/+0x26 (size 52, line 430553) |
| 11   | 0x2c | `0x823b0fe8`    | common "get +0x20" of pool, size 8 (line 423622)    |
| 12   | 0x30 | `0x822252c8`    | **per-class instance-size getter**: `return 0x40` (size 8, line 155836) |
| 13   | 0x34 | `0x823c1d98`    | **per-class RTTI constant**: `return 0xffffffff820e3ba8` (size 12, line 436616) |
| 14   | 0x38 | `0x823a9ab0`    | common setter: `this[0xc]=param_2; this[0x40]=param_2` (size 12, line 417916) |
| 15   | 0x3c | `0x8248f6c8`    | common 8-byte stub                                   |

### IMPORTANT STRUCTURAL FINDING — PROVEN

The vtable at `0x820e0860` is a **COMP-registry metadata vtable**, not a
per-instance behavior vtable. Its slots describe *class-level* operations
(alloc an instance, install from template, read instance size, read RTTI
token, iterate pool). There is **no per-frame "tick RuntimeExplosion" slot
here.** The only writer of `&PTR_FUN_820e0860` in the entire decomp is the
registrar at line 436553 — i.e. this pointer is never stored into any live
object. The on-wire RuntimeExplosion record is a POD stored in a hash pool
rooted at `DAT_836564f4 + 0x18`.

The per-instance behavior of a RuntimeExplosion is dispatched through a
**different** vtable at `0x820eae2c` (see "Function: X360 equivalent of PC
FUN_0066ae30" below), which the matcher will need to anchor separately.

## Vtable: Explosive (0x820dcb60)

PROVEN — same method, 16 BE slots at file offset `0xdcb60`.

| Slot | +off | Pointer         | Role                                                 |
|------|------|-----------------|------------------------------------------------------|
| 0    | 0x00 | `0x828280b0`    | common                                               |
| 1    | 0x04 | `0x8225e998`    | common                                               |
| 2    | 0x08 | `0x823cbcc0`    | per-class record-ctor: `*p=&PTR_FUN_820e4350`, `p[6]=&PTR_FUN_820e4448` (size 84, line 444987) — IDENTICAL body to RuntimeExplosion's slot 2 (`FUN_823d13f8`). The two share the same code path. |
| 3    | 0x0c | `0x828280b0`    | common                                               |
| 4    | 0x10 | `0x823d8f08`    | per-class instance allocator (same shape as `FUN_823c1d30`, size 88) |
| 5    | 0x14 | `0x823aeda0`    | common                                               |
| 6    | 0x18 | `0x823d8f00`    | per-class install-from-template → `FUN_823d9310(this,entity,template)` (size 8, line 454554) |
| 7    | 0x1c | `0x823d8e98`    | per-class worker: operates on `*p+0x48`, calls `FUN_823f7e08` if `DAT_82b7524b != 0` (size 104, line 454537) |
| 8..11| 0x20..0x2c | common | identical to RuntimeExplosion                       |
| 12   | 0x30 | `0x823b7070`    | **per-class instance-size getter**: `return 0x24` (size 8, line 428286) |
| 13   | 0x34 | `0x823ff500`    | **per-class RTTI constant**: `return 0xffffffff820e3164` (size 12, line 474324) |
| 14..15 | 0x38..0x3c | common | identical to RuntimeExplosion                       |

## Vtable: Flammable (0x820df328), Ignitor (0x820df378), RuntimeIgnitor (0x820e0e98)

PROVEN — same methodology; recorded for the matcher.

| Slot | Flammable      | Ignitor        | RuntimeIgnitor |
|------|----------------|----------------|----------------|
| 0    | `0x828280b0`   | `0x828280b0`   | `0x828280b0`   |
| 1    | `0x8225e998`   | `0x8225e998`   | `0x8225e998`   |
| 2    | `0x823cff28`   | `0x823cff80`   | `0x823d1aa8`   |
| 3    | `0x828280b0`   | `0x828280b0`   | `0x828280b0`   |
| 4    | `0x823bb4f0`   | `0x823bb6a8`   | `0x823c3a30`   |
| 5    | `0x823aeda0`   | `0x823aeda0`   | `0x823aeda0`   |
| 6    | `0x823bb548` → `FUN_823d6048` | `0x823bb700` → `FUN_823d60b0` | `0x823c3a88` → `FUN_823d71c0` |
| 7    | `0x823bb550`   | `0x823bb708`   | `0x823c3a90` thunk→`FUN_823d7228` |
| 8    | `0x823ac4c8`   | `0x823ac4c8`   | `0x823ac4c8`   |
| 9    | `0x823bc888`   | `0x823bc888`   | `0x823bc888`   |
| 10   | `0x823b9fe0`   | `0x823b9fe0`   | `0x823b9fe0`   |
| 11   | `0x823b0fe8`   | `0x823b0fe8`   | `0x823b0fe8`   |
| 12   | `0x823b4d18` → `return 4` | `0x823a99b8` → `return 0xc` | `0x823abf98` → `return 0x1c` |
| 13   | `0x823bb5b8` → `0x820e37cc` | `0x82404718` → `0x820e37d8` | `0x823c3a98` → `0x820e3d24` |
| 14   | `0x823a9ab0`   | `0x823a9ab0`   | `0x823a9ab0`   |
| 15   | `0x8248f6c8`   | `0x8248f6c8`   | `0x8248f6c8`   |

## Function: X360 equivalent of PC FUN_0052c730 (per-frame RuntimeExplosion iterator)

**Not located as a 1:1 analog.** PROVEN NEGATIVE: no vtable slot in
`0x820e0860` is a per-frame tick, and the only reference to the
RuntimeExplosion flags field (`DAT_836564fc`) outside its own registrar is
inside the big function `FUN_82289650` at line 224594 (see "FUN_0052bd60"
below) — which is a per-overlap, per-target invocation, **not** a per-frame
pool walk.

Candidate structural analog on X360: `FUN_82289650` itself. SPECULATIVE
label: there is likely a top-level dispatcher that drives this per entity
being damaged, but a dedicated "iterate every live RuntimeExplosion and
tick it" has not been located; the engine appears to drive explosions
**event-based** (produce → install → one-shot damage sweep → cleanup via
destroyer `FUN_823ff558`), not iterated.

## Function: X360 equivalent of PC FUN_0052aec0 (per-explosion overlap+setup)

**Not located as a 1:1 analog.** The setup+sweep is baked into the same big
function `FUN_82289650` — the overlap list is built in its outer loop and
each hit goes through the per-target block that includes the RuntimeExplosion
touch at line 224594 (below).

## Function: X360 equivalent of PC FUN_0052bd60 (per-overlap damage apply)

**PROVEN: `FUN_82289650` body @ decomp line 223862 (retail-xenon),
VA `0x82289650`, size 2288 bytes.**

- Reads `Explosive[+0x08]` (class record flags) via `DAT_836528cc` — not
  shown here but present in sibling callers (lines 188048, 290319, 294317,
  294652, 294679, 357049, 357141, 385144).
- Reads `RuntimeExplosion[+0x08]` (class record flags) via `DAT_836564fc` at
  line 224594 — this is the gate controlling whether the per-target lookup
  uses the fast or slow pool-handle path.
- Looks up (or creates) a RuntimeExplosion for each **overlapped target**
  (local `iVar12`) via `FUN_823db8f0(0xffffffff8365650c, iVar12)` and writes
  a float at `+0x28` of the resulting handle (line 224604:
  `*(float *)(iVar13 + 0x28) = fVar1;`).
- Also looks up RuntimeHealth (`DAT_83656548 / 0x83656558`), RuntimeNodeHealth
  (`DAT_83656594 / 0x836565a4`), and the SceneObject COMP (`DAT_8365807c /
  0x8365808c`) on the target.
- Calls `FUN_82246a08(..., iVar12, &local_3b0, 0)` at line 224590 — the generic
  **damage-ledger apply** (size 392, line 179327). This records an entry in
  the per-context damage ring (iVar3+0xde0 counter) but does **not** emit
  particles or ignite anything by itself.

**Explicit answer: does it directly ignite?**

> **PROVEN NEGATIVE in the lines read:** the body at lines 224450-224769
> contains **no call to the "StartEmitter" dispatch** (string literal at
> VA `0x820d6d30`, found by raw-image scan at file offset `0xd6d30`) and
> **no reference to "fire"**. The overlap path records damage and touches a
> per-target RuntimeExplosion handle timestamp; it does **not** emit a fire
> particle itself. Fire ignition on this platform therefore propagates
> **indirectly**, through either:
> (a) a FSM/state-transition on the damaged entity that in turn dispatches
>     the "StartEmitter" message against its own state table (the dispatch
>     table is `DAT_836db808` seeded with the "StartEmitter" string at
>     preview-xenon:304978 under hash `0x3347914f`), or
> (b) a RuntimeIgnitor spawned by a separate chain whose entry point is
>     not inside `FUN_82289650`.
> This matches the sibling's finding for PC: on PC, the only
> `StartEmitter("fire")` call site tied to this chain (FUN_004D28C0) is
> invoked from an entity's destruction FSM, not directly from the explosion.

**Callees (unique code-pointer calls visible in the body):**
| VA           | Role (where identifiable)                             |
|--------------|--------------------------------------------------------|
| `FUN_8217d8a0` | event post ("damage registered")                     |
| `FUN_82184598` | entity → component pointer                           |
| `FUN_822c3e8` *( `FUN_8222c3e8` )* | container lookup                     |
| `FUN_8225e998` | common 4-byte stub                                    |
| `FUN_82246a08` | **damage-ledger apply** (size 392)                    |
| `FUN_82289580` | pre-filter per hit                                    |
| `FUN_82282da0` | damage post-apply hook                                |
| `FUN_822d9990` | per-rig processing                                    |
| `FUN_82287f98` / `FUN_82287ec0` / `FUN_82298cd0` | per-platform cleanup |
| `FUN_82298cd0` | ... |
| `FUN_823d9e10` / `FUN_823dab38` / `FUN_823da2a0` / `FUN_823da3c8` / `FUN_823daf68` / `FUN_823db8f0` | pool helpers |
| `FUN_823f7b88` | class-pool "resolve-or-rehash" (size 368, line 160770) |
| `FUN_823f7ee8` | per-entity child enumeration                          |
| `FUN_82409b38` | hitbox/geom test (used across hit sweeps)             |
| `FUN_82467778` / `FUN_82467a38` / `FUN_82467ab0` / `FUN_82451e80` | shock-wave helpers |
| `FUN_82855cc0` / `FUN_82855e08` / `FUN_82856358` / `FUN_82854dd0` / `FUN_8215be60` / `FUN_82855fb8` / `FUN_8215e260` | math/vec helpers |
| `FUN_82232648` / `FUN_82232558` / `FUN_82232a08` | event posters                        |

**Notable constants / floats** (helpful for signature-match):
- `0x836564fc` (RuntimeExplosion flags) — unique data address appearing in
  the body.
- `0x8365650c` (RuntimeExplosion class record + 0x18, the pool-handle table).
- `0x82de698d`, `0x82de698f`, `0x82de6990`, `0x82de6998` — HUD/feedback flags.
- `0x820d9edc`, `0x820baf84`, `0x820baee8`, `0x820ca19c` — world-space float
  constants used in the knockback vector.

## Function: X360 equivalent of PC FUN_0066ae30 (Explosive-COMP producer)

**PROVEN: `FUN_82291440` @ decomp line 229096 (retail-xenon),
VA `0x82291440`, size 312 bytes.**

Body summary (lines 229098-229146):
```
piVar2 = FUN_82861bdc();                            // alloc 0x40-byte slot in the current class pool
*piVar2            = &DAT_83658074;                 // SceneObject class record ptr (vtbl 0x820e2b88)
piVar2[1]          = param_2;                       // target entity id
piVar2[2]          = DAT_836580bc - 1;              // SceneObject class generation seed
piVar2[2]          = *(int*)(piVar2[0]+0x48) - 1;
piVar2[3]          = 0;
piVar2[4]          = &DAT_836528c4;                 // *** Explosive class record ptr (vtbl 0x820dcb60) ***
piVar2[5]          = param_2;                       // target entity id
piVar2[6]          = DAT_8365290c - 1;              // Explosive class generation seed
piVar2[6]          = *(int*)(piVar2[4]+0x48) - 1;
piVar2[7]          = 0;
piVar2[0xe]        = DAT_820baf14;                  // world-space float constant
piVar2[0xd]        = 2;                             // default Detail = 2 (overwritten next)
*(byte*)(piVar2+0xf) = *(byte*)(piVar2+0xf) & 0x1f | 0x80;
piVar2[0xb]        = FUN_82398fc0(param_2);         // entity hash / resolved id
piVar2[0xc]        = FUN_823f7b88(param_2);         // another entity-derived id
FUN_82293218(piVar2 + 4);                           // resolve the Explosive cursor (piVar2[7] := Explosive instance)
piVar2[0xd]        = *(int *)(piVar2[7] + 0x20);    // *** copy Explosive[+0x20] (Detail) ***
// RedEffectComponent lookup
if ((DAT_83655144 >> 3 & 1) == 0)
   uVar4 = FUN_823daf68(0xffffffff83655154, param_2);
else
   uVar4 = FUN_823db8f0();
*(byte *)(piVar2 + 0xf) = (byte)(LZCOUNT(uVar4) << 2) & 0x80 | *(byte*)(piVar2+0xf) & 0x7f;
return piVar2;
```

- The 0x40-byte struct matches RuntimeExplosion's declared instance size
  (slot 12 of the class vtable, `FUN_822252c8` returns 0x40).
- Fields piVar2[0..3] are the **SceneObject-cursor** (base ptr + entity +
  generation seed + resolved instance), piVar2[4..7] are the
  **Explosive-cursor**. piVar2[0xd] (offset `+0x34`) ends up holding the
  Explosive's **Detail** value, pulled directly from Explosive instance
  `+0x20` — matching the cross-platform-byte-identical Explosive layout
  the sibling already proved.
- `RedEffectComponent` (base `0x8365513c`, vtable `0x820df468`, "RedEffectComponent"
  @ line 431884) is also consulted — SPECULATIVE: this is the per-colour-tint
  effect routing; not the fire ignition.

**Install site (how FUN_82291440 is used):**

**PROVEN** — `FUN_823ff510` @ VA `0x823ff510`, size 68, line 474333:
```
void FUN_823ff510(undefined8 param_1, undefined8 param_2) {
    undefined1 auStack_50 [64];                      // 0x40 stack buffer = RuntimeExplosion template
    uVar1 = FUN_82291440(auStack_50);                // build it
    FUN_823d6d48(0xffffffff836564f4, param_2, uVar1); // insert into RuntimeExplosion pool
    return;
}
```
`FUN_823d6d48` is the RuntimeExplosion vtable's **slot 6 target** (install-from-template, line 452768), and its first arg `0x836564f4` is the RuntimeExplosion class record base. The template is copy-inserted into the pool here, and if `DAT_82b7524b != 0` a `FUN_823f7cf8` callback fires (tracing/eventing).

**Instance-vtable binding of FUN_823ff510 — PROVEN by raw-image scan:**
- Function pointer `0x823ff510` appears at file offsets `0xeae44`
  (VA `0x820eae44`) and `0x111850` (VA `0x82111850`).
- The second site is a dispatch registration table — adjacent 8-byte entries
  are {code pointer, flag word 0x40xxxxxx} across many functions in the
  explosion/health/damage subsystem.
- The first site is **inside a per-RTTI handler vtable**: decomp line 479451-479454
  shows `uVar3 = FUN_82855718(0xffffffff820e3164); puVar2[1] = uVar3;
  puVar2[2] = DAT_836528c8; *puVar2 = &PTR_FUN_820eae2c;` — i.e. for the
  Explosive RTTI token `0x820e3164` (= Explosive vtable slot 13 constant)
  the engine instantiates a cursor whose vtable base is `0x820eae2c`, and
  `0x820eae44` sits at `0x820eae2c + 0x18` — that is **slot 6 (+0x18)** of
  the Explosive COMP's **instance-vtable**. Pattern-match: slot 6 on the
  Explosive instance-vtable is the "spawn runtime instance" callback (same
  slot-6 role as the class-metadata vtable's `install-from-template`), and
  on X360 it is `FUN_823ff510` (producer + inserter), parallel to PC's
  `FUN_0066ae30`.

**Explicit answer: does the producer install anything into the Ignitor subsystem?**

> **PROVEN NEGATIVE in the lines read:** `FUN_82291440` references only
> SceneObject (`DAT_83658074`), Explosive (`DAT_836528c4`), and
> RedEffectComponent (`DAT_83655144`) class records. There is no
> reference to Flammable (`DAT_8365500c`), Ignitor (`DAT_83655058`), or
> RuntimeIgnitor (`DAT_83656b0c`) anywhere in its body (lines 229098-229146).
> The producer mints a RuntimeExplosion only; it does not couple to the
> Ignitor subsystem. If a RuntimeIgnitor is minted on the overlapped
> target, that must happen somewhere else (candidates not yet verified
> this run: a Flammable-COMP handler reacting to a damage-ledger event
> through the state-machine).

## Fire-ignition reach

**On X360, fire does not start inside the explosion-overlap path.** PROVEN
NEGATIVE: the string "StartEmitter" lives at VA `0x820d6d30` (raw-image
scan hit; sibling note confirmed the dispatch-table seeding at
preview-xenon:304978 under hash `0x3347914f`), and neither `FUN_82289650`
(per-overlap damage apply) nor `FUN_82291440` / `FUN_823ff510`
(RuntimeExplosion producer + installer) contains any direct reference to
`0x820d6d30` or to the hash `0x3347914f`.

The fire-start path on X360 therefore flows through the same **script /
state-machine indirection** that PC uses:
1. `FUN_82289650` registers damage via `FUN_82246a08` and timestamps the
   per-target RuntimeExplosion handle at offset `+0x28`.
2. The target entity's own state machine (driven by the dispatch table at
   `DAT_836db808`) reacts to that damage/state change by issuing a
   `StartEmitter` with the authored emitter name — "fire" on canopies,
   bushes, etc.

I did **not** independently verify the state-machine side of this chain
this run; the "StartEmitter is only hit via FSM" claim here is the natural
extrapolation from the hard-negative call-site search, but confidence is
INFERRED, not PROVEN.

## Diff hypothesis vs PC

**No single wiring difference inside the explosion subsystem explains
"console C4 ignites canopy, PC C4 doesn't"**, based on what I can read this
run. The two files I audited:

- `FUN_82291440` (X360 producer) mints only a RuntimeExplosion. PC's
  `FUN_0066ae30` does the same. **No RuntimeIgnitor mint here on X360.**
- `FUN_82289650` (X360 per-overlap damage apply) does not call StartEmitter.
  PC's `FUN_0052bd60` similarly does not call StartEmitter directly, per the
  sibling note.

**SPECULATIVE candidate for the real diff** — not confirmed this run:
RedEffectComponent (`0x8365513c`, instance size `0x38`) is consulted by the
X360 producer at line 229138 but is NOT in the PC/X360 shared-class list.
If RedEffectComponent (console-only?) feeds into the Flammable↔Ignitor
coupling on canopies via a different COMP, that could be the missing wire.
PC-side verification would need to check whether PC's `FUN_0066ae30` touches
any class record with the same role.

Alternative SPECULATIVE candidate: the Explosive flags gate
(`DAT_836528cc`, 90+ call sites gating on `(flags >> 3) & 1 == 0`) is set to
`0` on X360 at init, meaning every Explosive-handle lookup uses the fast
`FUN_823daf68` path. If PC inverts that (flags = 8 to force
`FUN_823db8f0`), the Explosive instance resolution on the overlapped
target could silently return null on PC, which would collapse the whole
canopy-burn chain. **Not verified against PC bytes.**

## Exemap seed candidates

| PC VA        | X360 VA        | Confidence   | Rationale                                                                 |
|--------------|----------------|--------------|---------------------------------------------------------------------------|
| `0x0066ae30` | `0x82291440`   | INFERRED     | Both are the Explosive-COMP → RuntimeExplosion producer. Same 0x40-byte template layout; both read Detail from the Explosive's `+0x20`; both store entity + two class-record pointers + generation seeds; both consult a secondary COMP after main setup. Byte-signature anchors in `FUN_82291440`: `DAT_836528c4` (Explosive record), `DAT_83658074` (SceneObject), `DAT_83655144` (RedEffectComponent). |
| `0x0066ae30` | `0x823ff510`   | INFERRED     | Alternative pair — the "mint + install" wrapper. If the PC function has only one call to a producer + one `FUN_823d6d48`-shaped pool-insert, this is the stronger PC pairing. 68-byte function with exactly two calls and an auto 0x40 stack slot is highly distinctive. |
| `0x0052bd60` | `0x82289650`   | SPECULATIVE  | Both are the per-overlap damage dispatcher sitting between Explosive overlap and per-target handle update. 2288-byte X360 function is much larger than typical PC analogs — the inlining pattern differs across platforms (sibling noted "compiled differently cross-platform"), so a slot-level pair is unreliable. Byte-signatures to probe: the sequence `FUN_823db8f0 → FUN_823f7b88 → FUN_823db8f0` with first arg = `0x8365650c` is unique to this per-target-handle lookup. |
| `0x0052c730` | (none proved)  | –            | No pure per-frame RuntimeExplosion iterator found on X360. If PC's FUN_0052c730 is a hot-loop walker, the X360 equivalent may be absent (the engine is event-driven here on console). Do not seed a pair. |
| `0x0052aec0` | (none proved)  | –            | Same reasoning: the "per-explosion overlap+setup" phase on X360 is inlined into `FUN_82289650`'s outer walk. Suggesting a pair would be a guess. |
| `0x004D28C0` | (none proved)  | –            | PC's "StartEmitter('fire') from an entity's destruction FSM". The X360 analog is through the dispatch table seeded at preview-xenon:304978 (`DAT_836db808`, hash `0x3347914f`). Not located as a single function this run; needs a separate pass. |
| `0x004B7C40` | (none proved)  | –            | Not located. Needs more PC-side anchoring before an X360 pair can be seeded with confidence. |

**Cross-platform byte-signature anchors (useful outside slot-level matching):**

- Explosive instance size `0x24` : identical across platforms (sibling PROVEN).
- RuntimeExplosion instance size `0x40` : X360 slot `0x30` returns `0x40`.
- Explosive Detail at `+0x20` : X360 producer reads `*(int*)(piVar2[7]+0x20)`.
- Hash seed `0x9e3779b9` (golden ratio): shared constant in every COMP class
  record on X360; PC should have the same value in the same offset relative
  to each class record.

## Adversarial pass

**Strongest competing explanation** for the differences I observed:

> "The vtables at `0x820dcb60` / `0x820e0860` really are per-instance behavior
> vtables, I just missed the per-frame tick slot."

**Does it survive?** No. The sibling-identified vtables are referenced
exactly once each by the registrar (`grep` of `PTR_FUN_820dcb60` etc. in all
four decomp sources returned 5 total hits, all registrar writes). A
per-instance vtable would be written into every new instance, which would
produce dozens of indirect writes. The pointer-count disparity rules this
out. The real per-instance vtable is `0x820eae2c` (bound by RTTI token
`0x820e3164` at decomp line 479451), which is independently reachable and
IS the one that holds `FUN_823ff510` at slot 6.

> "FUN_82289650 is for a different projectile system (homing / grenade)."

**Does it survive?** Weakly. The function is very large (2288 bytes) and
touches many component classes. However, the specific RuntimeExplosion
flags DAT (`DAT_836564fc`) is only referenced from this body and from
RuntimeExplosion's own registrar — only 2 occurrences in the whole decomp.
That coincidence anchors FUN_82289650 to the RuntimeExplosion subsystem
specifically.

> "FUN_82291440 is a generic COMP-handle resolver, not an Explosive mint."

**Does it survive?** No. It writes `&DAT_836528c4` (literal Explosive
class record ptr) into piVar2[4] — that's deterministic Explosive-COMP
wiring; a generic resolver would take the class record as a parameter.

## Not verified

- The per-frame RuntimeExplosion tick (if any) on X360. The engine looks
  event-driven here, but a `grep`-only confirmation isn't the same as a
  full disassembly of every call site to the install-from-template
  callback.
- Whether `FUN_823d6d48` really inserts into the hash pool (I only read its
  prologue/epilogue; its body calls `FUN_823db988`, which I did not fully
  disassemble).
- Which code path emits `StartEmitter("fire")` on the canopy on X360. I only
  proved the string's existence and the dispatch table's seeding; the
  caller is unverified this run.
- RedEffectComponent's role — SPECULATIVE in the diff hypothesis.
- PC-side anchoring for FUN_0066ae30, FUN_0052bd60, FUN_0052c730 (the
  caller delegated the X360 side only).
- Whether PC's `FUN_0066ae30` reads Explosive `+0x20` (Detail) with the
  same pattern as X360 `FUN_82291440` line 229137. The matcher should
  check this once it has both halves.

## Opportunistic findings

- **SceneObject** class: X360 record base `0x83658074`, vtable `0x820e2b88`,
  instance size `0x1c`, flags `8`. Registrar at `FUN_823d3868` (line 449812,
  "SceneObject" @ 449835). Relevant because the RuntimeExplosion producer
  binds Explosive AND SceneObject into the same 0x40-byte template.
- **RedEffectComponent** class: X360 record base `0x8365513c`, vtable
  `0x820df468`, instance size `0x38`. Registrar at `FUN_823bbae0` (line
  431861, "RedEffectComponent" @ 431884). Appears in the Explosive producer
  as a secondary lookup.
- The hash seed `0x9e3779b9` (golden ratio) is set in every one of the 11
  class records I decoded on the way through — Explosive, Flammable,
  Ignitor, RuntimeExplosion, RuntimeIgnitor, RuntimeHealth,
  RuntimeNodeHealth, RuntimeProjectileThrown, RuntimeTimer,
  RuntimeAssetRef, RuntimeFlightNoise. Useful cross-platform scaffold:
  the exemap matcher can anchor "class record" pointers by scanning for the
  `0x100, 0x0, 0x100, 0x9e3779b9` quadword fingerprint.
- "StartEmitter" string VA on X360 retail: `0x820d6d30` (file offset
  `0xd6d30`). Hash under the Pandemic engine's string hasher: `0x3347914f`
  (preview-xenon:304977).
- Explosive-class RTTI constant: `0x820e3164` (returned by
  `FUN_823ff500`, Explosive vtable slot 13).
- RuntimeExplosion-class RTTI constant: `0x820e3ba8` (returned by
  `FUN_823c1d98`, RuntimeExplosion vtable slot 13).
- Flammable RTTI: `0x820e37cc`. Ignitor RTTI: `0x820e37d8`.
  RuntimeIgnitor RTTI: `0x820e3d24`. These are stable per-class
  identifiers useful for the matcher as byte-level anchors.
