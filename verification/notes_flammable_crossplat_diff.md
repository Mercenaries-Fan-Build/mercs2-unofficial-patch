# Flammable + adjacent COMP cross-platform diff — PC v1.1 / X360 retail / PS3 retail

Scope: resident worldentity container (`0x5007_5B3B`, type `0x5647_C35D`) in
`blocks\VZ\resident_P000_Q3.block`. One run per wad; consoles were BE→LE converted
through `mercs2_formats::be_to_le::convert::convert_block` of just that one container.

Tool: `/Volumes/Projects/mercs2-wad-simulator/target/release/flammable_crossplat_dump`
(source: `/Volumes/Projects/mercs2-wad-simulator/crates/mercs2_probe/src/bin/flammable_crossplat_dump.rs`).
Built this run; cargo output: `scratchpad/build6.log`.

Inputs:
- PC v1.1: `/Volumes/Projects/mercenaries-game/game-files/pc-game-vz.wad` (FFCS LE).
- X360 retail: `/Volumes/Projects/mercenaries-game/game-files/xbox-vz.wad` (SCFF BE).
- PS3 retail: `/Volumes/Projects/mercenaries-game/game-files/ps3-VZ.WAD` (SCFF BE).

Raw dumps:
- `scratchpad/tri_default.txt` — full 3-way comparison, every record of
  `Flammable / Ignitor / Health`.
- `scratchpad/tri_targets.txt` — same comparison restricted to the foliage targets
  plus `Model / Phy2 / ScrubObject / HibernationControl / ModelName / ObjectScript /
  DestructionLink / PhysicalLink / Label`.
- `scratchpad/tri_targets_full.txt` — every PC COMP touching each of
  `treeplaza03 / bush01 / palmtreebend01 / largecanopy01` (canopy01-vs-palm diff).
- `scratchpad/tri_{pc,xbox,ps3}_worldentity_le.bin` — raw LE container bytes (consoles
  are post-convert; CSUM trailers kept).

---

## Step 1 — Flammable hash and record stride

PROVEN — `pandemic_hash_m2("Flammable") = 0xD930020E`.
Computed in `scratchpad/flammable_hash.txt`; the implementation matches the sibling's
established hashes (`texture=0xF011157A`, `model=0x5B724250`, `Explosive=0xF74044BA`).

PROVEN — on-disk authored `Flammable` payload stride = **4 bytes** on all three
platforms:

| Platform | COMP shape         | info body (hex)                                 | schm present | records | data_body size |
|----------|--------------------|-------------------------------------------------|--------------|--------:|---------------:|
| PC v1.1  | ASCII `info`       | `"Flammable\0" 0E0230D9 50000000 04000000 00000000` | yes          |       4 |            672 |
| X360     | compact 16B `info` | `0E0230D9 50000000 04000000 00000000`           | **no**       |       4 |            672 |
| PS3      | compact 16B `info` | `0E0230D9 50000000 04000000 00000000`           | **no**       |       4 |            672 |

Decoded info fields (same on all three):
- `class_hash = 0xD930020E` (= `pandemic_hash_m2("Flammable")`).
- `version = 80` (0x50).
- `count = 4` (4 grouped records).
- `flags = 0`.

PC schm (one field): `type=Int @byte=0 bit_start=0 bit_width=0 name_hash=0x8B50DC56`.
Payload stride = **4 bytes**. Record size arithmetic independently
verifies the console stride: 4 records × 4 B n_keys + 160 B keys + 4 records × stride
= 16 + 640 + 4·stride = 672 ⇒ stride = 4. Matches.

Reconciling the sibling's "PS3 presize=256" note (SPECULATIVE on my side): 256
appears to be the engine's **runtime C++ Flammable object size** (vptr + members),
not the authored record stride; those are two different numbers and only the
authored stride is relevant for a wad-level diff. I did NOT verify the 256 runtime
figure this run — the engine disassembly for the Flammable ctor was out of scope.

Confidence: PROVEN for the authored stride (3 independent wads, arithmetic match).

---

## Step 2–3 — Owner table per platform (Flammable)

Four Flammable records cover 160 owners (sum of their key lists); every record's
payload is exactly `00 00 00 00`. The 4 record payloads are byte-identical across
the three platforms. Full key-by-key tables are in `scratchpad/tri_default.txt`
(the `[Flammable]` section, lines 406–577); here are the four record groupings on
PC (identical keys on console):

- Record A — 43 keys, all `_scrub_*` scrub-tree templates (`_scrub_global_palmtreebend01..05`,
  `_scrub_jungle_treetall01..03`, `_scrub_env_largecanopy01`, `_scrub_global_treeplaza03`,
  `_scrub_global_bananna`, …). Payload: `00 00 00 00`.
- Record B — 32 keys, all `__*` direct world entities (`__jungle_env_largecanopy01` =
  `0x800089CA`, `__global_env_palmtreebend01..05`, `__global_env_treeplaza03`,
  `__jungle_env_smallcanopy01`, `__jungle_env_treetall01..03`, …). Payload: `00 00 00 00`.
- Record C — 45 keys, bushes/plants/hedges (`_global_env_bush01` = `0x800064C6`,
  `_global_env_bushlarge01`, every hedge, every merida plaza shrub, every jungle_env_bush*
  …). Payload: `00 00 00 00`.
- Record D — 40 keys, tree placeables (`_jungle_env_largecanopy01` = `0x80006D04`,
  `_global_env_palmtreebend01..05` = `0x800065F2..` , `_global_env_treeplaza03` =
  `0x80004DAB`, `_jungle_env_treemedium01..03`, `_shanty_env_tree01..03`, …).
  Payload: `00 00 00 00`.

Flammable's lone field (`name_hash=0x8B50DC56`, type Int) is **0 for every tree,
every bush, every hedge, every palm — in every platform**. The field value does
not vary across any authored foliage entity.

Interpretation (INFERRED): Flammable is a flag-component. The engine cares only
that an entity *has* a Flammable record (so it ends up in the Flammable `flgs`
bit); the 4-byte payload holds no per-entity tuning. This matches the sibling's
earlier finding that the X360 Flammable class instance is 4 bytes.

---

## Step 4 — Three-platform byte comparison for the specific foliage targets

(Payload hex, same for all four columns per class → row is byte-identical.)

### Flammable

| Owner handle | Name                                      | PC          | X360        | PS3         |
|--------------|-------------------------------------------|-------------|-------------|-------------|
| `0x80006D04` | `_jungle_env_largecanopy01`               | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800089CA` | `__jungle_env_largecanopy01`              | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x8000A176` | `_jungle_env_largecanopy01_standalone`    | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x80008F84` | `_scrub_env_largecanopy01`                | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800089BE` | `_jungle_env_smallcanopy01`               | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800089BC` | `__jungle_env_smallcanopy01`              | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800065F2` | `_global_env_palmtreebend01`              | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800089BF` | `__global_env_palmtreebend01`             | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x80004DAB` | `_global_env_treeplaza03`                 | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800089C8` | `__global_env_treeplaza03`                | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x800064C6` | `_global_env_bush01`                      | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |
| `0x80006921` | `_global_env_bushlarge01`                 | `00 00 00 00` | `00 00 00 00` | `00 00 00 00` |

Verdict for Flammable: **cross-platform byte-identical for every foliage target
and in fact for every one of the 160 Flammable owners in the container**. See
`scratchpad/tri_default.txt` line 577: `summary: 160 record(s) checked, 0 differing
byte-wise`.

### Ignitor  (stride 12; fields f32 LifetimeOrStrength / f32 Radius / f32 Damage per name hashes 0x354694DB / 0x1AB984F4 / 0xCBDCD01A — field *names* not resolved this run; only the hashes are PROVEN)

**None of the foliage targets have an Ignitor record** on any of the three platforms.
Ignitor records exist only on 68 fire-emitter / particle entities
(`fx_EmitFlame*`, `global_particle_fire*`, `Fire Damage (Medium Canopy)` =
`0x8000876C`, `global_particle_firetreecanopymedium` = `0x8000954C`,
`global_particle_firetreecanopysmall` = `0x8000954E`,
`global_particle_firetreetrunkA..D`, …). **All 68 Ignitor payloads are byte-identical
across PC / X360 / PS3** (`scratchpad/tri_default.txt` line 665: `summary: 68
record(s) checked, 0 differing byte-wise`).

### Health  (stride 8; fields `0xD122919D` f32 HP, three 1-bit flags at byte 4 — hashes `0x8CA0FD72 / 0x4299D698 / 0xE648DE48`)

| Owner handle | Name                                      | PC                        | X360                      | PS3                       | HP (f32) |
|--------------|-------------------------------------------|---------------------------|---------------------------|---------------------------|---------:|
| `0x80006D04` | `_jungle_env_largecanopy01`               | `00 00 7a 45 00 00 00 00` | `00 00 7a 45 00 00 00 00` | `00 00 7a 45 00 00 00 00` |     4000 |
| `0x8000A176` | `_jungle_env_largecanopy01_standalone`    | `00 00 7a 45 00 00 00 00` | `00 00 7a 45 00 00 00 00` | `00 00 7a 45 00 00 00 00` |     4000 |
| `0x800089BE` | `_jungle_env_smallcanopy01`               | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` |      300 |
| `0x800065F2` | `_global_env_palmtreebend01`              | `00 00 c8 42 00 00 00 00` | `00 00 c8 42 00 00 00 00` | `00 00 c8 42 00 00 00 00` |      100 |
| `0x80004DAB` | `_global_env_treeplaza03`                 | `00 00 c8 42 00 00 00 00` | `00 00 c8 42 00 00 00 00` | `00 00 c8 42 00 00 00 00` |      100 |
| `0x800064C6` | `_global_env_bush01`                      | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` |      300 |
| `0x80006921` | `_global_env_bushlarge01`                 | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` | `00 00 96 43 00 00 00 00` |      300 |

Verdict for Health: **byte-identical cross-platform for every foliage target.**

The 2727-record `Health` sweep does report 184 byte-level diffs between PC and
consoles — but **none of those 184 are foliage**. Every one of the 184 shows the
pattern `PC …XX 00 00 00` vs `console …00 00 00 XX` in the byte-4-7 word; those
are the three 1-bit flag fields (`bit_start = 0 / 1 / 2`, 4-byte Int unit). In
BE storage with MSB-numbered bit fields the engine reads the same semantic bit
(bit N from the MSB) that PC reads as bit N from the LSB, so after a plain
byte-reverse the stored byte position moves but the engine's interpretation does
not. This is a **BE↔LE bit-numbering convention artifact, not an authoring
difference**. SPECULATIVE on exactly which bit is which flag; PROVEN that the
foliage targets have all three flags clear on all three platforms (bytes 4-7
are `00 00 00 00` everywhere).

---

## Step 5 — adjacent COMPs on the same foliage targets, three-way

Per-target full dumps are in `scratchpad/tri_targets_full.txt`. Three-way byte
diffs for the foliage targets across these classes (restricted to the 4 primary
owners `0x80006D04 / 0x800065F2 / 0x800064C6 / 0x80004DAB`):

| Class                | PC/X360/PS3 for foliage targets |
|----------------------|---------------------------------|
| Flammable (4 B)      | **identical** |
| Ignitor (12 B)       | foliage NOT in group on any platform (consistent) |
| Health (8 B)         | **identical** |
| HibernationControl   | identical on records present on consoles — but PC has 4219 explicit records and consoles have only 63; the ones that overlap are byte-identical. The gap is **not** per-entity authoring, it is the UNIQ-pass default: `CHDR flags bit 0` causes the engine to synthesize a default `HibernationControl` for every key without one, so the consoles rely on that synthesizer while PC authored explicit copies (SPECULATIVE on exactly which copies PC authored vs. the synthesizer; PROVEN that the records that do appear in all three platforms agree byte-for-byte for the foliage targets, see `scratchpad/tri_targets.txt` lines 538–544) |
| ModelName (4 B)      | identical — canopy01 = `3b bb 7a ff` = `0xFF7ABB3B` (matches the known model hash); palm = `0x0EDFAFFF`; treeplaza03 = `0x156B1738`; bush01 = `0xE43A561C` |
| ObjectScript (8 B)   | identical — canopy01 = `61 c9 6f bb 00 00 00 00` (script hash `0xBB6FC961`); palm = `ed b6 c6 2d 00 00 00 00` (`0x2DC6B6ED`); treeplaza03 = `e5 fa 6d e1 00 00 00 00` (`0xE16DFAE5`); bush01 has none |
| ScrubObject (4 B)    | none of the primary foliage world-entity owners are in this group; identical (17 records cover `_scrub_*` templates, byte-identical) |
| PhysicalLink (44 B)  | none of the primary foliage owners appear in the 694 PhysicalLink records |
| Label (4 B)          | **PC-only authored class.** `0x06DA8775 = pandemic_hash_m2("Label")` does not appear as a COMP hash in either console container's compact-info bodies, so `parse_comp_groups` sees no Label group on X360/PS3. Every foliage target on PC has Label payload `6d 6d 24 71` = hash `0x71246D6D`. SPECULATIVE on whether consoles carry the Label flag elsewhere (e.g. in `flgs`-only form with no `data`); not investigated this run. For our question (burn visibility) Label is unrelated. |

No `DestructionLink / DestructMachine / FireState / FireFSM / FireVFX /
Destructible / Breakable / Flame / Fire / Burn / Combustible / FireProps /
Ignition / Fuel / RuntimeIgnitor` COMP class is present in the container on any
platform (I searched by hash — their component groups are absent from
`parse_comp_groups`'s output). PROVEN by absence of their hashes in
`scratchpad/tri_default.txt`'s COMP walk. So the destruction FSM is not an
authored COMP in `resident_P000_Q3.block`; it is driven by the per-entity
`ObjectScript` (Lua) and by DebrisEffect records that reference particle
templates whose own `Ignitor` records are byte-identical cross-platform.

---

## Step 6 — PC-only, canopy01 vs palm

Full PC-only dumps of every COMP touching each handle are in
`scratchpad/tri_targets_full.txt`. The entity-level differences that matter:

| Component             | `_jungle_env_largecanopy01` (0x80006D04) | `_global_env_palmtreebend01` (0x800065F2) |
|-----------------------|--------------------------------------------------------|---------------------------------------------------|
| Flammable             | `00 00 00 00` (identical)                              | `00 00 00 00` (identical)                         |
| Health                | HP = **4000** (`00 00 7a 45 00 00 00 00`)              | HP = **100** (`00 00 c8 42 00 00 00 00`)          |
| HibernationControl    | `d0 07 a0 3c 14 00` (same as palm)                     | `d0 07 a0 3c 14 00`                               |
| ModelName             | `0xFF7ABB3B`                                           | `0x0EDFAFFF`                                      |
| ObjectScript          | **`0xBB6FC961`**                                       | **`0x2DC6B6ED`**  ← different Lua chunks          |
| OSMParameter          | **1 record** `(0x31EB7EA2 → 0x5F75A3D7)`               | **2 records** (both `0x31EB7EA2`, destinations `0x53FE886A` and `0x5280B8DA → 0x53FE886A`) |
| ParticleKey           | **1 record** `(0x501D5FD7 → 0xB60B3805)`               | **2 records** (`0x1177AAD5 → 0x15647409` and `0x501D5FD7 → 0xB60B3805`) |
| ModifierKey           | 2 records                                              | 3 records                                         |
| BoneCtrlWind          | **2 records** (two sub-trees)                          | 1 record                                          |
| DebrisEffect          | **2 records** (`0xC73E56E4` → effect `0x800076D4` and `0x58CE29AF` → effect `0x800076D4`) | **0 records** |
| PhysicsPropertyCrashable | all zeros                                          | all zeros                                          |
| NodeHealth 2nd field (`0x27C50F84`) | **1.0**                                   | **0.0**                                           |
| CoverHintOffset       | present                                                | present                                           |
| Disable3DDecals       | present                                                | present                                           |

The verdicts, in order of relevance to the "canopy01 doesn't visibly ignite"
question:

1. **The per-entity Lua chunk is different.** canopy01's ObjectScript hash is
   `0xBB6FC961`; palm's is `0x2DC6B6ED`. These resolve to two different scripts
   that the engine's `script_host` runs on destruction / damage. INFERRED:
   whichever script the palm runs is the one that spawns a fire particle emitter
   (which carries the `Ignitor`); canopy01's script does not do the equivalent
   spawn, which is why the material animation plays (charred stump mesh swap in
   the model's own LOD/destruction path) while no `global_particle_firetreecanopy*`
   emitter appears. I did NOT disassemble either Lua chunk this run — the chunk
   bodies live under `blocks\\VZ\\scripts_P000.block` and would need the
   `lua_chunk_scan` or `lua_structural_dump` probes to crack.

2. **canopy01 has no `ParticleKey` for a fire source.** Palm's `ParticleKey
   (0x1177AAD5 → 0x15647409)` is the entry that bone-attaches a particle; this
   record is absent on canopy01. PROVEN by its absence in the PC canopy01 dump.
   Whether that specific ParticleKey is the one Lua spawns on burn is INFERRED.

3. **DebrisEffect targets canopy01 only — it points at particle template
   `0x800076D4`, not any of the known `global_particle_firetreecanopy*` templates.**
   SPECULATIVE on whether `0x800076D4` is a wood-chunk burst (no fire) vs. a
   burning debris trail; not resolved this run.

4. **Health = 4000 vs 100.** Not a gate for visuals in itself (healing to zero
   just triggers destruction) but affects what the Lua chunk sees on hit.

5. The `OSMParameter` and `ModifierKey` deltas: entries index into the OSM
   (Observable State Machine) table and the ModifierKey table. SPECULATIVE on
   whether these differ in a way that selects a different particle attachment;
   not resolved this run.

The "gate" between palm (ignites visibly) and canopy01 (visual flame missing,
charred stump still appears) is **not** the authored Flammable record and **not**
the Health record — those are identical in shape and consistent in treatment. It
is downstream, in the entity's own ObjectScript (Lua chunk `0xBB6FC961`) and in
the particle-attachment keys (ParticleKey / OSMParameter / DebrisEffect) it uses.

---

## Adversarial pass

- Could Flammable's "cross-platform identical" conclusion be an artifact of my
  BE→LE converter zeroing non-trivial bytes? Falsifiable: the Flammable payload
  on all three wads is `00 00 00 00`. Byte-reverse of 0 is 0 — identity is
  trivially preserved. **But**: if the pre-swap BE storage was non-zero, I would
  be hiding it. Checked: the X360 LE container at `tri_xbox_worldentity_le.bin`
  carries the Flammable `data` body `0x80000003 ... (160 keys interspersed) ...`
  with every `payload_stride=4` region reading `00 00 00 00`. The console-side
  "Health diffs" show that the converter DOES propagate non-zero bytes — if
  Flammable held non-zero data, the diff would surface. So the "all zeros" is
  truly in the on-disk authored data, not a lossy artifact.
- Could the 160-record identity be a false equivalence because my iter_records
  walks the wrong stride on consoles? Independent arithmetic check:
  `data_body = 672 B` on all three platforms (console size proved by direct
  byte read; see `scratchpad/scan_xbox.txt`). 4 record headers + 160 keys +
  4 × stride = 672 ⇒ stride = 4 ⇒ matches PC schm.
- Could `parse_comp_groups` be missing a second `Flammable` group? Searched by
  hash across all 589 X360 descriptors (`scratchpad/scan_xbox.txt`): exactly
  one COMP#212 with info-hash `0x0E0230D9` (= `0xD930020E` LE). One group.
- Could the Lua-chunk hashes I report (`0xBB6FC961` / `0x2DC6B6ED` /
  `0xE16DFAE5`) actually be the SAME hash with the console having byte-reversed
  it? Falsifiable: the ObjectScript record for canopy01 is byte-identical across
  PC/X360/PS3 in `scratchpad/tri_targets.txt` lines 583–586. So the hash on
  consoles is exactly what PC authors. The hashes themselves are different
  BETWEEN canopy01 and palm, not between platforms.
- Could the 184 Health diffs include foliage I missed? Grepped the diffs file
  for `canopy / palmtree / treeplaza / treetrunk / env_bush / env_treeoak /
  env_tree01 / smallcanopy / largecanopy`: 0 hits (`scratchpad/tri_foliage_diffs.txt`
  is empty).
- Does the PS3 "Flammable=256" presize hint imply a bigger authored stride I
  overlooked? No — 672 B of console `data` body with 160 keys would be
  impossible with stride=256 (needs 160·256 = 40960 B just for payloads). The
  256 figure refers to a different engine-side object.

---

## What I did NOT verify

- The decoded names of several non-trivial field hashes. Only the hashes are
  PROVEN; the string names (e.g. the three Health flag bits `0x8CA0FD72 /
  0x4299D698 / 0xE648DE48`, Ignitor's `0x354694DB / 0x1AB984F4 / 0xCBDCD01A`,
  Flammable's single field `0x8B50DC56`) were not cracked this run. I did not
  consult `pandemic_hash` rainbow tables in the sibling.
- The PS3 Flammable vtable address `0x00f67b58` and the X360 vtable `0x820df328`:
  not touched this run. The sibling's claim that the engine `Flammable` class
  instance is 4 bytes on X360 matches the authored stride, so it is consistent
  but I did not re-derive it from the exe.
- The Lua chunks `0xBB6FC961` (canopy01) and `0x2DC6B6ED` (palm): not
  disassembled. The hypothesis "canopy01's script does not spawn a fire-emitting
  particle" is INFERRED from the ParticleKey / DebrisEffect differences in the
  authored container, not confirmed from the script bytecode.
- The `0x800076D4` particle template referenced by canopy01's `DebrisEffect`:
  not resolved — the Debris chain may or may not spawn an Ignitor-bearing child.
- Whether consoles emit a `Label` COMP group somewhere else (not under its hash
  `0x06DA8775`): the group did not appear in `parse_comp_groups` output, so for
  consoles I report "NOT PRESENT" with the caveat that no deeper search was done
  this run.
- The 184 Health "diffs" I classified as a BE↔LE bit-packing-convention artifact:
  I did not spot-check more than a few examples against the live engine's
  runtime bit read. The classification is INFERRED from the uniform byte
  pattern (every diff puts the non-zero byte at position 7 on consoles vs.
  position 4 on PC with values restricted to `{0x20, 0x80, 0xA0, 0xC0}` →
  bit_start values ∈ {0, 1, 2} in the LSB numbering). A pedantic verification
  would read a few of those entities' Health flags from a live x64dbg session;
  I did not do that.
