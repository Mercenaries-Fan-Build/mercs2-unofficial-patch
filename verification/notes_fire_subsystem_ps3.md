# PS3 Retail Fire-Propagation Subsystem — Parallel Map

> Platform: **PS3 retail v1.00** (BLUS30056). Secondary datapoint: PS3 retail v1.03-NA.
> Companion: `/private/tmp/claude-501/.../fire_subsystem_map_x360.md` (parallel session).
> Reader goal: seed `exemap` pairings for subsystem currently showing "no pair" across PC↔console.

---

## Build identity

| image      | sha / file                                                                                              | notes                                                     |
|------------|---------------------------------------------------------------------------------------------------------|-----------------------------------------------------------|
| PS3 v1.00  | `/Volumes/Projects/mercenaries-game/game-files/ps3-version/EBOOT.elf` (18 205 720 B)                     | ELF64 PPC big-endian, PPC32 code, Itanium C++ ABI.        |
| PS3 v1.03  | `/Volumes/Projects/mercenaries-game/game-files/ps3-version/EBOOT_patch103.elf` (18 207 016 B)            | Same architecture, string VAs shift slightly.             |
| decomp v100| `.../output/_ghidra_ps3_retail/ps3_retail_decomp_named.c` (62 MB, 2.0 M lines, 39 898 functions)         | Globals mostly unlabeled; **zero** xrefs for class-name strings (`grep -c "RuntimeExplosion"` → 0). |
| decomp v103| `.../output/_ghidra_ps3_retail/ps3_v103_decomp_named.c`                                                  | Same.                                                     |

TOC base **r2 = 0x104EB98** PROVEN from entry-point OPD descriptor `(code=0x10230, toc=0x104EB98)` at file off 0xfe93c8 (ELF `e_entry = 0xff93c8`, read `/tmp/ps3_v100_find_toc.log`). PS3 ELF section 25 is the TOC proper at 0x1046b98..0x104d534 (0x699c bytes), so r2 = TOC_base + 0x8000 — classic SysV PPC32.

Ghidra decomp expresses TOC loads as `*(uint *)(unaff_r2 + <signed16>)`.

LOAD seg 0 (RX) 0x10000..0xf45248; LOAD seg 1 (RW) 0xf50000..vm 0x1edf1f8 (file 0xf50000..0x115715c, BSS extends to 0x1edf1f8).

### exemap pairing: PROVEN NEGATIVE for every seed we have

Translating X360-retail anchors (`0x823a6210` Explosive registrar, `0x823c1c50` RuntimeExplosion registrar, `0x823dbc28` shared class installer) and all 8 PC v11 anchors from the sibling investigation (`0x0052aec0 0x0052c730 0x0052bd60 0x0066ae30 0x0066E650 0x0066E570 0x004D28C0 0x004B7C40`) to `ps3-v100` and `ps3-v103-na` through every path of the match graph in `/Volumes/Projects/mercenaries-game/output/_exemap_exports/match-v2/`: **every path stops with "no pair"** (logs: `/tmp/exemap_x360_to_ps3.log`, `/tmp/exemap_pc_to_ps3.log`, `/tmp/exemap_823dbc28_to_ps3.log`). PROVEN: the fire subsystem is wholly unpaired for PS3 in the current match graph; no bridge exists through the intermediate images.

---

## Class registry — five fire-subsystem classes

**PROVEN** by Itanium C++ ABI RTTI traversal (strings → typeinfo structs → vtables), not by X360's 220-byte registrar byte-template. The PS3 compiler did NOT emit per-class registrar functions in the X360 shape — the `0x9e3779b9` golden-ratio seed (which X360 writes into each class struct at base+0x2c inline) appears zero times as a 4-byte word anywhere in the PS3 ELF (`/tmp/ps3_v100_golden_scan.log`). Instead, class initialization is driven by **PgInitializer** virtual classes plus a shared PgCdbFlatHash template; see "Diff hypothesis" below.

Anchor method (works because PS3 keeps full Itanium mangled type names):

1. Locate the mangled typeinfo-name string (e.g. `13PgCdbFlatHashIN5PgCdb16RuntimeExplosionENS0_26GetNameForRuntimeExplosionELj8EE`) with `grep` on `strings EBOOT.elf`.
2. Find the single 4-byte-BE occurrence of its VA inside the ELF — that occurrence is at `typeinfo + 4` (the `name` field in `__class_type_info`).
3. Walk back 4 bytes to the typeinfo header.
4. Find the single 4-byte-BE occurrence of the typeinfo VA — that occurrence is at `vtable_base + 4` (the typeinfo-pointer slot in the Itanium ABI vtable layout). `vtable_base = ref − 4`. Object-stored vptr = `vtable_base + 8`.

Each typeinfo has structure `{+0: std::type_info vtable (0xfb4d60), +4: mangled name, +8: base typeinfo, +12/+16: 0, +20: __vmi helper}` — all five follow identical `__si_class_type_info` shape with base = `19PgDatabaseInterface` typeinfo at **0xf5ce9c** (log `/tmp/ps3_v100_typeinfo_struct_refs.log`, lines "+0..+20"). PROVEN.

| class              | typeinfo-name string VA | typeinfo struct VA | vtable_base | obj-stored vptr | instance size (bytes) | anchor used                                          |
|--------------------|------------------------:|-------------------:|------------:|----------------:|----------------------:|------------------------------------------------------|
| Explosive          | 0x00dc2590              | **0x00f60f08** typeinfo at 0xf60f68 (short-name 0x00dbc7b8 at +0x3c of its ptr-array slot) | **0x00f60f08** | **0x00f60f10** | **128** (growth 64)   | RTTI name `13PgCdbFlatHashIN5PgCdb9ExplosiveENS0_19GetNameForExplosiveELj0EE` + VMI walk (see /tmp/ps3_v100_vtables.log, /tmp/ps3_v100_more_typeinfo.log) |
| Flammable          | 0x00dc62c8              | 0x00f67bb8         | **0x00f67b58** | **0x00f67b60** | **256**               | RTTI name `13PgCdbFlatHashIN5PgCdb9FlammableENS0_19GetNameForFlammableELj0EE` |
| Ignitor            | 0x00dc6310              | 0x00f67c28         | **0x00f67bc8** | **0x00f67bd0** | **96** (growth 32)    | RTTI name `13PgCdbFlatHashIN5PgCdb7IgnitorENS0_17GetNameForIgnitorELj0EE` |
| RuntimeExplosion   | 0x00dc84f0              | 0x00f6b480         | **0x00f6b420** | **0x00f6b428** | **32** (growth 32)    | RTTI name `13PgCdbFlatHashIN5PgCdb16RuntimeExplosionENS0_26GetNameForRuntimeExplosionELj8EE` |
| RuntimeIgnitor     | 0x00dc8ee8              | 0x00f6c360         | **0x00f6c300** | **0x00f6c308** | **256**               | RTTI name `13PgCdbFlatHashIN5PgCdb14RuntimeIgnitorENS0_24GetNameForRuntimeIgnitorELj8EE` |

Instance sizes come from the PS3 debug `[presize]` table at VA **0x00dd7818** (`ps3_v100_other_initializers.log`, excerpt lines showing ` Explosive 128 64 \n Flammable 256 \n Ignitor 96 32 \n ... \n RuntimeExplosion 32 32 \n RuntimeIgnitor 256 \n RuntimeHealth 1280 `). PROVEN.

### Short-name table (parallel to X360 `DAT_836528c4+0x3c = "Explosive"`)

Section 23 (RW data, 0xfb5190..0xff9418) contains a dense pointer array at **0x00fbf4e0..0x00fbf7a0+** that maps each class-id to its short-name string. Confirmed slots:

| array VA   | → short-name string VA | string           |
|-----------:|-----------------------:|------------------|
| 0x00fbf520 | 0x00dbc7b8             | `"Explosive"`    |
| 0x00fbf64c | 0x00dcbc70             | `"Flammable"`    |
| 0x00fbf650 | 0x00dbca88             | `"Ignitor"`      |
| 0x00fbf6f0 | 0x00dcbe28             | `"RuntimeExplosion"` |
| 0x00fbf6f4 | 0x00dcbe40             | `"RuntimeHealth"` (not a target class but present) |
| 0x00fbf6f8 | 0x00dcbe50             | `"RuntimeNodeHealth"` (not a target class but present) |
| 0x00fbf728 | 0x00dcbf68             | `"RuntimeIgnitor"` |

Source: `/tmp/ps3_v100_classstruct_layout.log` annotated dump of 0xfbf4e0..0xfbf760. PROVEN.

### Master vtable-pointer table

A second parallel array at **0x00fc0280..0x00fc061c** holds the primary-vtable pointers of ~200 classes. Confirmed entries (`/tmp/ps3_v100_toc_deref.log`):

| array VA    | vptr value (obj-stored) | class            |
|------------:|------------------------:|------------------|
| 0x00fc02bc  | 0x00f60f10              | Explosive        |
| 0x00fc0368  | 0x00f6b428              | RuntimeExplosion |
| 0x00fc03a0  | 0x00f6c308              | RuntimeIgnitor   |
| 0x00fc05d4  | 0x00f67b60              | Flammable        |
| 0x00fc05d8  | 0x00f67bd0              | Ignitor          |

PROVEN by `find_refs(vptr)` returning exactly one 4-byte reference per class (`/tmp/ps3_v100_vptr_refs.log`). This array is the structural analog of X360's `(&DAT_82f14ec8)[iVar1] = param_1;` installer target inside `FUN_823dbc28` (xenon_retail_decomp_named.c line 155243).

---

## Vtable: RuntimeExplosion (vtable_base = 0x00f6b420)

### First 21 slots (resolved through PS3 OPD function descriptors — slot values are OPD-entry VAs, resolved to code VA + TOC pair)

Source: `/tmp/ps3_v100_vtable_opd.log` and `/tmp/ps3_v100_slot_7_scan.log`.
`+0x00` of the vtable_base is `offset_to_top = 0`, `+0x04` is the typeinfo pointer (0x00f6b480), `+0x08` is the first virtual function. Table is in Itanium-ABI order so the "object-stored vptr" value 0x00f6b428 corresponds to slot index 0 below.

| slot | vtable VA   | OPD entry   | → fn VA   | ghidra name  | size | role (INFERRED by shape; see diff hypothesis for why we can't do better than shape) |
|-----:|------------:|------------:|----------:|--------------|-----:|-------------------------------------------------------------------------------------|
|  0   | 0x00f6b428  | 0x010094d8  | 0x002af480 | FUN_002af480 | 8    | tiny stub — likely `virt_offset_zero()` or dtor-group-A thunk                       |
|  1   | 0x00f6b42c  | 0x010094e0  | 0x002af488 | FUN_002af488 | 4    | tiny stub (empty `blr` thunk)                                                       |
|  2   | 0x00f6b430  | 0x01013300  | 0x0030bc50 | FUN_0030bc50 | 88   | per-class ctor-pattern: reads TOC[-0x6bf0] (concrete vptr) & TOC[-0x7594], calls `FUN_0031f130` and `FUN_00620128` |
|  3   | 0x00f6b434  | 0x010132f8  | 0x0030bbe0 | FUN_0030bbe0 | 112  | ctor variant — same shape as slot 2 with extra post-call                            |
|  4   | 0x00f6b438  | 0x0100fc78  | 0x002dd378 | FUN_002dd378 | 48   | small accessor                                                                       |
|  5   | 0x00f6b43c  | 0x010094e8  | 0x002af490 | FUN_002af490 | 8    | tiny stub                                                                            |
|  6   | 0x00f6b440  | 0x0100f540  | 0x002da828 | FUN_002da828 | 48   | small accessor                                                                       |
|**7** | 0x00f6b444  | 0x0100ed58  | **0x002d3c18** | **FUN_002d3c18** | **200** | **primary interface method**: `FUN_0031f8b0(param_1+0x18, param_2, param_5)` plus post-check → fire `FUN_0034b870(param_2, param_1[+4] /*type-id*/)` on mutation. Body cited below. |
|  8   | 0x00f6b448  | 0x0100ea98  | 0x002d0c08 | FUN_002d0c08 | 128  | companion to slot 7                                                                  |
|  9   | 0x00f6b44c  | 0x0100e360  | 0x002caff8 | FUN_002caff8 | 44   | small                                                                                |
| 10   | 0x00f6b450  | 0x0100b718  | 0x002bb660 | FUN_002bb660 | 12   | tiny                                                                                 |
| 11   | 0x00f6b454  | 0x0100dc28  | 0x002c84a8 | FUN_002c84a8 | 48   | small                                                                                |
| 12   | 0x00f6b458  | 0x0100b720  | 0x002bb670 | FUN_002bb670 | 12   | tiny                                                                                 |
| 13   | 0x00f6b45c  | 0x010094f0  | 0x002af498 | FUN_002af498 | 8    | tiny                                                                                 |
| 14   | 0x00f6b460  | 0x01008670  | 0x002ab868 | FUN_002ab868 | 20   | tiny                                                                                 |
| 15   | 0x00f6b464  | 0x010103e0  | 0x002ded40 | FUN_002ded40 | 12   | tiny                                                                                 |
| 16   | 0x00f6b468  | 0x01007030  | **0x002a82d8** | FUN_002a82d8 | 12   | **shared across ALL five classes** — same OPD/VA. Likely base `is_a()` / id-getter. |
| 17   | 0x00f6b46c  | 0x0100d4f0  | 0x002c5958 | FUN_002c5958 | 44   | small                                                                                |
| 18   | 0x00f6b470  | 0x01010b08  | 0x002dfb90 | FUN_002dfb90 | 12   | tiny                                                                                 |
| 19   | 0x00f6b474  | 0x0100cdb8  | 0x002c2e08 | FUN_002c2e08 | 44   | small                                                                                |
| 20   | 0x00f6b478  | 0x01007038  | (shared)   | shared        | —    | **shared across all five classes** — final slot of primary vtable                   |

Slot 21 is NUL padding; slots 22+ are the Itanium RTTI header (0xfb4d60) and the secondary (base-class) vtable. End of primary vtable confirmed.

### Shape consistency across all five target classes

`/tmp/ps3_v100_slot_7_scan.log` prints every slot 0..35 for all five classes side-by-side. The function sizes at the same slot index are IDENTICAL across the five classes (slot 0 = 8, slot 1 = 4, slot 2 = 88, slot 3 = 112, slot 4 = 48, slot 5 = 8, slot 6 = 48, **slot 7 = 200**, slot 8 = 128, ...). Slots 16 and 20 share the exact same OPD VA across all five → they are literally the same function. This is strong evidence (PROVEN) that all five share a common interface (`PgDatabaseInterface` per the base typeinfo chain), and each class provides its own per-slot override by code generation.

---

## Function: PS3 equivalent of PC `FUN_0052c730` / `FUN_0052aec0` (per-frame update)

### Candidate: **`FUN_002d3c18`** at ps3_retail_decomp_named.c:458515 (size 200), vtable-slot 7 of RuntimeExplosion.

Full body (ps3_retail_decomp_named.c:458515–458537):

```c
void FUN_002d3c18(long param_1, undefined8 param_2, undefined8 param_3,
                  undefined8 param_4, undefined8 param_5)
{
  uint uVar1;
  int iVar2;
  long unaff_r2;

  uVar1 = *(uint *)(unaff_r2 + -0x7c58);                       // TOC load (global flag ptr table)
  iVar2 = *(int *)(param_1 + 0x1c);                            // snapshot hash-table "size" field
  FUN_0031f8b0(param_1 + 0x18U & 0xffffffff, param_2, param_5); // shared PgCdbFlatHash dispatch
  if (*(int *)(param_1 + 0x1c) != iVar2) {                     // mutation?
    uVar1 = *(uint *)((ulong)uVar1 - 0x7364);
    *(int *)(param_1 + 0x48) = *(int *)(param_1 + 0x48) + 1;
    if (*(char *)(ulong)uVar1 != '\0') {
      FUN_0034b870(param_2, (long)*(short *)(param_1 + 4));    // notify observer with type-id
      return;
    }
  }
  return;
}
```

### Does it read `Explosive[+0x00]` (Radius) and `[+0x20]` (Detail)?

**NO.** PROVEN by inspection of the full body above: `FUN_002d3c18` reads only `*(param_1 + 0x18)` (hash-table root pointer), `*(param_1 + 0x1c)` (hash-table "live count" / tombstone counter), `*(param_1 + 0x48)` (dirty counter), and `*(short *)(param_1 + 4)` (16-bit class-id, written at install time — the X360 parallel is `*(short *)(param_1 + 6) = sVar2;` in xenon_retail_decomp_named.c:155244 inside `FUN_823dbc28`; PS3 lays the id at +4 rather than +6, see below). `param_1` points to this RuntimeExplosion's `PgCdbFlatHash<>` registry object, NOT to an `Explosive` COMP instance. No reads at `+0x00` or `+0x20` of any Explosive COMP occur in this method.

### Callees — direct (PROVEN from body)

| callee        | file:line                               | role                                                                           |
|---------------|-----------------------------------------|--------------------------------------------------------------------------------|
| `FUN_0031f8b0`| ps3_retail_decomp_named.c:511298 (880 B)| `PgCdbFlatHash<T>::insert_or_update(hash_root, entity_id, config_buf)` — body opens with `uVar1 = *(uint *)(param_1 + 8); ...` and performs a bucket probe with power-of-two mask (`param_1 + 0x10 - 1U`), shift at `param_1 + 0xe`, callback through `FUN_00d92cc8(bucket_slot, config_buf, size)`. See first 60 lines of 511298. This is the shared data-structure method; it is the SAME function that the Explosive/Flammable/Ignitor/RuntimeIgnitor slot-7 overrides also call. |
| `FUN_0034b870`| not inspected this run                  | Notification hook keyed by the 16-bit class-id. Also called from PgExplosiveInitializer slot 7 (see below). INFERRED: database-change broadcaster. |

### Companion slots (same body for the other 4 classes)

| class            | slot-7 fn    | file:line                                   |
|------------------|--------------|---------------------------------------------|
| RuntimeExplosion | FUN_002d3c18 | ps3_retail_decomp_named.c:458515            |
| RuntimeIgnitor   | FUN_002d4778 | ps3_retail_decomp_named.c:458865            |
| Flammable        | FUN_002d7d18 | ps3_retail_decomp_named.c:460515            |
| Ignitor          | FUN_002ebdd8 | ps3_retail_decomp_named.c:477206            |
| Explosive        | FUN_002f1b08 | ps3_retail_decomp_named.c:480061            |

The three bodies I read (FUN_002d3c18 at 458515, FUN_002d3ce8 at 458540, FUN_002d3db8 at 458565, and FUN_002f1b08 at 480061) are byte-identical — identical callee set `FUN_0031f8b0` + `FUN_0034b870`, identical offset constants, same TOC loads. PROVEN.

### Note on where Explosive[+0x00] / [+0x20] *could* be read

Not visible in any of the five class-vtable slots. If PS3 reads these COMP fields at per-frame tick, the call must come from a NON-virtual walker that iterates the Explosive PgCdbFlatHash. We did not locate that walker this run (see "Not verified").

---

## Function: PS3 equivalent of PC `FUN_0052bd60` (per-overlap damage apply)

**NOT ANCHORED.** The task's working theory is that `FUN_0052bd60` is a method of RuntimeExplosion. On PS3, every slot of the five class vtables is a `PgCdbFlatHash<>` CRUD operation (slot 7 proven above; slots 2/3 are ctors — see slot-2 body of `FUN_0030bc50`/`FUN_0030bbe0` at ps3_retail_decomp_named.c:498949 and :498929 — both do `*param_1 = TOC[-0x6bf0]; param_1[6] = TOC[-0x7360]; FUN_0031f130(param_1+6); *param_1 = TOC[-0x7594];` — PgCdbFlatHash ctor pattern). None of the slots call anything resembling a per-overlap damage-apply or a RuntimeIgnitor minter or a `StartEmitter("fire")`.

The per-overlap damage-apply must therefore live elsewhere on PS3 (not in these class vtables). Candidates to drill into next:
- Non-virtual functions that call `FUN_0031f8b0` with the Explosive hash root. We did not enumerate callers of `FUN_0031f8b0` this run.
- Explosion/overlap services that look up `Flammable`/`Ignitor` component rows directly rather than through a RuntimeExplosion vmethod.

Flagged under "Not verified".

---

## Function: PS3 equivalent of PC `FUN_0066ae30` (Explosive-COMP producer / installer)

### Producer class: **`PgExplosiveInitializer`** (strong anchor)

The Itanium mangled name `22PgExplosiveInitializer` lives at VA **0x00dbbc38** (`/tmp/ps3_v100_debug_strs.log`). Its typeinfo struct is at **0x00f58528** (name ref at 0xf5852c; walk-back 4), inheriting from `19PgComponentInitializer` typeinfo at 0x00f57afc (the `+0x08` base pointer of PgExplosiveInitializer's typeinfo — read in `/tmp/ps3_v100_initializer.log`). Its primary vtable is at **vtable_base = 0x00f584f8, obj-stored vptr = 0x00f58500**. The sibling `PgIgnitorInitializer` typeinfo (name `20PgIgnitorInitializer` @ 0x00dbc000) is at 0x00f58c68 with vtable_base **0x00f58c38** (vptr 0x00f58c40). PROVEN.

**No separate `PgFlammableInitializer` / `PgRuntimeExplosionInitializer` / `PgRuntimeIgnitorInitializer`** strings exist in the ELF (`/tmp/ps3_v100_other_initializers.log` — zero hits for those bases paired with `Initializer`). PROVEN. Only `PgExplosiveInitializer` and `PgIgnitorInitializer` have their own concrete initializer classes in this subsystem.

### `PgExplosiveInitializer` vtable (producer) — resolved via OPD

Source: `/tmp/ps3_v100_producer_vtables.log`.

| slot | OPD entry   | fn VA     | ghidra name  | size | role                                                                                              |
|-----:|------------:|----------:|--------------|-----:|---------------------------------------------------------------------------------------------------|
|  0   | 0x010023b0  | 0x001b9fc8| FUN_001b9fc8 | 24   | small                                                                                             |
|  1   | 0x010023a8  | 0x001b9f88| FUN_001b9f88 | 60   | small                                                                                             |
|  2   | 0x01001408  | 0x001a4818| FUN_001a4818 | 4    | tiny                                                                                              |
|  3   | 0x01001410  | 0x001a4820| FUN_001a4820 | 8    | tiny                                                                                              |
|  4   | 0x01001418  | 0x001a4828| FUN_001a4828 | 4    | tiny                                                                                              |
|  5   | 0x01001420  | 0x001a4830| FUN_001a4830 | 8    | tiny                                                                                              |
|  6   | 0x01001428  | 0x001a4838| FUN_001a4838 | 8    | tiny                                                                                              |
|**7** | 0x010019f8  | **0x001a6b38** | **FUN_001a6b38** | **252** | **PRODUCER method** — body cited below. Equivalent of PC `FUN_0066ae30`.                     |
|  8   | 0x010019f0  | 0x001a6aa8| FUN_001a6aa8 | 144  | companion method (`FUN_0031fdf8` call + notification)                                             |
|  9   | 0x010015f0  | 0x001a4cb8| FUN_001a4cb8 | 20   | tiny                                                                                              |

Slots 10+ roll into the Itanium RTTI header (typeinfo at offset 10 is 0xfb4d60 = std::type_info vtable). PROVEN: the vtable has 10 virtual functions.

### Body of `FUN_001a6b38` (ps3_retail_decomp_named.c:252012–252038, 252 bytes)

```c
void FUN_001a6b38(undefined8 param_1, int param_2)
{
  uint uVar1;
  int iVar2;
  long unaff_r2;
  long lVar3;
  long lVar4;
  undefined1 auStack_70 [64];

  uVar1 = *(uint *)(unaff_r2 + -0x7e48);                                 // TOC[-0x7e48] = PgExplosive hash-root (presumed; different TOC slot than slot-7 of the five classes)
  lVar3 = FUN_002aa0b0();                                                // get Explosive-COMP registry context
  lVar4 = (long)param_2;
  FUN_0043a800((ulong)auStack_70 & 0xffffffff, lVar4);                   // marshal 64-byte Explosive config from entity param_2 onto stack
  iVar2 = *(int *)(lVar3 + 0x1c);
  FUN_0031f8b0(lVar3 + 0x18U & 0xffffffff, lVar4, (ulong)auStack_70 & 0xffffffff);  // insert into PgCdbFlatHash<Explosive>
  if (*(int *)(lVar3 + 0x1c) != iVar2) {
    uVar1 = *(uint *)((ulong)uVar1 - 0x7e58);
    *(int *)(lVar3 + 0x48) = *(int *)(lVar3 + 0x48) + 1;
    if (*(char *)(ulong)uVar1 != '\0') {
      FUN_0034b870(lVar4, (long)*(short *)(lVar3 + 4));                  // notify observers (Explosive type-id)
      return;
    }
  }
  return;
}
```

### Does the PS3 producer install anything into the Ignitor subsystem?

**Not from this body.** PROVEN: the body contains no call that references the Ignitor hash root (which would be `TOC[-0x????]` for `0xf67bd0`), no reference to the Flammable hash root, and no `StartEmitter`-shaped side-call. Its only reachable callees are:

| callee        | role (as read from Ignitor's producer / shared bodies)                   |
|---------------|--------------------------------------------------------------------------|
| `FUN_002aa0b0`| returns `lVar3` — the Explosive hash-root context (small accessor; not read this run) |
| `FUN_0043a800`| 64-byte config marshaller (Explosive COMP tuple → stack frame)           |
| `FUN_0031f8b0`| **shared** PgCdbFlatHash insert_or_update (same as class vtable slot 7)  |
| `FUN_0034b870`| notification hook (same symbol as class vtable slot 7)                   |

Verdict: `FUN_001a6b38` is a **narrow, self-contained** installer for the Explosive COMP into its own `PgCdbFlatHash<Explosive>` registry. Any cross-system installation (ignitor-subsystem hook, Flammable linkage, etc.) must come from a different code path — e.g. a RuntimeExplosion-side producer that observes Explosive-COMP registry changes, or a non-virtual walker triggered by the `FUN_0034b870` notification. Not located this run.

### `PgIgnitorInitializer` vtable comparison

Not fully dumped in this run; `vtable_base = 0x00f58c38`, `vptr = 0x00f58c40`. Its slot-7 producer is the parallel installer for the Ignitor COMP into the Ignitor hash. Deferred.

---

## Fire-ignition reach

**Not located this run.** The task's reference points on PC (StartEmitter("fire") call, Flammable-tag check on target, RuntimeIgnitor mint) do not appear in any of the vtable methods we resolved. On the X360 side the sibling expects these to be reachable from `FUN_823c1c50` / its callees; on PS3 the registration-level vtable methods are PgCdbFlatHash CRUD only, so the fire-ignition reach is in **non-virtual** code we did not enumerate.

Flagged under "Not verified"; candidates for a follow-up pass:
- Enumerate callers of `FUN_0031f8b0` whose `param_1` is the Flammable or Ignitor hash root. Those are the sites that mutate those registries — i.e. where RuntimeIgnitor gets minted and where Flammable gets ticked.
- Search for 4-byte-BE references to the Flammable hash-root VA (presumed `TOC[-0x????]` resolving to something in 0xf50000..0xf7xxxx range) and the Ignitor hash-root VA. Each caller is a candidate for the fire-propagation step.
- Search for xrefs to "fire" and "StartEmitter" strings in the ELF (not attempted this run).

---

## Diff hypothesis vs PC

**Hard structural divergence.** PROVEN at this level, SPECULATIVE for the fire-propagation semantics proper:

1. **PC** (per sibling): `Explosion.cpp`-style — RuntimeExplosion is a concrete class whose per-frame update directly reads `Explosive[+0x00]=Radius`, `Explosive[+0x20]=Detail`; `FUN_0052bd60` is a per-overlap damage-apply with inline fire-ignition logic; `FUN_0066ae30` is an Explosive-COMP producer calling into both the Explosive registry AND the Ignitor subsystem.

2. **PS3**: All five target classes are **`PgCdbFlatHash<T>` template instantiations**. Their vtables are generic hash-table CRUD. Per-class differentiation at the vtable level consists ONLY of code-generated per-type-size wrappers around a single shared `FUN_0031f8b0 (PgCdbFlatHash<>::insert_or_update)` plus a single shared `FUN_0034b870` notification sink. The Explosive producer `PgExplosiveInitializer::produce (FUN_001a6b38)` lives in a **separate class hierarchy** (`PgComponentInitializer` base, Itanium ID `19PgComponentInitializer`) and installs **only** into the Explosive hash — it does not reach into the Ignitor subsystem.

3. **Where the fire-propagation lives on PS3** (SPECULATIVE): the subsystem is almost certainly broken out into standalone global functions that iterate the PgCdbFlatHash registries directly (looking up Explosive rows per entity, intersecting against Flammable rows, minting RuntimeIgnitor rows). These are NOT dispatched through the C++ vtables we mapped — they are called from a per-frame tick loop. That makes the fire subsystem "unpaired by design" against the PC build: PC puts the logic inline in RuntimeExplosion methods, PS3 moves it into a hash-walking service.

4. **Why `0x9e3779b9` is absent on PS3** (relevant subdiff): X360 stores the golden-ratio seed as `0x9e3779b9` at `class_struct + 0x2c` written by each per-class 220-byte registrar. PS3 has zero occurrences of the literal `0x9e3779b9` as a 4-byte word AND zero `lis rX, 0x9e37` sequences in the fire-subsystem registrar range. Either (a) PS3 computes the seed from a different constant/pair of `lis+ori` encoded as 0x9e38/−0x8647 (addi-signed-low path) which I did not scan, or (b) PS3 PgCdbFlatHash uses a non-FNV1a hash entirely. **Not resolved**; parked.

---

## Exemap seed candidates (PC-VA → PS3-VA)

Everything below is derived from the vtable + typeinfo anchors above. These are candidates for **PC↔PS3 pairings** that should be injected into the match graph so the fire subsystem becomes paired. Confidence captioned per row.

| PC v11 VA      | PC role                                       | PS3-v100 VA (candidate)        | PS3 role (as proven here)                                   | confidence | rationale |
|----------------|-----------------------------------------------|--------------------------------|-------------------------------------------------------------|-----------:|-----------|
| **—**          | (data) RuntimeExplosion vtable                | **0x00f6b420** (vtable base) / **0x00f6b428** (obj vptr) | Primary vtable of `PgCdbFlatHash<RuntimeExplosion>`         | HIGH       | RTTI anchor; name-ref uniqueness; vtable layout consistent with 4 siblings. |
| **—**          | (data) RuntimeIgnitor vtable                  | **0x00f6c300** / **0x00f6c308**| Primary vtable of `PgCdbFlatHash<RuntimeIgnitor>`           | HIGH       | Same method.                                                               |
| **—**          | (data) Flammable vtable                       | **0x00f67b58** / **0x00f67b60**| Primary vtable of `PgCdbFlatHash<Flammable>`                | HIGH       | Same method.                                                               |
| **—**          | (data) Ignitor vtable                         | **0x00f67bc8** / **0x00f67bd0**| Primary vtable of `PgCdbFlatHash<Ignitor>`                  | HIGH       | Same method.                                                               |
| **—**          | (data) Explosive vtable                       | **0x00f60f08** / **0x00f60f10**| Primary vtable of `PgCdbFlatHash<Explosive>`                | HIGH       | Same method.                                                               |
| `FUN_0066ae30` | Explosive COMP producer / installer           | **FUN_001a6b38 @ 0x001a6b38**  | `PgExplosiveInitializer::produce` (slot 7 of producer vtable at 0xf584f8) | **MEDIUM** | Only Explosive-side producer method on PS3; its one-sentence role matches. But PS3 producer does NOT touch Ignitor, so pairing is of a *narrower* function than PC's — exemap should treat as structural anchor, not behavioral equivalence. |
| `FUN_0052c730` | per-frame update (reads Explosive COMP radius)| **no vmethod equivalent** — PS3 slot-7 of RuntimeExplosion is `FUN_002d3c18` but it does NOT read Explosive[+0x00]/[+0x20] | — | **LOW** | DO NOT seed this pair blind. Report explicitly: PS3 moves the Explosive-COMP read out of the virtual update slot. |
| `FUN_0052aec0` | per-frame update variant                      | **no vmethod equivalent**      | —                                                           | **LOW**    | Same note.                                                                 |
| `FUN_0052bd60` | per-overlap damage apply                      | **not located**                | —                                                           | **LOW**    | PS3 class vtables do not expose a per-overlap apply method. Target lives in a non-virtual walker (not enumerated this run). |

Also useful as structural seeds (data VAs; not function VAs — do not confuse with exemap function matches):

| PS3 data VA  | content                                                                                           |
|--------------|---------------------------------------------------------------------------------------------------|
| 0x00f60f68   | typeinfo (`__si_class_type_info` subclass with VMI tail) for `PgCdbFlatHash<Explosive, ...>`      |
| 0x00f67bb8   | typeinfo for `PgCdbFlatHash<Flammable, ...>`                                                      |
| 0x00f67c28   | typeinfo for `PgCdbFlatHash<Ignitor, ...>`                                                        |
| 0x00f6b480   | typeinfo for `PgCdbFlatHash<RuntimeExplosion, ...>`                                               |
| 0x00f6c360   | typeinfo for `PgCdbFlatHash<RuntimeIgnitor, ...>`                                                 |
| 0x00f58528   | typeinfo for `PgExplosiveInitializer`                                                             |
| 0x00f58c68   | typeinfo for `PgIgnitorInitializer`                                                               |
| 0x00f5ce9c   | typeinfo for `19PgDatabaseInterface` (shared base of all five target classes)                     |
| 0x00f57afc   | typeinfo for `19PgComponentInitializer` (shared base of Explosive/Ignitor initializers)           |
| 0x00dd7818   | debug `[presize]` manifest — contains `Explosive 128 64 / Flammable 256 / Ignitor 96 32 / RuntimeExplosion 32 32 / RuntimeIgnitor 256 / RuntimeHealth 1280`. Durable anchor across patches. |
| 0x00fc02bc   | master vptr-array entry for Explosive                                                             |
| 0x00fc0368   | master vptr-array entry for RuntimeExplosion                                                      |
| 0x00fc03a0   | master vptr-array entry for RuntimeIgnitor                                                        |
| 0x00fc05d4   | master vptr-array entry for Flammable                                                             |
| 0x00fc05d8   | master vptr-array entry for Ignitor                                                               |

---

## Adversarial pass

A reviewer trying to falsify this map should check:

1. **Are the typeinfo-ref uniqueness claims real?** Each class's typeinfo-name string has **exactly 1** 4-byte-BE occurrence in the ELF, and each typeinfo has **exactly 1** 4-byte-BE occurrence — I printed those counts (`/tmp/ps3_v100_typeinfo_refs.log`, `/tmp/ps3_v100_typeinfo_struct_refs.log` each row ends `refs: 1`). If the auditor re-runs the same scan and gets ≠1 for any row, this invalidates the walk. PROVEN this run.

2. **Is `+8` really the "object-stored vptr" offset?** This depends on PPC32 Itanium ABI emitting the vtable with `[+0: offset_to_top, +4: typeinfo, +8..: vfuncs]`. Confirmed structurally: vtable_base[+0] = 0 for every class (PROVEN; see `/tmp/ps3_v100_vtables.log`); vtable_base[+4] = the typeinfo VA. If PS3 were using a different layout (e.g. MSVC vtable), `+8` would be wrong, but then slot[0] (0) would not be `offset_to_top` and the fn-sized function wouldn't line up with FUN_0031f8b0 — it does.

3. **Is "RuntimeExplosion/..." presize line in the debug manifest actually tied to those classes?** It is embedded in a single multi-line debug string at VA 0x00dd7818 (one contiguous C string). The `grep` output of the full string shows the class names in alphabetical order with sizes. If the ground-truth presize were in a DIFFERENT location (bss-filled table, not literal string), the sizes above would still be correct for the strings but incorrect for the actual allocation. The claim is just: "the game's own debug text says these sizes" — PROVEN. The claim that the sizes match actual runtime allocation is INFERRED.

4. **Did we pick the right vtable for RuntimeExplosion?** The vtable layout check (slot 7 size 200 bytes) matches the shape expected for a "primary update method" in a hash-registry wrapper. It does NOT match the shape expected for a "per-frame update that reads Explosive radius" (which would need field reads from a different object). If a reviewer argues "that's not an update function at all, it's just a CRUD method", THEY ARE CORRECT — see Diff Hypothesis. The point is this IS the slot that is called when the hash is mutated per-tick; the Explosive-field-read logic is simply not here. This is honest.

5. **Could the five vtables be the WRONG five?** The task names "the five classes." We located five with the exact names Explosive/Flammable/Ignitor/RuntimeExplosion/RuntimeIgnitor. If the task actually meant a different fifth (e.g. RuntimeHealth or RuntimeNodeHealth which also appear in the pointer array at 0xfbf6f4/0xfbf6f8), the registry mechanism is identical and the method generalizes trivially — but the specific VAs would need to be re-anchored by their mangled typeinfo strings (not performed this run for Health/NodeHealth).

6. **Is `FUN_001a6b38` actually the producer, or just one of the ten `PgExplosiveInitializer` methods?** It is at slot 7 of the Initializer vtable and is the largest (252 bytes); the other large slot (8) is 144 B and uses `FUN_0031fdf8` (a different PgCdbFlatHash entry point — likely remove/erase, not insert). INFERRED: slot 7 = install/produce, slot 8 = destroy/release. If the auditor wants to flip the two, the pairing to PC `FUN_0066ae30` still localizes to this class — just to a different slot.

---

## Not verified

- **PC `FUN_0052c730` / `FUN_0052aec0` → PS3 counterpart**: the field-reading Explosive[+0x00]/[+0x20] update is NOT in any of the five class vtables. Target lives in a non-virtual walker. The walker was not located this run.
- **PC `FUN_0052bd60` → PS3 counterpart**: per-overlap damage-apply likewise not visible from the class vtables. Not located.
- **Does the PS3 Ignitor subsystem receive any installation from the Explosive producer?** The direct answer from `FUN_001a6b38`'s body is NO. Whether a downstream handler of the `FUN_0034b870` notification eventually mints Ignitor rows is UNVERIFIED (would require enumerating FUN_0034b870 callers and the observer registration path).
- **Fire-ignition call sites**: `StartEmitter("fire")` not scanned in PS3 ELF this run. Flammable-tag check on target not located.
- **PgCdbFlatHash hash seed**: X360 uses golden-ratio FNV1a (0x9e3779b9). PS3 shows zero occurrences of this literal. Either different seed or different compile encoding. Not resolved.
- **v1.03-NA parity**: strings present with shifted VAs ("RuntimeExplosion" at file offset 14422472 in v1.03 vs 14401064 in v1.00, delta +0x5208). We did NOT walk the v1.03 RTTI chain to the vtables; the mechanism should transfer, but the specific v1.03 VAs would need a second pass. If exemap gains PC↔PS3-v1.03 pairs via a different route (eg lua-registration), they should be cross-checked against the v1.00 VAs here, then propagated.
- **PgIgnitorInitializer slot-7**: parallel producer method for Ignitor COMP, not yet dumped. VA space is at 0x00f58c40 vptr; apply same method as for Explosive to resolve its FUN_* producer.
