# FEATURE-004 — mesh-emitter fire visuals

**Status:** experimental, OFF by default (`FEATURE_004 = 1` in `scripts/unofficial_patch.ini` to enable).
**Build:** v1.1 only. The three remedies are gated by in-place byte-signature checks; if the exe
doesn't match, that remedy skips and the others still arm independently.

## The missing behaviour

On PS3/Xbox, model-state scripts that start fires on canopies, trees and wrecks produce
visible flames through the particle emitter attached to the destruction state. On PC the
emitter runs but no particles render — the user sees the model fade to charred stumps with no
fire in between. Affected examples:

- `jungle_env_largecanopy01` (the big destructible canopy that acts as a wall near the PMC
  driveway). HP 4000, model hash `0xFF7ABB3B`.
- Burnable trees on both the full-size and far LOD.
- Vehicle wrecks whose destruction script attaches a mesh-type fire emitter.

Palms (`global_env_palmtreebend*`) burn correctly on PC today because their fire emitter is a
point emitter, not a mesh emitter. Everything else that uses a mesh emitter is the bug set.

## Mechanism (PROVEN against v1.1 Mercenaries2.exe MD5 `5b9976f162e050f4adcc51bb997ba97f`)

The particle system computes the emitter's particle count per frame as
`cvttss2si(surface_area_fp)` clamped to a configurable cap. `surface_area_fp` comes from a
mesh-surface-area walker (entry `0x49BC90`, body `0x49CCE0`) that iterates the submesh graph
under one model node and reads a per-triangle area array at `submesh+0x15C`:

```
0049D003  mov eax, [submesh + 0x15C]
0049D022  cmp eax, 0
0049D024  je  <advance submesh>
```

If `+0x15C` is null, the walker skips that submesh entirely — no accumulator write. The
accumulator (`xmm0 ^ xmm0` at `0x49BC97`) stays `0.0f`, which `cvttss2si` at `0x49BCF5`
converts to integer `0`. Zero particles.

The AREA submesh loader at `0x478200..0x4787E2` (containing the call site at `0x4787D4`)
reaches an index-copy at `0x85C070` to populate `submesh+0x154` (strip indices). The copy's
internal body at `0x85C07D` does `mov dword ptr [edi+0x14], 0` — i.e. **the loader actively
zeroes `submesh+0x15C`** and never refills it. No code path on PC populates `+0x15C` for an
AREA submesh.

On consoles the emitter reads vertex buffers directly instead of this precomputed array
(unverified at depth; based on structural parity in the X360 decomp — the Xbox walker reads
`*(*(instance + 0x1e0) + 4)` the same way PC's walker does, but the leaf dereference chain was
not traced into. See [notes_emitfix_leaf_drill.md](notes_emitfix_leaf_drill.md).)

Additionally, the StartEmitter mesh walk at `0x4D2A50` passes a `include-hidden-nodes` flag
derived from the model-state template, computed as `setne dl` against `[esi+0x20]` at
`0x4D2C3A`:

```
004D2C3A  0F 95 C2        setne dl
```

For every mesh emitter whose state-Enter marks the fire node hidden, the walker skips those
nodes. On consoles the equivalent flag is forced on (unverified; INFERRED from behaviour).

## The three remedies

### A — Hidden-node flag (3-byte patch)

Overwrite the `setne dl` at `0x4D2C3A` with `mov dl, 1 ; nop` so the hidden-node skip is
disabled for every StartEmitter invocation. The flag is always `1`.

Signature check (abort if mismatched): `0F 95 C2 D9 1C 24 50 51` at `0x4D2C3A`.
Patch bytes: `B2 01 90`.

Risk: if any emitter case legitimately needs hidden-node skipping to look correct, this
changes it. No case has been identified where this matters — but the possibility exists.

### B — Walker return-value fallback (two 5-byte call rewrites)

Patch the 5-byte `call 0x49BC90` at `0x48ADBF` so the game dispatches through our walker
thunk instead. The thunk forwards the normal 5-stack-arg ABI plus `ecx = shape` to the
original walker, inspects the return value, and if it's `≤ 0` AND the shape type at `[ecx+0]`
is `1`, `4`, or `5` (mesh-type), calls our `EfConvert`:

1. Walks the model/node graph under the shape's render instance (shape+4) to find the
   world-space AABB of every mesh under the shape's node (shape+0x10).
2. Keeps the box only if its centre is within 40 world units of the effect anchor at
   `effect+0x7C4`.
3. Shrinks the box to 80% of its half-extent per axis, capped at 15 units, to stay inside
   the foliage silhouette.
4. Records `(shape, owner=effect+0x10, def=effect+0x7E4, mn[3], mx[3])` in a 128-slot ring
   buffer.
5. Overwrites `*shape = 0` to mark the shape as "converted — spread by our emit thunk".

Patch the 5-byte `call 0x48AF50` at `0x48FED8` so the emit stage goes through our emit thunk.
The thunk reads `*shape`; if `0` and the shape is in our table, it fills the N-particle
output positions with random points in the recorded box (xorshift-LCG mix of RDTSC and the
output address). Otherwise it falls through to the original emit-by-shape.

Risk: 128-slot ring is per-frame bounded. If more than 128 unique converted shapes are live
in one frame the oldest wraps and renders at its last known transform until the next
walker-return=0 refreshes it. In practice there are usually a handful of concurrent
foliage/wreck burns.

### C — AREA submesh area build (one 5-byte call rewrite)

Patch the 5-byte `call 0x85C070` at `0x4787D4` so after the original index-copy runs — which
populates `submesh+0x154` (strip indices) and zeroes `submesh+0x158` and `+0x15C` — we build
the per-triangle area array ourselves and store it at `+0x15C`:

1. Each strip triangle `i` uses indices `[i, i+1, i+2]` from `+0x154`.
2. Decode three `FLOAT16` positions per index from `+0x158`.
3. Area = `|cross(b-a, c-a)| / 2`. 0 if any index is `0xFFFF` (strip cut) or out of range.
4. Encode as `FLOAT16` per triangle and store at a freshly allocated buffer via the game
   allocator at `0x84AE70`; write the buffer pointer to `[submesh+0x15C]`.
5. If the submesh's vertex declaration says `POSITION` has a non-zero offset (which PC's
   index-copy ignores), re-populate `+0x158` from the vertex stream at that offset. Honours
   `D3DDECLTYPE_FLOAT16_4` only — mesh fire emitters all use that type in shipped content.

Risk: the game allocator's exact ABI isn't fully proven. The verifier called it "takes `eax +
ecx + 1 stack arg`, plain `ret`"; our stub pushes `size` and a flag `1`, zeroes `eax` and
`ecx`, and reads the result from `eax`. If this is wrong the allocation call fails (returns
null), we abort the triangle fill (leaving `+0x15C` null), and B's volume-fallback picks up
the slack. Fail-safe by design.

Signature checks: `0x4787D4` opcode `E8`, target `0x85C070`; `0x4787D4 - 0x2A` prefix
`80 7C 24 60 00 74 28 8B 54 24 18`; `0x4787D9` epilogue `5F 5E 5D 5B 83 C4 4C C2 08 00`.

## How the three interact

- **A alone** can fix emitters whose model nodes are hidden. For canopy01 this is not the
  dominant case — canopy01's fire node is visible.
- **B alone** restores visible fire for every mesh emitter whose walker returns 0 — this is
  the general-case fix. Covers canopy01, trees, wrecks.
- **C alone** fills `+0x15C` so the walker returns a real area. When this works, B's
  fallback doesn't trigger. The visible result is the same; C is more faithful to the
  authored content.

Running all three gives the broadest coverage and the most faithful behaviour on meshes that
do have an AREA chunk. The pack arms each independently; a signature mismatch in one does
not disarm the others.

## Attribution

The byte-level findings this fix is built on were discovered via comparing notes with the
Mercenaries2Remastered project (`https://github.com/Koloses/Mercenaries2Remastered`,
`dll/src/emitfix.c`). That codebase is GPL-v3; this patch is MIT and is written from the
findings as a specification, not from their source. See
[notes_emitfix_verification.md](notes_emitfix_verification.md) for the structural confirmation
pass and [notes_emitfix_leaf_drill.md](notes_emitfix_leaf_drill.md) for the four byte-level
proofs that the leaf claim holds against the on-disk PC v1.1 exe. Earlier research into the
cross-platform fire subsystem — before the renderer-level bug was identified — is preserved in
[notes_fire_subsystem_x360.md](notes_fire_subsystem_x360.md),
[notes_fire_subsystem_ps3.md](notes_fire_subsystem_ps3.md),
[notes_flammable_crossplat_diff.md](notes_flammable_crossplat_diff.md), and
[notes_startemitter_caller_map.md](notes_startemitter_caller_map.md). Those lines of inquiry
dead-ended because the differentiator was not where we initially suspected, but the maps are
useful anchors for future cross-platform work (e.g. exemap seeding).

## Open follow-ups

- **Live measurement**: a diagnostic probe that hooks the walker return and logs the walker
  area + the final particle count per shape per frame. Would independently confirm the
  before/after effect without relying on visual inspection.
- **Verify the X360 vertex-buffer-direct-read claim**: not needed to ship the PC fix, but the
  cross-platform picture is incomplete without it.
- **Shape/effect struct offsets** (`shape+4` = render instance, `shape+0x10` = node hash,
  `effect+0x7C4` = anchor position, `effect+0x10` = owner, `effect+0x7E4` = def): these are
  inherited from the research source without independent static verification. If something
  looks wrong at runtime, these are the first offsets to audit.
- **Game allocator ABI**: the "takes eax + ecx + 1 stack arg, plain ret" description from
  the verifier isn't a full ABI proof. If C's allocations fail at runtime, this is the first
  thing to inspect.
