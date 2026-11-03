# emitfix.c independent verification

## Inputs

- **PC v1.1 exe**: `/Volumes/Projects/mercenaries-game/game-files/canonical/Mercenaries 2 World in Flames v1.1/Mercenaries2.exe`
  - MD5 `5b9976f162e050f4adcc51bb997ba97f`  (matches remaster's claimed "as-shipped v1.1 with disc check")
  - File size 53,944,080 bytes; PE32; ImageBase `0x00400000`; SizeOfImage `0x03371F10`
  - `.text` section: VA=`0x00401000`, VSZ=`0x00704000`, RawPos=`0x00001000`
- **X360 retail decomp**: `/Volumes/Projects/mercenaries-game/output/_ghidra_x360_retail/xenon_retail_decomp_named.c` (10.9 MB, strings present).  Raw `.xex` not re-parsed; relied on named Ghidra decomp as instructed.
- All byte reads performed directly from the on-disk PE via Python/capstone — scripts in this scratchpad: `pe_read.py`, `disasm.py`, `disasm2.py`, `scan_offsets.py`.

## Signature check table (PC v1.1)

| VA | Expected (remaster) | Bytes read (PE32) | Match |
|---|---|---|---|
| `0x4787D4` | 5-byte `E8 ..` reaching `0x85C070` | `E8 97 38 3E 00` → target = `0x4787D9 + 0x003E3897` = **`0x0085C070`** | **PROVEN** |
| `0x4787AA` (0x4787D4-0x2A) | `80 7C 24 60 00 74 28 8B 54 24 18` | `80 7C 24 60 00 74 28 8B 54 24 18 52 8D 85 10 01` — first 11 bytes **exact** | **PROVEN** |
| `0x85C070` | function entry (index-copy) | `56 8B 77 14 85 F6 74 0C E8 A3 EE FE FF C7 47 14 00 00 00 00` (push esi; mov esi,[edi+0x14]; test; call `0x84AF20`; `mov dword ptr [edi+0x14], 0`); further on reaches `call 0x84AE70` with eax=0, ecx=0 and size pushed — PROVEN this is the "free-then-grow and realloc via `0x84AE70`" index/strip buffer copier the remaster describes | **PROVEN** |
| `0x4787D9` | AreaStub return-addr epilogue `5F 5E 5D 5B 83 C4 4C C2 08 00` | `5F 5E 5D 5B 83 C4 4C C2 08 00 CC CC CC CC CC CC` — first 10 bytes **exact** (`pop edi; pop esi; pop ebp; pop ebx; add esp, 0x4C; ret 8`) | **PROVEN** |
| `0x84AE70` | allocator entry | `53 55 8B 6C 24 0C 85 ED 56 57 8B F8 8B F1 75 08 8D 45 01 ... C3` — function prolog; path: `if (size == 0) return size+1`; else enters a bucket allocator with critical-section (`call dword ptr [0xB05128]` / `[0xB0512C]` = Enter/LeaveCS pair at 0x84AEB0 and 0x84AF06), then tail-calls `0x84DF50` / `0x84D9D0`. Entry PROVEN. ABI note: it takes **two reg args (eax, ecx) plus one stack arg at [esp+0xC]** and ends with plain `ret`, not `ret N` — i.e. caller cleans the stack arg. Remaster's label "stdcall 2 args" is loose/inaccurate but the function identity is correct. | **PROVEN entry; ABI label partially wrong** |
| `0x4D2C3A` | `0F 95 C2 D9 1C 24 50 51` | `0F 95 C2 D9 1C 24 50 51 8B 4D 0C 8B C3 52 E8 F3` — first 8 bytes **exact** | **PROVEN** |
| `0x48ADBF` | 5-byte `E8 ..` reaching `0x49BC90` | `E8 CC 0E 01 00` → target = `0x48ADC4 + 0x00010ECC` = **`0x0049BC90`** | **PROVEN** |
| `0x49BC90` | mesh area walker entry | `83 EC 18 D9 44 24 2C 0F 57 C0 83 EC 08 D9 5C 24 04 89 44 24 10 D9 44 24 30 D9 1C 24 52 8B 54 24 2C 50 52 F3 0F 11 44 24 14 F3 0F 10 ...` — function prolog; this is a **short wrapper (ends `ret` at `0x49BD03`)** that packs the inputs and tail-calls the real mesh workhorse `0x49CCE0` (`call 0x49CCE0` at `0x49BCF0`), then clamps the fp result against `esi` and returns. See "Function identity" below. | **PROVEN entry; see caveat on body** |
| `0x48FED8` | 5-byte `E8 ..` reaching `0x48AF50` | `E8 73 B0 FF FF` → target = `0x48FEDD + 0xFFFFB073` = **`0x0048AF50`** | **PROVEN** |
| `0x48AF50` | emit-by-shape entry | `83 EC 50 53 55 8B 6C 24 60 56 8B F0 8B 01 83 F8 07 57 ... FF 24 85 A0 B3 48 00 ...` — prolog + `mov eax, [ecx]; cmp eax, 7; ja default; jmp dword ptr [eax*4 + 0x48B3A0]` — **8-way jump table** on an int tag at `[ecx+0]`. Jump table at `0x48B3A0`: case 0=`0x48B066`, case 1=`0x48AF77`, case 2=`0x48B203`, case 3=`0x48B25A`, case 4=`0x48AFE7`, case 5=`0x48B025`, case 6=`0x48B2B0`, case 7=`0x48B302`. **Entry PROVEN**. | **PROVEN** |

**Call-site context for the `effect+0x770` claim (PROVEN):** at `0x48FED2..0x48FED8` the caller sets `lea ecx, [ebx+0x770]` and immediately `call 0x48AF50`. That is byte-level proof that `ecx` passed into emit-by-shape is **(effect struct)+0x770**, and the switch dispatch inside dereferences `[ecx+0]` — i.e. the shape-type word lives at `effect+0x770`, exactly as the remaster claims.

## Function identity table

### Walker chain `0x48ADBF → 0x49BC90 → 0x49CCE0`

- `0x49BC90` is a thin wrapper: builds a context struct on the stack (sets `dword ptr [eax+8] = &<ctx>` and seeds `[esp+0x28] = 0x00BAC4C8` — a function/vtable pointer in `.rdata`), then `call 0x49CCE0`, converts the returned float to int and clamps to `esi`.
- `0x49CCE0` reads **`mov edi, dword ptr [ecx + 0x1E0]`** (same `+0x1E0` offset Xbox walks — see below), then `mov eax, dword ptr [edi+0x4C]`, `mov ebx, dword ptr [edi+0x50]`, iterates on `+0x50` as an index/table pointer, dereferences `+0x2A0` on a sibling struct, and later `+0x60` and `+0x58`. None of the first ~400 bytes of `0x49CCE0` directly reads `+0x154/+0x158/+0x15C` off `edi` or `ecx`. The specific submesh fields the remaster names are presumably loaded in callees of `0x49CCE0` that this verification did not drill into.

### Case-0/4/5 emit path `0x48AF50 → common thunk at 0x48AFAF → 0x49BE00 → 0x49D380`

- Case 0 (`0x48B066`), case 4 (`0x48AFE7`) and case 5 (`0x48B025`) all converge on a shared body at `0x48AFAF` after pushing a shape-type literal (0, 4 or 5). That body does `call 0x49BE00` (at `0x48AFD1`).
- `0x49BE00` builds a 0x30-byte struct on the stack (zero-fills, copies vectors from caller stack slots), seeds `[esp+0x40] = 0x00BAC4C8` (same vtable pointer as `0x49BC90`), and `call 0x49D380`. Again, no `+0x154/+0x158/+0x15C` reads in the first 768 bytes.
- Case 1 (`0x48AF77`) handler reads **`mov dl, byte ptr [ecx + 0x35]; test dl, 2`** and bails if the bit isn't set — this is almost certainly the "effect sub-shape enable flag" the remaster alludes to. Then `movss xmm0, [ecx+0x1C]` scales a per-area factor and reaches the same thunk at `0x48AFAF`.

### Submesh offsets `+0x154 / +0x158 / +0x15C`

Byte-pattern scan of `.text` for disp32 modrm references:

| Offset | Raw byte-pattern occurrences | Example valid instructions |
|---|---|---|
| `+0x154` | 430 | `fld dword ptr [esi+0x154]` @ `0x426E24`; `cmp dword ptr [esi+0x154], eax` @ `0x408574` |
| `+0x158` | 517 | `mov eax, dword ptr [edi+0x158]` @ `0x4269B9` / `0x4269FC`; `fstp dword ptr [eax+0x158]` @ `0x435BF6`; `comiss xmm0, dword ptr [ebx+0x158]` @ `0x435D4E` |
| `+0x15C` | 334 | `fld dword ptr [ebx+0x15C]` @ `0x435D76`; `fstp dword ptr [eax+0x15C]` @ `0x435C02`; `fld dword ptr [ecx+0x15C]` @ `0x438352` |

**PROVEN**: these three offsets are read extensively off some struct(s) in `.text` — plausibly the submesh. **UNVERIFIED**: that they are specifically "strip indices pointer / FLOAT16 position pointer / per-triangle area pointer", that it is actually the walker chain reaching them (as opposed to other geometry code), and that they are null on the "many meshes that don't have them". That is the load-bearing diagnostic claim and this verification did not trace to it.

## StartEmitter at `0x4D2A50`

Prolog: `55 8B EC 83 E4 F0 ... 81 EC A4 00 00 00`  (push ebp; mov ebp,esp; and esp,-0x10; sub esp,0xA4). Epilogue at `0x4D2C53`: `mov esp,ebp; pop ebp; ret` (plain `C3`, no `ret N`).
- Reads **three** args at `[ebp+8]`, `[ebp+0xC]`, `[ebp+0x10]`. Caller cleans → **cdecl**, 3 args. **PROVEN**.
- Early path at `0x4D2A8D..0x4D2AAB`: `mov esi, [ebp+8]; push esi; mov eax, 0xDF7B10; call 0x6493D0` — a lookup indexed by arg1 against a global table at `0x00DF7B10`. The returned pointer is dereferenced at `+4`. Shape consistent with "resolve arg1 (GUID?) → object pointer". **INFERRED** that arg1 is a GUID.
- `mov eax, [ebp+0x10]; call 0x672EB0` later — fetch of a second object; stored in `ebx`. Consistent with arg3 as a hash → emitter template pointer.  **INFERRED**.
- Transit into emit-by-shape chain: `0x4D2C34 lea esi, [edi + 0x770]` then `0x4D2C48 call 0x48B540` (NOT a direct call to `0x48AF50`, but `0x48B540` is the dispatcher that routes to `0x48AF50`). This matches the remaster's claim that StartEmitter transits through emit-by-shape.

So: `0x4D2A50` is cdecl with 3 args, PROVEN; the "(guid, node, hash)" semantic label is INFERRED but structurally plausible.

## Hidden-node skip at `0x4D2C3A`

Full byte-level context (`0x4D2C20..0x4D2C48`, all PROVEN):

```
0x4D2C20  mov eax, [esi+0x28]
0x4D2C23  mov ecx, [esi+0x24]
0x4D2C26  sub esp, 8
0x4D2C29  cmp dword ptr [esi+0x20], 0    ; <-- the compare
0x4D2C2D  fstp dword ptr [esp+4]
0x4D2C31  fld  dword ptr [esi+0x2C]
0x4D2C34  lea  esi, [edi+0x770]
0x4D2C3A  setne dl                        ; <-- claimed hidden-node gate
0x4D2C3D  fstp dword ptr [esp]
0x4D2C40  push eax                        ; arg5
0x4D2C41  push ecx                        ; arg4
0x4D2C42  mov ecx, [ebp+0xC]
0x4D2C45  mov eax, ebx
0x4D2C47  push edx                        ; arg3 = (dl expanded) = ([esi+0x20] != 0)
0x4D2C48  call 0x48B540
```

- There IS a conditional-forwarding boolean built at `0x4D2C3A`: `dl` is set from `cmp [esi+0x20], 0` 11 bytes earlier.
- But **this is NOT a `je`/`jne` that would "skip" the call to the emitter dispatcher** — the call happens unconditionally. The boolean `dl` is instead **pushed as the first stack argument** to `0x48B540`, which is where the actual skip-or-emit decision presumably lives.
- Describing this literally as "hidden-node skip at the StartEmitter site, gated by `setne dl` at 0x4D2C3A" **oversimplifies**. The gate is downstream, inside `0x48B540`; the `setne dl` here only produces the boolean that drives it. If a patch flipped this `0F 95 C2` to `0F 94 C2` or forced `dl=1`, it would make the "node is not hidden" claim universally true — but it does not avoid the call, and it isn't locally the branch.  **PARTIAL** confirmation.

## Xbox analog

All five Xbox VAs the remaster names appear as named functions in the Ghidra decomp:

| Xbox VA | Decomp entry | Body |
|---|---|---|
| `0x8222FA68` | `FUN_8222fa68`, size=8 | Tail-calls `FUN_82861BD4` (vtable/thunk stub) |
| `0x821C9268` | `FUN_821c9268`, size=8 | 8-byte stub |
| `0x82573EE0` | `FUN_82573ee0`, size=8 | 8-byte stub |
| `0x821EA888` | `FUN_821ea888`, size=8 | 8-byte stub |
| `0x821EB2A8` | `FUN_821eb2a8`, size=8 | 8-byte stub |

Caveats:
- "size=8" means Ghidra classified these as 8-byte PPC trampolines, not full function bodies. The real logic lives at the thunk targets (e.g. `FUN_82861BD4` for StartEmitter). This is normal for Xbox 360 inter-DLL / vtable dispatch stubs, and does NOT invalidate the address labels — the stub IS the entry point callers see.
- Call site at `xenon_retail_decomp_named.c:97634` invokes `FUN_8222fa68(local_14, local_18, local_1c)` with **three** decoded tokens from a `FUN_82308a80`/`FUN_823089b8` parser chain. Consistent with cdecl-style `(guid, node, hash)`.  **PROVEN** 3-arg call.
- `FUN_821EB2A8` call site at line 27310 includes **`uStack_24 = *(undefined4 *)(*(int *)(param_8 + 0x1e0) + 4)`** — i.e. the Xbox walker caller reads **`[struct + 0x1E0]` then `+4`** — byte-identical offset chain to PC `0x49CCE0` (`mov edi, [ecx+0x1E0]; mov eax, [edi+0x4C]` — the +0x4C differs but the +0x1E0 root is the same).  **PROVEN** structural parallel.

So the Xbox addresses are plausibly the analogs of the PC path. Confirming the Xbox "reads vertex buffers directly instead of a CPU submesh copy" claim would require disassembling the trampoline targets (`FUN_82861BD4`, bodies behind `FUN_821EB2A8`) and comparing against known D3D VB handles — not done here.

## Verdict

**PARTIALLY CONFIRMED.**

- **Signatures (10/10) PROVEN**: every expected byte sequence and every call target in the remaster's v1.1 address table matches the PE32 on disk.
- **Function identities PROVEN** for 0x4787D4 call site, 0x85C070 index-copy, 0x4787D9 epilogue, 0x4D2A50 StartEmitter (cdecl 3-arg), 0x48ADBF→0x49BC90 call, 0x49BC90 walker *entry*, 0x48FED8→0x48AF50 call, 0x48AF50 emit-by-shape (8-way switch on `[ecx+0]` where `ecx = effect+0x770`), and `0x4D2C3A setne dl`.
- **Allocator ABI label mildly inaccurate**: `0x84AE70` is not pure stdcall-2 — it reads two reg args (eax, ecx) and a stack arg, ending with plain `ret`. Function identity is correct; the label is just imprecise.
- **"Hidden-node skip at 0x4D2C3A"** — the `setne dl` is PROVEN; its semantic role as a "skip gate" is a simplification: the local site unconditionally calls the next function and only forwards `dl` as a flag. The gate is in the callee `0x48B540`, not at `0x4D2C3A`.
- **Submesh field claim (+0x154 / +0x158 / +0x15C)** — the three offsets ARE referenced extensively (430/517/334 raw matches) in `.text`, but **not** in the first ~400B of the walker body `0x49CCE0` or in `0x49BE00/0x49D380`'s first ~768B. The remaster's specific "strip indices / FLOAT16 position / per-triangle area" labelling and the "many meshes don't have them → area=0 → no emission" diagnostic is **UNVERIFIED** by this run. It is INFERRED consistent, not PROVEN.
- **Xbox analog** — all five addresses exist as entry stubs with structurally parallel usage (same `+0x1E0` root pointer walk, same 3-arg invocation). The claim that Xbox "reads vertex buffers instead" is UNVERIFIED here (would require tracing the thunk-target bodies).

## Adversarial pass — strongest reasons to doubt even this partial confirmation

1. **The load-bearing claim is the one I couldn't verify.** The remaster's technical thesis is: PC emitter divides by (surface area) measured off CPU-side submesh fields that most jungle-canopy meshes don't populate → 0 → no particles. I verified the plumbing but did NOT verify the actual field reads at +0x154/+0x158/+0x15C, nor that they are null on canopy meshes, nor that the surface-area value is used as a divisor/factor that gates particle count. Everything upstream could match the remaster's description and the diagnosis still be wrong at the leaves. This is where a follow-on probe should concentrate — read the body of `0x49D380` (and chain into whatever reads a submesh pointer and does the `+0x154/+0x158/+0x15C` loads), and verify the dependency chain back to a float that if 0 produces 0 particles.
2. **`0x4D2C3A setne dl` is not a jump.** The remaster's prose ("skips hidden nodes") naturally suggests a conditional branch at that site; there isn't one. A reader of the remaster source could easily patch the wrong instruction (nop the `setne`, or flip the compare) and get a result that looks like it worked for the wrong reason.
3. **`size=8` Xbox stubs** could be red-herring addresses. The stubs are PROVEN entry points, but all meaningful behavior lives at the thunk targets — this verification did not look at them. If the thunk resolves to something other than the function the remaster assumes, the Xbox parallel breaks silently.
4. **Allocator ABI label.** `0x84AE70` behaves like a fastcall-ish allocator, not stdcall-2. If the remaster patches a call site to it using stdcall calling convention assumptions, the stack discipline will be wrong and the game will crash — this is a concrete, test-at-boot failure mode worth checking before shipping.
5. **No independent cross-method confirmation.** All ten signatures agree, but one method (direct byte read of the PE) was used for all of them. This can't catch a systemic error in the method (e.g., wrong ImageBase assumption — though sections and `[0xDF7B10]` global refs do self-consistently check out).

## Not verified

- The remaster's specific labels for `+0x154` (strip indices pointer), `+0x158` (FLOAT16 position pointer), `+0x15C` (per-triangle area pointer).
- That the surface area actually divides particle count / gates emission to 0 — the arithmetic chain between the walker result and the particle count was not traced.
- That the Xbox path "reads vertex buffers instead" — the trampoline targets `FUN_82861BD4`, `FUN_82861BCC`, `FUN_82861BC0`, and the real bodies behind `FUN_821EA888` / `FUN_821EB2A8` were not disassembled.
- 1.0 cross-check (as the project is on 1.1).
- PS3 analog (not in scope).
- Behavior of the downstream dispatcher `0x48B540` that consumes the `dl` flag from `0x4D2C3A`.
- Whether `[esi+0x20]` at `0x4D2C29` is in fact the "node hidden" field vs. some other 32-bit node property.
