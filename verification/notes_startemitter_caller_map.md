# StartEmitter("fire") caller chain — cross-platform

> Target: locate every C/C++ call site that reaches `StartEmitter("fire")` on
> X360 and PS3, cross-reference to PC, and compare gating.
>
> **Headline (PROVEN):** there is **no** C/C++ call site that passes the
> literal `"fire"` to `StartEmitter` on any of the three platforms. `"fire"`
> is a *runtime* string / name-hash, delivered to a generic
> `StartEmitter(entity, name_hash, flag)` native handler by one of three
> callers — a Lua binding, an FSM-state inline-command stub, or a per-entity
> iterator that reads a per-row "fx-on-destroy" name from the entity's
> template. The native handler is content-agnostic; it emits whatever name
> hash it's given. The ignition differentiator **cannot** live in this call
> site. See **Verdict** below.

The old prior-note claim that PC `FUN_004D28C0` is "the SWIT-level
`StartEmitter("fire")` call site" is **unsupported** by the evidence this
run turned up (see **PC** section).

---

## Working images

| image                | path                                                                                                | note                                                        |
|----------------------|-----------------------------------------------------------------------------------------------------|-------------------------------------------------------------|
| X360 retail (image)  | `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail/default.pe_ghidra.bin` (26 607 616 B) | image base `0x82000000`; big-endian PPC.                    |
| X360 retail decomp   | `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail_xenon/xenon_retail_decomp_named.c`   | 44 MB, retail xex.                                          |
| X360 preview-profile | `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_preview_xenon/xenon_decomp_named.c`         | strings resolved (hash→name map seeded as string literals). |
| PS3 retail v1.00     | `/Volumes/Projects/mercenaries-game/output/_ghidra_ps3_retail/ps3_retail_decomp_named.c`            | 62 MB, 2.0 M lines.                                         |
| PC exe               | `/Volumes/Projects/mercenaries-game/output/Mercenaries2.exe` (17 122 568 B)                         | image base `0x400000`, 12 sections including `Stext` SecuROM. |
| PC fnsig / match graph | `/Volumes/Projects/mercenaries-game/output/_exemap_exports/{pc-v10,pc-v11}.fnsig.json`, `match-v2/` | signature DB does not list every tiny function.             |

Hashes mentioned below are **4-byte Pandemic string hashes** as they appear
in the shipping constants. The hashing algorithm itself was not reversed
this run; the mapping of `hash → "name"` is PROVEN only where the X360
preview-profile reverse-registry writes the literal `"name"` into the
debug-side slot keyed by that hash (line 304977-304978 of the preview-xenon
decomp).

---

## X360

### Dispatch tables (seeded at process init, in the same registrar function)

The retail-xenon dispatch registrar body spans
`xenon_retail_decomp_named.c:162280-162418`. It seeds **two parallel command
tables** keyed by the Pandemic hashes `SendDamage`, `SetState`,
`StartEmitter`, ... :

| table base    | role                                              | StartEmitter slot handler | binding line |
|---------------|---------------------------------------------------|--------------------------|--------------|
| `0x83567f88`  | **execution** FSM command dispatch (OnEnter runs) | `LAB_8222fa30`           | 162298       |
| `0x83568190`  | undo/stop sibling table — StartEmitter is bound to the shared no-op `LAB_82232430` | `LAB_82232430` | 162369 |

Each binding is a two-line pair in the registrar:
```c
local_20[0] = (code *)&LAB_8222fa30;                                   // 162297
FUN_82232738(0xffffffff83567f88, 0x3347914f, local_20);                // 162298
```
`FUN_82232738` body (`@82232738`, size 96, decomp line 164801) inserts a
single `int` into `base[2+bucket]`, where `bucket = FUN_82855900(hash, base+0x108, 0x40)`
(open-addressed hash table, 64 slots). PROVEN.

The matching lookup is `FUN_82232870` (`@82232870`, size 104, line 164859):
```c
bool FUN_82232870(int base, undefined4 *out, undefined8 hash) {
  int bucket = FUN_82855778(hash, base + 0x108, 0x40);
  if (bucket >= 0) *out = *(undefined4 *)((bucket + 2) * 4 + base);
  return bucket >= 0;
}
```
`FUN_82232870` is called only from `FUN_8222d618@162297` (data lookup, not a
call) and `FUN_8223def0@173347` (flag-value lookup) — i.e. in retail-xenon
the **generic `.find`** of this table is not called in a function-pointer
`invoke` form. The actual dispatch per-command is inlined into the FSM
step-runner elsewhere. For the purposes of this map that detail does not
change the chain: the slot stored for hash `0x3347914f` in table
`0x83567f88` is `LAB_8222fa30`, and `LAB_8222fa30` is reached when the FSM
command runner iterates a state's inline commands and lands on a
`StartEmitter` opcode.

### Hash confirmation

`hash("StartEmitter") = 0x3347914f` — PROVEN cross-table. Three independent
registrars reference this same hash:

| decomp                              | line  | role                                                      |
|-------------------------------------|-------|-----------------------------------------------------------|
| `_ghidra_x360_preview_xenon`        | 304769| execution-table seed `0x836db3f8`, handler `LAB_8231a7f8` |
| `_ghidra_x360_preview_xenon`        | 304874| sibling-table seed `0x836db600`, handler `LAB_82315b38`   |
| `_ghidra_x360_preview_xenon`        | 304977| **reverse-registry** seed `0x836db808`, value = string `"StartEmitter"` — this is the single site that proves the hash↔name mapping |
| `_ghidra_x360_retail_xenon`         | 162298| execution-table seed `0x83567f88`, handler `LAB_8222fa30` |
| `_ghidra_x360_retail_xenon`         | 162368| sibling-table seed `0x83568190`, handler `LAB_82232430`   |

The hash algorithm itself was not reversed. Common FNV-1a / djb2 / jenkins
seeds do not reproduce `0x3347914f` for `"StartEmitter"` (log:
`startemitter_fire_caller_map/hash_investigate.log` + `hash_investigate2.log`).
PROVEN NEGATIVE for the standard families; the hasher is Pandemic-internal.
This means we **can't** compute `hash("fire")` from first principles this
run — we would need to dump the hash function body first. **NOT VERIFIED**
this run.

### String "StartEmitter" byte location

VA `0x820d6d30` (file offset `0xd6d30`) in `default.pe_ghidra.bin`. PROVEN
from prior sibling. This string is **only referenced by the debug reverse
registry at preview-xenon:304977-304978** — i.e. it is the human-readable
"name" used when the engine formats a dispatch failure. Retail-xenon
contains the string but no code reference to it.

### Dispatch stub `LAB_8222fa30` — PPC disassembly

56 bytes at `0x8222fa30`-`0x8222fa64`, dumped from the raw image
(log: `scratchpad/x360_8222fa68_disasm.log`, which actually starts from
0x8222fa30):

```
0x8222fa30  7c6a1b78   mr   r10, r3           ; r10 = ctx (FSM state inline-arg block)
0x8222fa34  7c832378   mr   r3,  r4           ; r3  = entity id (shift arg2 to arg1)
0x8222fa38  816a0004   lwz  r11, 4(r10)       ; r11 = ctx[+4] (opcode-cursor word count)
0x8222fa3c  396bffff   addi r11, r11, -1
0x8222fa40  916a0004   stw  r11, 4(r10)       ; cursor -= 1
0x8222fa44  392b0002   addi r9,  r11, 2
0x8222fa48  396bffff   addi r11, r11, -1
0x8222fa4c  5528103a   slwi r8,  r9, 2
0x8222fa50  38eb0002   addi r7,  r11, 2
0x8222fa54  54e6103a   slwi r6,  r7, 2
0x8222fa58  7c88502e   lwzx r4,  r8, r10      ; r4 = ctx[cursor]   (arg1: emitter name hash — "fire")
0x8222fa5c  916a0004   stw  r11, 4(r10)       ; cursor -= 1 (again)
0x8222fa60  7ca6502e   lwzx r5,  r6, r10      ; r5 = ctx[cursor-1] (arg2: flag/style)
0x8222fa64  48000004   b    .+4               ; fall through to the next function (FUN_8222fa68)
```

The `b .+4` lands exactly on `0x8222fa68`, which `/output/_ghidra_x360_retail/func_starts.txt` lists as a function entry (file line 2245). So **`LAB_8222fa30` is a parameter-shuffling tail-call stub into `FUN_8222fa68`**.

### Real StartEmitter handler — `FUN_8222fa68` @ VA `0x8222fa68`

PROVEN. Decomp lines 162945-163089, 724 bytes. Signature as inferred
`(entity_id, emitter_name_hash, flag)`:

| line  | reads / writes                                                             | role                                           |
|-------|----------------------------------------------------------------------------|------------------------------------------------|
| 162992| `FUN_823da3c8(0xffffffff83655494, uVar7, param_3)`                         | Resolve any per-entity override for the name (`0x83655494 = ???` lookup table) |
| 162996| `uVar8 = FUN_82408b88(param_3)`                                            | Resolve emitter-name-hash → emitter resource ptr |
| 163000| `FUN_8241e720(auStack_80, param_2, 0, 1, uVar7)`                           | Get entity's bone/transform                   |
| 163005| `FUN_82409b38(DAT_8313452c, uVar8, uVar9, local_f0, 0, 0)`                 | Allocate RuntimeEffect pool entry             |
| 163017| `FUN_823d9e10(0xffffffff83656008, uVar7, local_f0[0], local_cc, &local_d0)`| Install into **class record `0x83656008`** (= RuntimeEffect class record) |
| 163019| `piVar11 = FUN_823daf68(0xffffffff83657548, local_f0[0])`                  | Resolve `0x83657548` COMP on the new effect — pool handle |
| 163028| `iVar10 = FUN_823daf68(0xffffffff83655154, uVar8)`                         | **Resolve `RedEffectComponent` (class record `0x83655154`) for emitter resource** |
| 163040| `dVar18 = *(float *)(iVar10 + 0x1c)`                                       | Read **RedEffectComponent[+0x1c] = base lifetime** |
| 163044| `FUN_823da3c8(0xffffffff836551d4, uVar7, param_3)`                         | Per-entity lifetime multiplier look-up          |
| 163046| `dVar18 = dVar18 * *(float *)(iVar12 + 4)`                                 | Apply per-entity lifetime multiplier            |
| 163051-163076| `*(int *)(iVar2 + 0x770/0x774/0x778/0x790/0x794) = ...`              | Write particle params into the RuntimeEffect record |

The lifetime read `RedEffectComponent[+0x1c]` is interesting — it is the
same offset the sibling X360 map identified for RedEffectComponent (class
record `0x8365513c`, vtable `0x820df468`). **The class-record pointer `0x83655154`
here = `0x8365513c + 0x18`** which is the pool-handle entry inside the
RedEffectComponent class record. PROVEN by subtraction and cross-reference
to the sibling note.

### Gating conditions reached in the body

The handler does NOT gate on any of the task's listed conditions
(Flammable, Health/DamagedState, DamageKey=ExplosionLarge
`0xDC0A895F`, entity-type/kind-code). Its gates are:

1. **Resource-exists:** `uVar8 != 0` from `FUN_82408b88(param_3)` — i.e. the
   named emitter must exist in the loaded resource graph. If "fire" is not
   loaded as an emitter resource, line 162997 bails `return`.
2. **Transform-fetch:** `FUN_8241e720(...)` must return non-zero. Line 163001
   bails otherwise.
3. **Pool-alloc:** `local_f0[0] != 0` after `FUN_82409b38`. Line 163006 bails.
4. **RuntimeEffect handle:** `piVar11 != 0` and `*piVar11 != 0`. Lines 163024
   and 163033 bail.
5. **RedEffectComponent exists:** `iVar10 != 0` from
   `FUN_823daf68(0x83655154, uVar8)`. Line 163037 bails.
6. **Lifetime > 0:** `RedEffectComponent[+0x1c] > 0.0` (fcmp vs
   `DAT_820baf14 = 0.0`). Line 163041 bails if lifetime≤0.
7. **Per-entity lifetime multiplier > 0** (if present). Line 163048 bails.

**None of these are "is this entity flammable" / "is this a canopy / palm"
checks.** The handler is content-agnostic. The ignition decision happens
upstream.

### Direct callers of `FUN_8222fa68` on X360 retail

Scanning `xenon_retail_decomp_named.c` for `FUN_8222fa68`:

| decomp line | containing fn     | fn size | call-site context                                                                                              |
|-------------|-------------------|---------|----------------------------------------------------------------------------------------------------------------|
| 162945      | (declaration)     | 724 B   | —                                                                                                              |
| **162947**  | (signature)       | —       | `void FUN_8222fa68(undefined8 param_1,undefined8 param_2,ulonglong param_3)`                                   |
| **190376**  | `FUN_82257b70`    | 1376 B  | Direct call from a **per-entity iterator**. Fx-name hash is **`FUN_82398890(..., 0x5da4c94e, *(iVar4+4))`** — i.e. the entity's own "fx-on-destroy" (`hash=0x5da4c94e`) attribute, resolved from its template record. See analysis below. |
| **337838**  | `FUN_823341f0`    | 168 B   | **Lua binding** `ObjectState.StartEmitter`. Shape `FUN_8222fa68(local_14, local_18, local_1c)` after popping 3 Lua args (entity, name hash, flag) via `FUN_82308a80`/`FUN_823089b8`. |

**One hidden caller** — the FSM-command tail-call via `LAB_8222fa30`
(`0x8222fa30`-`0x8222fa64`) which falls through at `b .+4`. This caller is
not visible as a `FUN_8222fa68(...)` text match because the branch is a
raw `b`.

### Lua binding — `ObjectState.StartEmitter`

PROVEN from `/Volumes/Projects/mercenaries-game/output/_exemap_exports/x360-retail.luareg.tsv:720`:
```
0x820b9018   ObjectState   4   StartEmitter   0x823341f0
0x820b9018   ObjectState   5   StopEmitter    0x82334298
```
`FUN_823341f0@337818` (168 B) wraps the native `FUN_8222fa68`:

```c
undefined8 FUN_823341f0(undefined4 param_1 /* Lua state */) {
  local_20 = param_1;
  lVar1  = FUN_82308a80(&local_20, 1,              &local_14, 0);         // arg1: entity
  lVar3  = FUN_823089b8(&local_20, lVar1+1,        &local_18);            // arg2: emitter name hash ("fire")
  iVar4  = FUN_823089b8(&local_20, lVar3+lVar1+1,  &local_1c);            // arg3: flag
  if (positives) {
    FUN_8222fa68(local_14, local_18, local_1c);                          // → the real handler
    return 0;
  }
  return FUN_82206f20(&local_20);                                         // Lua error path
}
```

### `"StartEmitter(\"fire\")" call sites` summary — X360 retail

**No** site in the X360 retail C/C++ hot path passes the literal string
`"fire"` or a hard-coded 4-byte `"fire"` hash constant. "fire" always
arrives at `FUN_8222fa68` through **one** of these three channels:

| channel                | code path                                                   | source of the name hash                       |
|------------------------|-------------------------------------------------------------|-----------------------------------------------|
| **Lua**                | `FUN_823341f0 → FUN_8222fa68`                               | Lua call-stack top; typically a string→hash conversion inside `FUN_823089b8` or the Lua side. |
| **FSM state command**  | FSM state-runner → dispatch stub `LAB_8222fa30 → FUN_8222fa68` | Pre-hashed inline arg stored in the entity's FSM state block (data file). |
| **Per-entity iterator**| `FUN_82257b70 → FUN_8222fa68`                              | Entity template field keyed by `hash=0x5da4c94e` (the entity's "default fx" attribute). The hash `0x5da4c94e` is NOT `hash("fire")` — see "0x5da4c94e" note below. |

### Note on hash `0x5da4c94e` (used by `FUN_82257b70`)

Hash `0x5da4c94e` appears at retail-xenon lines 190345, 190367, 230172,
233048, 405065, 405084. Three bodies compare `local_14c == 0x5da4c94e` and
substitute a fallback. In X360 preview-xenon the same hash sits at lines
341036 / 341061 / 386472 / 391348 / 597337 / 597353. It is a **generic
attribute key** (something like `fx_on_death` / `fx_tag`), not the hash of
`"fire"`. SPECULATIVE: it's a "destroy-fx" attribute hash — `FUN_82257b70`
reads a per-entity default and only falls through to StartEmitter when the
entity has one.

---

## PS3

### `"StartEmitter"` on PS3 — same hash, three dispatch tables

PROVEN. Searching `ps3_retail_decomp_named.c` for the hash constant
`0x3347914f` returns two hits — the *registrar* body at lines 572632 and
572774:

```c
572632: iVar3 = FUN_00625ba8(0x3347914f, uVar1, 0x40);
572774: iVar3 = FUN_00625ba8(0x3347914f, uVar1, 0x40);
```

`FUN_00625ba8` is the PS3 structural analog of X360's `FUN_82855900` —
hash-into-bucket for an open-addressed 64-slot table. The two hits are the
two parallel dispatch tables (execution and sibling), matching the X360
pattern. **PS3 strings are not xref'd in the shipped Ghidra pass**, so I
cannot grep `"StartEmitter"` directly in the decomp; but the hash is
identical to X360 which is strong independent anchoring.

### String `"StartEmitter"` byte location on PS3

NOT located this run. The task flagged this as a hard-fail criterion. The
dispatch-side anchor (hash `0x3347914f`) at ps3 decomp lines 572632/572774
is PROVEN, and these are structural proofs of the same opcode mechanism as
X360. Hash-only anchoring on PS3 is sufficient for the dispatch map.

**Not-verified gap:** the raw ELF has not been scanned for the ASCII bytes
`"StartEmitter"` yet. If a reviewer wants string-level confirmation, grep
the EBOOT.elf (file at `/Volumes/Projects/mercenaries-game/game-files/ps3-version/EBOOT.elf`).

### Real StartEmitter handler — `FUN_00389b10` @ VA `0x00389b10`

PROVEN. 792 bytes, ps3_retail_decomp_named.c:566941-567032. Signature
`(entity_id, emitter_name_hash, flag)`. Body structurally parallel to X360
`FUN_8222fa68`:

| ps3 line | role                                                                                     |
|----------|------------------------------------------------------------------------------------------|
| 566972   | `uVar4 = FUN_002a9b80()` — PS3 TOC fetch for class-record `Explosive`-era anchor         |
| 566973   | `lVar2 = FUN_0031c888(uVar4, lVar9, param_3)` — per-entity override lookup                |
| 566977   | `iVar5 = FUN_0034cac0(param_3)` — resolve name hash → emitter resource                    |
| 566979   | `FUN_00213480(auStack_90, param_2, 0, 1, lVar9)` — transform/bone fetch                   |
| 566982   | `FUN_0062ac70(auStack_100, auStack_90)` — copy transform                                  |
| 566983   | `FUN_00351168(..., lVar2, auStack_100, local_110, 0, 0)` — pool-alloc RuntimeEffect       |
| 566994   | `FUN_0031cbd0(uVar4, lVar9, local_110[0], local_bc, &local_c0)` — install into RuntimeEffect PgCdbFlatHash |
| 566997/567000 | `uVar7 = FUN_0031e918 or FUN_0031f340(lVar3+0x18, local_110[0])` — PgCdbFlatHash dispatch, parallel to X360 `FUN_823daf68/FUN_823db8f0` |
| 567012   | `dVar11 = *(float *)(lVar2 + 0x1c)` — **RedEffectComponent[+0x1c] = base lifetime**       |
| 567013   | `fVar1 = *(float *)(uVar10 - 0x7fc4)` — load `0.0` constant from TOC                      |
| 567014   | **`if ((double)fVar1 < dVar11)` — gate on lifetime > 0.0**                                |
| 567018   | `dVar11 * lVar9[+4]` — per-entity lifetime multiplier                                     |
| 567020   | `FUN_00257068(dVar11, lVar2[+0x2c], lVar2[+0x30], iVar5+0x8f0, param_1, param_2, lVar2[+0x20]!=0)` — spawn the effect |

The gating is **identical** to the X360 pattern: resource exists, transform
exists, pool-alloc succeeds, RuntimeEffect handle non-zero,
RedEffectComponent exists with lifetime > 0, per-entity multiplier > 0.
PROVEN byte-for-byte parallel.

### Direct callers of `FUN_00389b10` on PS3

Three sites in `ps3_retail_decomp_named.c`:

| ps3 line | containing fn    | size   | role                                                                                                 |
|----------|------------------|--------|------------------------------------------------------------------------------------------------------|
| 567766   | `FUN_0038ac70`   | 68 B   | **FSM dispatch stub** (equivalent of X360 `LAB_8222fa30`). Pops two inline args from state ctx `param_1 + ctx_cursor*4 + 8` and tail-calls. Body: `*(param_1+4) -= 2; FUN_00389b10(param_2, *(param_1+(cursor-1)*4+8), *(param_1+cursor*4+8))`. PROVEN. |
| 610019   | `FUN_003ced28`   | 2680 B | **Per-entity iterator**, parallel to X360 `FUN_82257b70`. Uses the same **hash `0x5da4c94e`** (line 610010) to look up a per-entity "fx tag" attribute via `FUN_001c0c98(local_c0, 0x5da4c94e, *(uVar22+4))`. Then calls `FUN_00389b10(lVar29, FUN_00228e30(iVar23, (int *)uVar22[0xa9]), local_1d0)`. |
| 830120   | `FUN_00548618`   | 292 B  | **Lua binding** `ObjectState.StartEmitter`. Pops 3 Lua args (entity, name hash, flag) via `FUN_00502470`/`FUN_005005f8` and calls `FUN_00389b10(local_2c, local_28, local_24[0])`. |

The FSM-stub `FUN_0038ac70` is a 68-byte function on PS3 whereas on X360 it
is a 56-byte inline stub (`LAB_8222fa30`) that Ghidra did not treat as a
function entry. The PS3 compiler emitted a proper function; the X360 one
inlined the branch. Behavior is identical.

### Lua binding registration — PS3

PROVEN via exemap translation from X360 retail `0x823341f0` → PS3 v1.00
`0x00548618` (1957 of 1957 paths agree). The PS3 `luareg.tsv` lists the
same `ObjectState.StartEmitter` entry pointing at `0x00548618`.

### `"StartEmitter(\"fire\")" call sites` summary — PS3

Identical shape to X360: no literal-"fire" call site; three channels into
`FUN_00389b10` — Lua wrapper, FSM dispatch stub, per-entity iterator. The
per-entity iterator uses the same `0x5da4c94e` attribute hash.

---

## PC

### Lua binding — `ObjectState.StartEmitter`

PROVEN from the exemap match graph
(`match-v2/pc-v10__x360-retail.tsv:585`):
```
0x005d2fa0   0x823341f0   lua
```
and `match-v2/x360-retail__ps3-v100.tsv:552`:
```
0x823341f0   0x00548618   lua
```

Cross-platform Lua `StartEmitter` wrapper VAs — PROVEN by `exemap translate`
over all available match paths (1957 of 1957 agree for every target):

| image               | wrapper VA   |
|---------------------|-------------|
| X360 retail         | `0x823341f0`|
| X360 preview-final  | `0x823333a0`|
| X360 preview-profile| `0x8244df58`|
| PC v10              | `0x005d2fa0`|
| PC v10-drmfree      | `0x005d2fa0`|
| PC v11              | `0x005d2f90`|
| PS3 v1.00           | `0x00548618`|
| PS3 v1.03-NA        | `0x0054c6f8`|

### Native StartEmitter impl on PC

NOT exemap-paired. `FUN_8222fa68` (X360 retail) → PC v10 translation
returns **no pair** on every path. The PC native handler must be found by
following `FUN_005d2fa0`'s callees — which this run did not disassemble.
The PC v10 fnsig entry for `0x005d2fa0` is **absent** (the fnsig DB skips
sub-200-byte Lua wrappers), so the callee list isn't in the fnsig graph
either. Finding the PC native handler is a follow-up pass (grep PC decomp
or disassemble `0x005d2fa0`).

### String `"StartEmitter"` on PC

PROVEN. VA `0xBB8828` (file offset `0x7B8828`, in `.rdata` section — image
base `0x400000`, `.rdata` RVA `0x705000`→`0xB05000`). Preceded in memory
by `"StopEmitter\0"`.

### String `"fire"` on PC

PROVEN NEGATIVE: scanning `Mercenaries2.exe` for standalone `"fire\0"`
(regex `(?<![A-Za-z0-9_])fire\x00`) returns **zero** hits
(`scratchpad/pc_strings_fire.log`). "fire" is not a string literal in the
PC executable. This matches the X360 preview-xenon decomp which likewise
has zero `"fire"` string refs.

### `FUN_004D28C0` — the prior-note PC candidate

PROVEN NEGATIVE on PC v11 fnsig DB: `0x004D28C0` is **not** a function
entry in PC v11 (neighbors: `0x004D2710`, `0x004D2A50`). It IS a function
entry in **PC v10** / **PC v10-drmfree**:

PC v10 fnsig for `0x004D28C0` (from pc-v10.fnsig.json):
```
sig  : 69f43ab7701ed45b
n    : 150 instructions
sz   : 516 bytes
calls: 11
callees: 0x00649440, 0x00672f60, 0x00688970, 0x008231d0, 0x006746d0,
         0x00824790, 0x00649180, 0x005857e0 (x2), 0x00649440, 0x0048b470
drefs : 0x00d2ae64, 0x01286dd4, 0x51d71134, 0x00df8b10, 0x01176108,
        0x00d8a41c, 0x00b9b664, 0x00df9110, 0x017bfba8, 0x017be398,
        0x00b9b690, 0x00df8a10, 0x00b9b690
strs  : (empty — PROVEN NEGATIVE)
```

**`FUN_004D28C0`'s `strs` list is empty, and none of its `drefs` points
to the "StartEmitter" string VA `0x00BB8828` or to a known Lua state
pointer.** Also, exemap-translate of `FUN_004D28C0` across every path of
the match graph returns **"no pair"** for every target image — PROVEN by
`exemap_translate_pc_4d28c0.log`.

**Verdict on prior claim:** The claim that PC `FUN_004D28C0` is "the
SWIT-level `StartEmitter("fire")` call site" is **NOT supported by the
evidence this run**. `FUN_004D28C0` has no string references and does not
pair cross-platform. It may be an unrelated per-entity update function
whose VA happened to be noted; the trail to `StartEmitter` from
`FUN_004D28C0` is not visible in the fnsig graph. INFERRED: either the
prior note was mis-recorded, or the connection is via a callee of
`FUN_004D28C0` and the Lua infrastructure (not a direct call).

The actual PC `StartEmitter` entry point is **`FUN_005d2fa0`** (the Lua
wrapper), which is the only cross-platform-paired symbol in this chain.

### Gating conditions on PC

Not inspected this run — would require disassembling the PC native handler
(callee of `0x005d2fa0`). By structural analogy (both X360 and PS3 share
byte-identical gates), the PC gates will be the same: emitter resource
exists, transform exists, pool-alloc succeeds, RedEffectComponent exists
with lifetime > 0, per-entity multiplier > 0. **NOT VERIFIED** this run.

---

## Three-platform gating diff

| gating condition                                     | X360 (FUN_8222fa68) | PS3 (FUN_00389b10) | PC (native, not disassembled) |
|------------------------------------------------------|---------------------|--------------------|-------------------------------|
| Named emitter resource exists                        | line 162997, bail   | line 566977, bail  | likely same                   |
| Entity transform available                           | line 163000, bail   | line 566979, bail  | likely same                   |
| RuntimeEffect pool alloc succeeds                    | line 163005, bail   | line 566983, bail  | likely same                   |
| RuntimeEffect handle non-zero                        | line 163024, bail   | line 567011, bail  | likely same                   |
| **RedEffectComponent exists** on emitter             | line 163028, bail   | line 567004, bail  | likely same                   |
| **RedEffectComponent[+0x1c] (lifetime) > 0.0**       | line 163041, bail   | line 567014, bail  | likely same                   |
| Per-entity lifetime multiplier (`0x836551d4` COMP) > 0 | line 163048, bail | line 567018, bail  | likely same                   |
| Flammable-record on `this` check                     | **ABSENT**          | **ABSENT**         | likely absent                 |
| Health/DamagedState check                            | **ABSENT**          | **ABSENT**         | likely absent                 |
| DamageKey = ExplosionLarge (`0xDC0A895F`) check      | **ABSENT**          | **ABSENT**         | likely absent                 |
| Entity-type / kind-code check                        | **ABSENT**          | **ABSENT**         | likely absent                 |

**All three task-mentioned ignition-specific gates are absent on BOTH
consoles.** The X360 and PS3 native handlers are byte-wise parallel. The
PC handler was not disassembled but has the same callers and the same Lua
binding, so by structural analogy its gating is also unlikely to introduce
Flammable/DamageKey checks.

---

## Verdict

**The differentiator for "canopy ignites on consoles but not on PC" is
NOT in the `StartEmitter` call site**. PROVEN by:

1. The native `StartEmitter` handler is content-agnostic across platforms:
   it emits whatever name hash it is handed, after a fixed set of
   resource/alloc/lifetime gates that are byte-parallel between X360 and
   PS3. (RedEffectComponent lifetime check is the only ignition-relevant
   gate, and it's identical across console builds.)

2. The three channels into the native handler on both consoles are:
   - **Lua binding** (`FUN_823341f0` / `FUN_00548618` / `FUN_005d2fa0`)
   - **FSM-command stub** (X360 `LAB_8222fa30` / PS3 `FUN_0038ac70`), which
     reads the emitter name hash from an inline FSM state data block
   - **Per-entity iterator** (X360 `FUN_82257b70` / PS3 `FUN_003ced28`),
     which reads the "fx-tag" attribute (hash `0x5da4c94e`) from the
     entity template

3. The literal `"fire"` string **does not exist in any of the three
   binaries' code sections**. The hash of `"fire"` is also absent (we did
   not compute the hash because the Pandemic hash function was not reversed
   this run — see "Not verified").

**The next upstream points to walk** are therefore:

- **FSM state data in authored content** — the per-SWIT `.state`
  files/tables. If the console version of a canopy's destruction state
  table carries a `StartEmitter("fire", ...)` row that PC's does not, that
  is the entire divergence. This is a **data** check (WAD content), not a
  code check.
- **The entity-template "fx tag" attribute** read by the per-entity
  iterator (hash `0x5da4c94e`). If a canopy's template records a fire-fx
  on-destroy on consoles but a different / null one on PC, that is the
  divergence. **Follow-up:** reverse the hash `0x5da4c94e` to the string
  name of the attribute key, then diff the attribute's value on
  `jungle_env_largecanopy01` across platform-specific wads.
- **The Lua side** — if a destroy-callback script on consoles calls
  `ObjectState.StartEmitter(obj, "fire")` on canopies but PC's build
  doesn't ship that script. The Lua corpus is available in
  `_exemap_exports/lua/` and in each WAD; **follow-up:** `grep -r
  StartEmitter` through the pair-matched Lua index to find per-script
  divergences.

---

## Exemap seed candidates

Candidates for new (PC VA, X360 VA, PS3 VA) tuples with confidence captions.
All tuples below come either from exemap-proven pairings (HIGH) or from
structural analogy of fn signature + role (MEDIUM).

| role                                   | PC v10 / v11               | X360 retail | PS3 v1.00  | confidence | basis                                                 |
|----------------------------------------|----------------------------|-------------|------------|------------|-------------------------------------------------------|
| **Lua binding `ObjectState.StartEmitter`** | `0x005d2fa0` / `0x005d2f90` | `0x823341f0` | `0x00548618` | HIGH       | exemap 1957/1957 paths agree; luareg.tsv confirms.    |
| **Lua binding `ObjectState.StopEmitter`**  | (unverified)               | `0x82334298` | (likely `0x00548740` neighbor) | MEDIUM | same table, adjacent slot.                            |
| **Native StartEmitter handler**        | (unknown — follow callees of `0x005d2fa0`) | `0x8222fa68` | `0x00389b10` | MEDIUM (X360↔PS3) / LOW (PC) | X360↔PS3 body gates and callee pattern are byte-parallel; PC untranslated by exemap. |
| **FSM dispatch stub (StartEmitter tail-call)** | (unknown)        | `0x8222fa30` (56-byte inline, not a func entry) | `0x0038ac70` | MEDIUM | Both strip 2 inline args from state ctx and tail-call the native handler. |
| **Per-entity iterator w/ fx-tag `0x5da4c94e`** | (unknown — exemap unpaired) | `0x82257b70` | `0x003ced28` | MEDIUM | Both call the native StartEmitter with `FUN_hash_lookup(..., 0x5da4c94e, template)`. Not exemap-pairable (both are large and heavily inlined). |
| **FSM dispatch registrar body**        | (unknown)                  | around line 162280-162418 of retail-xenon (anchor hash `0x3347914f` at line 162298) | around line 572630-572780 of ps3 retail (anchor hash `0x3347914f` at line 572632) | MEDIUM | Both seed two parallel 64-slot command tables with 30+ opcodes and share the hash constants. |
| **Hash-to-bucket helper `h(x) % 0x40` style** | (unknown)           | `FUN_82855900` (insert) / `FUN_82855778` (find) | `FUN_00625ba8` (insert) / find-side companion | MEDIUM | Both are the open-addressed lookup for the FSM command tables. |

### Data-side anchors

| anchor                                                | X360 retail  | PS3 v1.00 | notes                                                 |
|-------------------------------------------------------|--------------|-----------|-------------------------------------------------------|
| String `"StartEmitter"`                               | `0x820d6d30` | (file-offset not scanned) | X360: raw-image scan proved; PS3: scan the ELF to confirm. |
| Hash `"StartEmitter" == 0x3347914f`                   | shared       | shared    | Confirmed by the three (X360) / two (PS3) hash-constant occurrences. |
| String `"StartEmitter"` on PC                         | VA `0x00bb8828` | — | file offset `0x7b8828` in `.rdata`.                     |
| Execution-dispatch table base (StartEmitter)          | `0x83567f88` | (TOC-base relative, not scanned) | X360 only; PS3 is `TOC[-??]`, not located. |
| Sibling (undo/stop) table base                        | `0x83568190` | (same)    | binds StartEmitter to shared no-op `LAB_82232430`.      |
| Debug reverse-registry (hash → name) base             | `0x836db808` (preview-xenon) | — | holds literal `"StartEmitter"` at the hash slot.       |
| RedEffectComponent class record                       | `0x8365513c` (per sibling X360 map), pool handle `0x83655154` | typeinfo at `0x00f58xxx`? (not scanned specifically for RedEffect) | sibling notes it as console-only. |
| Attribute hash used by the per-entity iterator        | `0x5da4c94e` | `0x5da4c94e` | shared constant; semantic name not reversed.          |

---

## Adversarial pass

**Competing explanation 1:** "`LAB_8222fa30` is a stand-alone function, not
a tail-call forwarder into `FUN_8222fa68`."

Does it survive? **No.** The last instruction of the stub (`b .+4`) jumps
to exactly `0x8222fa68`, which `/output/_ghidra_x360_retail/func_starts.txt`
line 2245 lists as a function start. There is no return instruction
anywhere in the 56-byte stub body. PROVEN tail-call. FUN_8222fa30 has no
own frame, no mflr save, no stwu — it's a leaf-forwarder.

**Competing explanation 2:** "`FUN_8222fa68` is a different function
(stop-emitter, update-emitter, etc.)."

Does it survive? **No.** The decomp body resolves a name hash → emitter
resource (line 162996, `FUN_82408b88`), allocates a RuntimeEffect pool
entry (line 163005), installs it (line 163017), and reads parameters from
the resolved RedEffectComponent (line 163028-163055). This is a
**create/spawn** signature. The sibling retail function at `0x8222fd78`
(decomp line 163092) is 280 bytes and is called from `FUN_82334298` —
`ObjectState.StopEmitter` — i.e. StopEmitter has its own real handler. So
`0x8222fa68` is unambiguously the start/spawn handler.

**Competing explanation 3:** "FSM command table slot for `0x3347914f` is
actually bound to the no-op `LAB_82232430` because the slot was overwritten
by a later registrar."

Does it survive? **No.** The two registrar lines 162298 and 162368 **write
to DIFFERENT TABLES** (`0x83567f88` and `0x83568190`). Line 162298 binds
`LAB_8222fa30` into the execution table; line 162368 binds the no-op into
the sibling (undo/stop) table. Both writes stand.

**Competing explanation 4:** "The hash `0x5da4c94e` IS the hash of
`"fire"`."

Does it survive? **No.** The decomp uses `0x5da4c94e` and `local_154`
(entity-provided hash) side by side (preview-xenon:597353 — `uVar11 =
0x5da4c94e; if (!bVar5) uVar11 = local_154;`). If `0x5da4c94e` were
`hash("fire")`, then an entity's own `local_154` would ALSO be
`hash("fire")` in the matching case — which can't be true because
`local_154` is read from the entity's template at `+0x34`-ish and
different entities store different hashes there. `0x5da4c94e` is an
**attribute key** or a **default-fx name**, not `"fire"` specifically.

**Competing explanation 5:** "PC `FUN_004D28C0` is still the right PC call
site, just unlabelled in the fnsig DB."

Does it survive? **Weakly.** `FUN_004D28C0` IS an entry in PC v10's fnsig
(size 516, 150 insns). But: (a) it has zero `strs`, (b) its drefs don't
point at `0x00bb8828` (the `"StartEmitter"` string VA), (c) it is
exemap-unpaired on every path. The only way it could reach
`StartEmitter("fire")` is through a *data-driven* path (callees that
eventually dispatch via Lua or the FSM runner). In that case the
"call site" framing is itself wrong — `FUN_004D28C0` would be an upstream
caller that invokes an FSM transition or a Lua chunk, not a C-level
`StartEmitter(obj, "fire")` call site.

---

## Not verified

- **Pandemic hash function body** — not reversed; could not independently
  compute `hash("fire")` to grep for the constant. The 32-bit constant
  that the shipping binaries use for `"fire"` is unknown.
- **PC native `StartEmitter` handler** — not disassembled. Should be a
  callee of `0x005d2fa0`.
- **PS3 raw-image byte scan for `"StartEmitter"`** — not performed. The
  hash-based anchor is sufficient for dispatch, but the string VA proof is
  missing on PS3 proper. Follow-up: `strings` or raw-byte grep of
  `/Volumes/Projects/mercenaries-game/game-files/ps3-version/EBOOT.elf`.
- **The reader side of X360 tables `0x83567f88` / `0x83568190`** —
  `FUN_82232870` is the generic reader but is only called from two sites,
  neither of which invokes the resolved slot. The actual FSM step-runner
  inlines the hash lookup. The step-runner itself was not located this
  run.
- **`FUN_82257b70` purpose** — this is a 1376-byte per-frame per-entity
  iterator with 90+ branches. Its specific trigger (per-frame tick vs
  per-explosion sweep vs destroy-notification handler) was not resolved.
- **Semantic of hash `0x5da4c94e`** — not reversed. SPECULATIVE
  "fx-on-destroy" / "fx-tag" / "DefaultEffect".
- **Semantic of class records `0x836551d4` (X360)** — the per-entity
  lifetime multiplier COMP. Role not fully proven; may be a
  "effect-lifetime-scalar" tag read from Lua.
- **The claim that `FUN_82257b70` fires fx only on *destroyed* entities**
  — likely but unverified. Would require reading the containing dispatcher
  of `FUN_82257b70`.
- **PC side of the three channels** — the FSM dispatch stub and
  per-entity iterator are compiler-and-platform-specific; neither has been
  located on PC. The Lua wrapper is PROVEN; the other two are structural
  SPECULATIVE pairings.
