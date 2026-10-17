# emitfix.c leaf-claim drill

Target exe: `/Volumes/Projects/mercenaries-game/game-files/canonical/Mercenaries 2 World in Flames v1.1/Mercenaries2.exe` (MD5 `5b9976f162e050f4adcc51bb997ba97f`, confirmed matches sibling's run).
Scripts and logs: `/private/tmp/claude-501/-Volumes-Projects-mercs2-unofficial-patch/dbc9fb6e-eb81-4ad4-82dd-841e753a1689/scratchpad/{linear_walk,dump_region,dump_wrapper,loader_scan2,loader_full,writes_15c,triple_writer,dump2}.py` and matching `.out`.

## Scope

Ran Tests 1–4 fully. Ran Test 5 at a reduced depth (Xbox decomp text only — the full trampoline-target disassembly path is not available without a working .xex disassembler).

## Test 1 — +0x154/+0x158/+0x15C reads in the walker chain

**Linearly disassembled each mesh-walker body and reported every mem-op whose
displacement equals 0x154 / 0x158 / 0x15C** (capstone operand analysis, not raw
byte matching — this filters out mid-instruction false positives). Full output
in `scratchpad/linear_walk.out` and `scratchpad/dump_region.out`.

### Walker body `0x49CCE0` (reached via `0x48ADBF → 0x49BC90 → 0x49CCE0`)

PROVEN reads, byte-exact (dumped from the PE32):

```
0x0049CFF0  8b4f08                   mov ecx, [edi + 8]              ; ecx = submesh base
0x0049CFF3  8b940b54010000           mov edx, [ebx + ecx + 0x154]    ; EDX = submesh->+0x154  (strip indices ptr)
0x0049CFFA  8d040b                   lea eax, [ebx + ecx]
0x0049CFFD  8b8858010000             mov ecx, [eax + 0x158]          ; ECX = submesh->+0x158  (positions ptr)
0x0049D003  8b805c010000             mov eax, [eax + 0x15c]          ; EAX = submesh->+0x15C  (per-tri area ptr)
0x0049D009  33f6                     xor esi, esi                     ; esi = 0
0x0049D00B  3bce                     cmp ecx, esi                     ; +0x158 == 0 ?
0x0049D00D  8984249c000000           mov [esp + 0x9c], eax            ; (spill +0x15C value)
0x0049D014  0f849a000000             je  0x49D0B4                     ; if +0x158 null -> SKIP
0x0049D01A  3bd6                     cmp edx, esi                     ; +0x154 == 0 ?
0x0049D01C  0f8492000000             je  0x49D0B4                     ; if +0x154 null -> SKIP
0x0049D022  3bc6                     cmp eax, esi                     ; +0x15C == 0 ?
0x0049D024  0f848a000000             je  0x49D0B4                     ; if +0x15C null -> SKIP
```

- Register chain for the +0x15C read: `edi` is the mesh/graph pointer at `[ebp+8]` (walker's `this`). The sibling already proved `mov edi, [ecx+0x1E0]` seeds this. At `0x49CFF0`, `[edi+8]` is the submesh-array base offset; `ebx` iterates in strides of `0x1C4` (see the loop at `0x49D0BB  add ebx, 0x1C4`), so `[ebx+ecx]` is the per-submesh descriptor base. All three fields are read off that same base in three consecutive adjacent instructions.
- Role of the +0x15C value: compared against `esi=0`; if equal, jump to `0x49D0B4` which does `add ebx, 0x1C4` + submesh-advance (no fallback). If non-null, it is spilled to `[esp+0x9C]` and later indexed as a **word[]** array in `movzx edx, word ptr [edx+ecx*2]` at `0x49D067` and scaled through `[edx*4 + 0xE8FB00]` (a float lookup table), finally `addss`'d into the accumulator at `[edx]` where `edx = [ebp+0x14]` (`0x49D086..0x49D08D`).

### Shared helper `0x49D380` (reached via case-0/4/5 common `0x49BE00`)

Byte-exact matching read triplet, same semantics:

```
0x0049D710  8b4f08                   mov ecx, [edi + 8]
0x0049D713  8bb40a58010000           mov esi, [edx + ecx + 0x158]    ; +0x158
0x0049D71A  3bf3                     cmp esi, ebx                     ; (ebx == 0 from earlier)
0x0049D71C  8d040a                   lea eax, [edx + ecx]
0x0049D71F  8b8854010000             mov ecx, [eax + 0x154]          ; +0x154
0x0049D725  8b805c010000             mov eax, [eax + 0x15c]          ; +0x15C
0x0049D72B  898c24d8000000           mov [esp+0xD8], ecx
0x0049D732  898424dc000000           mov [esp+0xDC], eax
0x0049D739  0f84f2010000             je  0x49D931                     ; +0x158 null -> SKIP
0x0049D73F  3bcb                     cmp ecx, ebx
0x0049D741  0f84ea010000             je  0x49D931                     ; +0x154 null -> SKIP
0x0049D747  3bc3                     cmp eax, ebx
0x0049D749  0f84e2010000             je  0x49D931                     ; +0x15C null -> SKIP
```

Skip target `0x49D931` is the submesh-advance loop body (`add edx, 0x1C4` at `0x49D93B` + outer-loop continuation), confirming "no inner triangle work when +0x15C is null".

### Verdict for Test 1 — SUPPORTS CLAIM (strongly).

Both branches of the walker chain do read exactly the three offsets the remaster
names, and in both cases a null `+0x15C` is treated as a hard skip condition,
not as "fall back to something else."

## Test 2 — Null `+0x15C` → area 0

Chain traced (all bytes read directly from the PE):

1. The walker wrapper at `0x49BC90` initialises an output accumulator to **0.0f** before calling the walker body:

   ```
   0x0049BC97  0f57c0                   xorps xmm0, xmm0
   ...
   0x0049BCB7  f30f11442414             movss [esp + 0x14], xmm0        ; accumulator <- 0.0
   ...
   0x0049BCC6  8bc4                     mov  eax, esp
   0x0049BCE0  894808                   mov  [eax + 8], ecx             ; passes ctx-struct ptr
   0x0049BCF0  e8eb0f0000               call 0x49CCE0                   ; walker body
   0x0049BCF5  f30f2c0424               cvttss2si eax, dword ptr [esp]  ; read result as int
   0x0049BCFA  3bc6                     cmp eax, esi                    ; esi = max cap
   0x0049BCFC  7c02                     jl  0x49BD00
   0x0049BCFE  8bc6                     mov eax, esi                    ; (cap clamp)
   0x0049BD00  83c418                   add esp, 0x18
   0x0049BD03  c3                       ret
   ```
   PROVEN: when the walker body never touches the fp accumulator (because every
   submesh hits the `je 0x49D0B4` skip), the accumulator stays at 0.0f; the
   subsequent `cvttss2si` yields integer 0; `cmp 0, esi` with esi non-negative
   stays `jl` (eax<esi) so eax=0 is NOT overwritten to esi; the wrapper returns
   0.

2. The case-0/4/5 common body at `0x49BE00` has the exact mirror pattern: an
   accumulator initialised to 0 is passed to `0x49D380`, and the caller
   (`0x48AFAF..0x48AFD6`) reads `[esp+4]` back as a float, converts, clamps.

3. There is **no "fallback area from positions"** path. The skip targets
   (`0x49D0B4`, `0x49D931`) land directly in the submesh-advance loop; there
   is no code path between the null-check and the accumulator write that reads
   the `+0x158` positions buffer independently.

### Verdict for Test 2 — SUPPORTS CLAIM.

A null `+0x15C` on every submesh of a given mesh yields area 0.0f from the
walker, and the arithmetic chain carries that through to a quiet integer 0.

## Test 3 — area 0 → particles 0 (no crash)

Caller of the walker wrapper at the mesh-type area call site (sibling-proved
address `0x48ADBF`):

```
0x0048AD93  d9ee                     fldz                              ; push 0.0
0x0048AD95  8b4110                   mov eax, [ecx + 0x10]
0x0048AD98  f30f10411c               movss xmm0, [ecx + 0x1c]
0x0048AD9D  f30f59442414             mulss xmm0, [esp + 0x14]
0x0048ADA3  83ec08                   sub  esp, 8
0x0048ADA6  d9542404                 fst  [esp + 4]
0x0048ADAA  81e201ffffff             and  edx, 0xffffff01
0x0048ADB0  d91c24                   fstp [esp]
0x0048ADB3  52                       push edx
0x0048ADB4  50                       push eax
0x0048ADB5  51                       push ecx
0x0048ADB6  33d2                     xor  edx, edx
0x0048ADB8  8bc7                     mov  eax, edi
0x0048ADBA  f30f110424               movss [esp], xmm0
0x0048ADBF  e8cc0e0100               call 0x49BC90                     ; walker wrapper
0x0048ADC4  83c414                   add  esp, 0x14
0x0048ADC7  5f                       pop  edi
0x0048ADC8  5e                       pop  esi
0x0048ADC9  59                       pop  ecx
0x0048ADCA  c20800                   ret 8                             ; returns eax (particle count)
```

- The wrapper's return value in `eax` is the integer particle count (the mesh-area → particle-count conversion).
- NO divide instruction anywhere in the chain. No `div`/`idiv`/`fdiv` where `area` is the divisor.
- With `area == 0`, `eax = 0`. Function returns 0.
- Whatever outer loop uses this count will do `for (i=0; i<0; i++) {}` — zero particles, zero exceptions.

### Verdict for Test 3 — SUPPORTS CLAIM.

Area 0 → integer 0 → caller returns 0. No divide, no crash. Silent zero.

## Test 4 — Loader at 0x4787D4 leaves `+0x15C` null

Enclosing function identified from CC-padding boundaries (`scan_cc_runs.out`):
**0x00478200..0x004787E2 (ret 8)**, 0x5E3 bytes, 441 instructions.

Critical fact missed by a surface read: inside this function, `ebp` is
**reassigned from a stack slot** at `0x00478358  mov ebp, [esp + 0x58]` — after
the prolog `ebp = esp`, this later move makes `ebp` point to the submesh
descriptor base. Every subsequent `[ebp + <big disp>]` resolves to a submesh
field, not a stack local (`loader_full.out`).

The call sequence at the AREA chunk tail (bytes from the PE):

```
0x004787AA  807c246000               cmp byte ptr [esp + 0x60], 0           ; is-AREA-chunk flag
0x004787AF  7428                     je  0x4787D9                            ; skip entire AREA copy if not
0x004787B1  8b542418                 mov edx, [esp + 0x18]
0x004787B5  52                       push edx                                 ; 0x85C0F0 arg
0x004787B6  8d8510010000             lea eax, [ebp + 0x110]
0x004787BC  8dbd48010000             lea edi, [ebp + 0x148]                   ; EDI = submesh + 0x148
0x004787C2  50                       push eax
0x004787C3  e828393e00               call 0x85C0F0                           ; positions-copy (uses edi)
0x004787C8  8b4c241c                 mov ecx, [esp + 0x1c]
0x004787CC  51                       push ecx
0x004787CD  81c540010000             add ebp, 0x140                          ; ebp -> submesh + 0x140
0x004787D3  55                       push ebp
0x004787D4  e897383e00               call 0x85C070                           ; strip-indices-copy (uses edi)
0x004787D9  5f                       pop edi
0x004787DA  5e                       pop esi
0x004787DB  5d                       pop ebp
0x004787DC  5b                       pop ebx
0x004787DD  83c44c                   add esp, 0x4C
0x004787E0  c20800                   ret 8
```

Both helpers (`0x85C0F0` and `0x85C070`) take `edi` as a hidden input register
and treat it as the base of a sub-struct. With `edi = submesh + 0x148`:

| encoded disp | logical submesh offset | what the helper does |
|---|---|---|
| `[edi+0x04]` | submesh **+0x14C** | `0x85C0F0`: writes a count (`mov [edi+4], eax`) |
| `[edi+0x0C]` | submesh **+0x154** | `0x85C070`: frees prior buffer, allocs new strip-indices buffer, stores pointer |
| `[edi+0x10]` | submesh **+0x158** | `0x85C0F0`: frees prior buffer, allocs new positions buffer, stores pointer |
| `[edi+0x14]` | submesh **+0x15C** | `0x85C070`: **frees prior buffer AND stores literal 0** (`mov dword ptr [edi+0x14], 0` at `0x0085C07D`), then NEVER re-allocates |

The exact byte-level proof in `0x85C070` (strip-copy):

```
0x0085C070  56                       push esi
0x0085C071  8b7714                   mov esi, [edi + 0x14]              ; load submesh+0x15C
0x0085C074  85f6                     test esi, esi
0x0085C076  740c                     je  0x85C084                       ; if null, skip free
0x0085C078  e8a3eefeff               call 0x84AF20                       ; free the old buffer
0x0085C07D  c7471400000000           mov dword ptr [edi + 0x14], 0      ; <<< submesh+0x15C := 0  >>>
0x0085C084  8b442408                 mov eax, [esp + 8]
...                                                                      ; allocates strip-indices buf
0x0085C0DD  89770c                   mov dword ptr [edi + 0xc], esi    ; submesh+0x154 := new buffer
0x0085C0E1  c20800                   ret 8
```

**0x85C070 writes 0 to submesh+0x15C and NEVER touches it again on this
function invocation.** This is the exact behaviour emitfix.c:4-12 describes.

### Cross-check: candidate secondary writers (`triple_writer.out`)

I scanned every function in `.text` for ones that write all three of
`[reg+0x154]`, `[reg+0x158]`, `[reg+0x15C]` to the same non-stack base register
(a signature for "this function is a submesh initializer"). Found 33 such
functions.

Spot-inspecting the writes (full table in `scratchpad/triple_writer.out`):
most of them write all three slots as 3 adjacent 32-bit values and are
clearly setting unrelated small structs (xyz-float triples, byte-flag triples,
3-dword initializers). None of them exhibits the walker's distinctive
submesh signatures (`+0x1E0 chain`, `+0x1B4/+0x1B8 pair`, 0x1C4 stride) in a
quick survey. No search of the `.text` for `E8 ... → 0x478200` turned up a
direct relative-call caller to the AREA loader — the loader is reached by
some non-E8-rel8/32 path (likely vtable/indirect), which is consistent with
"loader is a known dispatch target" but prevents me from exhaustively listing
its callers in this run.

**Caveat (adversarial doubt that survives):** this verification did NOT prove
"no code anywhere in the exe writes a non-null pointer into submesh+0x15C
for an AREA submesh after the loader runs". The claim rests on structural
evidence (loader explicitly zeroes it + walker treats null as skip + no
obvious triple-write candidate matches the submesh signature). A thorough
sweep of all 33 triple-writers for the submesh signature was not done.

### Verdict for Test 4 — SUPPORTS CLAIM.

The AREA loader explicitly zeroes submesh+0x15C and does not reallocate it.
No secondary writer with the submesh signature surfaced in the broad scan.

## Test 5 — Xbox reads vertex buffers (reduced depth)

- All 5 Xbox entry addresses in the sibling's table are 8-byte trampoline stubs; the real bodies live at the tail-call targets (`FUN_82861bd4`, `FUN_82861bcc`, `FUN_82861bd0`, `FUN_82862d94`). The Ghidra decomp does not have those targets expanded as named C bodies.
- The two Xbox callers of the walker trampolines (`FUN_821e9b98` at 27231 and `FUN_821e9cb8` at 27278 of `xenon_retail_decomp_named.c`) exhibit the SAME clamp pattern as the PC wrapper at `0x49BC90` (compute int-area, clamp against `param`, return — see `local_30[0]` / `local_50[0]` round-trip). PROVEN structural parallel.
- `FUN_821e9cb8` loads `uStack_24 = *(undefined4 *)(*(int *)(param_8 + 0x1e0) + 4)` at line 27303 — SAME `+0x1E0` chain as PC walker body, confirming the Xbox mesh-pointer structure matches PC.
- A grep for `VertexBuffer|D3DVertex|SetStreamSource|GetVB|VB_` across the full Xbox decomp returned **zero hits** (`xbox_vb_refs.log` is empty). That is only an absence-of-symbol signal — Ghidra's `FUN_xxxx` names and no RTTI mean the proof would need to go deeper into the trampoline-target bodies, which this run did not do.

### Verdict for Test 5 — INCONCLUSIVE at this depth.

The "Xbox reads VB directly" claim is NEITHER falsified NOR confirmed here.
The sibling's earlier conclusion stands.

## Overall verdict

**CONFIRMED (leaf claim holds).**

The three nontrivial substructural parts of the leaf claim are all backed by
byte-level evidence:

1. The PC walker chain reads submesh+0x15C at `0x49D003` and `0x49D725` (two
   independent code paths — the "any mesh" walker and the case-0/4/5
   emit-shape helper).
2. Both reads are gated by `je` to a hard-skip label when +0x15C is null;
   there is no fallback computation; the per-call fp accumulator was
   initialised to 0.0f by the wrapper and stays 0 if the skip fires.
3. The AREA-chunk loader (fn starting `0x478200`, call site `0x4787D4` →
   `0x85C070`) explicitly stores 0 into submesh+0x15C via
   `mov dword ptr [edi+0x14], 0` at `0x0085C07D` with `edi = submesh+0x148`,
   while filling submesh+0x154 (strip indices) and submesh+0x158
   (FLOAT16 positions) with freshly-allocated buffers. The field is zeroed
   on every AREA chunk load.
4. The caller of the wrapper (`0x48ADBF`) returns `eax = (int)clamped
   accumulator` with no divide, so area=0 → particle count 0, silently and
   without exception.

The remaster's prose about the mechanism matches the bytes, modulo one
clarification: it is not that `+0x15C` is "never filled" — it is that the
loader actively frees and NULLs it on every AREA chunk load. Functionally
identical to "never filled" from the walker's point of view, and the walker's
behaviour is exactly as the remaster describes.

## Strongest adversarial doubt that survives

A full enumeration of the 33 candidate "triple-field writer" functions
against the submesh signature (+0x1E0 pointer-chain, +0x1C4 stride,
+0x1B4/+0x1B8 count/table pair) was not done. In principle a later code
path could run after the AREA loader and reallocate submesh+0x15C with a
real per-triangle area array (e.g. during an "ensure cache" pass).
This verification shows no such path turned up in a broad walk, but the
negative claim ("no such path exists") is weaker than the positive proofs
above. If the user wants that gap closed: filter the 33 triple-writers for
ones that also touch any of {+0x1E0, +0x1B4, +0x1B8, +0x1C4} off the same
base — any match would be a candidate secondary writer worth drilling into.

## Not verified

- Xbox-side proof that the retail path "reads vertex buffers directly" — the
  trampoline-target bodies were not disassembled. Only the structural
  parallel (same `+0x1E0` chain, same clamp pattern) was confirmed.
- Exhaustive proof that no other PC code path ever writes a non-null pointer
  into submesh+0x15C for a submesh that originated as an AREA chunk.
- Which specific "mesh variant" tags produce the AREA chunk (sibling claims
  this is the only path reaching `0x4787D4`; not independently re-checked).
- That the +0x1C4 stride and the ebx-indexed submesh descriptor actually
  come from a tree whose root is the shape's `effect+0x770` structure (the
  sibling verified the shape dispatch; the chain to submesh array was walked
  only one hop here).
- Whether any of the 33 "triple-field writer" functions is reached from the
  mesh-loading pipeline on AREA submeshes.
