/* FEATURE-004 — mesh-emitter fire visuals.
 *
 * Model-state scripts call `StartEmitter` (v11 0x4D2A50) with (guid, node, fx_hash). The target
 * emitter runs a mesh-surface-area walker over the referenced submesh graph and multiplies the
 * computed area into a particle count. For canopies, trees and wrecks whose submesh has no
 * per-triangle area array at `+0x15C`, the walker's null-check at 0x49D003 / 0x49D725 skips
 * every submesh, the fp accumulator stays 0.0f, cvttss2si at 0x49BCF5 clamps to 0, and the
 * particle count is 0 — no fire renders despite the emitter firing. The AREA submesh loader
 * near 0x4787D4 actively zeroes `+0x15C` and never refills it (see verification/FEATURE-004.md
 * for the four PROVEN byte-level findings).
 *
 * Three independent remedies, each gated by its own code-match Expect():
 *   A — Hidden-node flag: at v11 0x4D2C3A overwrite `setne dl` with `mov dl, 1 ; nop` so the
 *       `include-hidden-nodes` flag passed into 0x48B540 is always 1. StartEmitter mesh walks
 *       no longer skip model nodes marked hidden.
 *   B — Walker return-value fallback: patch the 5-byte call at 0x48ADBF (walker) and 0x48FED8
 *       (emit-by-shape) to redirect through our thunks. When the walker returns 0, derive an
 *       AABB from the shape's model-node mesh chain and spawn particles at random points inside
 *       it.
 *   C — AREA submesh area build: patch the 5-byte call at 0x4787D4 so after the original index
 *       copy populates `+0x154` and zeroes `+0x15C`, we compute per-triangle areas from the
 *       strip indices and FLOAT16 positions and store them back at `+0x15C`.
 *
 * All three are v11-only. The patch sites are verified against the shipped v1.1 Mercenaries2.exe
 * (MD5 5b9976f162e050f4adcc51bb997ba97f).
 */
#include "../src/fixpack.h"

#include <windows.h>
#include <d3d9.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VA_SETNE_HIDDEN         0x004D2C3Au
#define VA_WALKER_CALL          0x0048ADBFu
#define VA_WALKER_FN            0x0049BC90u
#define VA_EMIT_CALL            0x0048FED8u
#define VA_EMIT_FN              0x0048AF50u
#define VA_AREA_CALL            0x004787D4u
#define VA_IDXCOPY_FN           0x0085C070u
#define VA_AREA_RET             0x004787D9u
#define VA_ALLOCATOR            0x0084AE70u

#define SHAPE_OFF_IN_EFFECT     0x770u
#define SHAPE_OFF_AT            0x7C4u
#define SHAPE_OFF_OWNER         0x10u
#define SHAPE_OFF_DEF           0x7E4u

static const unsigned char A_WANT[8]       = {0x0F, 0x95, 0xC2, 0xD9, 0x1C, 0x24, 0x50, 0x51};
static const unsigned char A_PATCH[3]      = {0xB2, 0x01, 0x90};
static const unsigned char CALL_OPCODE     = 0xE8;
static const unsigned char AREA_PREFIX[11] = {0x80, 0x7C, 0x24, 0x60, 0x00, 0x74, 0x28,
                                              0x8B, 0x54, 0x24, 0x18};
static const unsigned char AREA_RET_SIG[10] = {0x5F, 0x5E, 0x5D, 0x5B, 0x83, 0xC4, 0x4C,
                                               0xC2, 0x08, 0x00};

static int protect_rwx(void* addr, SIZE_T len, DWORD* saved) {
    return VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, saved) ? 1 : 0;
}

static void protect_restore(void* addr, SIZE_T len, DWORD saved) {
    DWORD unused;
    VirtualProtect(addr, len, saved, &unused);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
}

static int write_bytes(void* addr, const void* src, SIZE_T len) {
    DWORD saved;
    if (!protect_rwx(addr, len, &saved)) return 0;
    memcpy(addr, src, len);
    protect_restore(addr, len, saved);
    return 1;
}

static int expect_bytes(const void* addr, const void* want, SIZE_T len) {
    return memcmp(addr, want, len) == 0;
}

static int call_target(DWORD call_site) {
    unsigned char opcode = *(unsigned char*)call_site;
    if (opcode != CALL_OPCODE) return 0;
    return (int)(call_site + 5 + *(int32_t*)(call_site + 1));
}

static int patch_call_rel32(DWORD call_site, void* target) {
    unsigned char buf[5];
    int32_t rel = (int32_t)((DWORD)target - (call_site + 5));
    buf[0] = CALL_OPCODE;
    memcpy(buf + 1, &rel, 4);
    return write_bytes((void*)call_site, buf, 5);
}

/* ===== A: hidden-node flag ===== */

static int install_hidden_node_flag(void) {
    if (!expect_bytes((void*)VA_SETNE_HIDDEN, A_WANT, sizeof(A_WANT))) {
        m2_logf("[feature_004] A skipped: signature mismatch at 0x%08X", VA_SETNE_HIDDEN);
        return 0;
    }
    if (!write_bytes((void*)VA_SETNE_HIDDEN, A_PATCH, sizeof(A_PATCH))) return 0;
    m2_logf("[feature_004] A armed: hidden-node flag forced at 0x%08X", VA_SETNE_HIDDEN);
    return 1;
}

/* ===== B: volume-emitter fallback ===== */

typedef struct {
    unsigned char* shape;
    DWORD owner;
    DWORD def;
    float mn[3];
    float mx[3];
} ShapeBox;

#define NBOX 128
static ShapeBox g_box[NBOX];
static volatile LONG g_box_next;
static volatile LONG g_convert_hits;
static volatile LONG g_convert_boxes;

static int is_finite(float f) {
    return f == f && f > -1e6f && f < 1e6f;
}

static DWORD lcg_mix(DWORD a, DWORD b) {
    DWORD x = a ^ (b * 2654435761u);
    x = x * 1664525u + 1013904223u;
    return x;
}

/* Walk the model/node graph under render-instance `inst` and compute a world-space AABB of
 * every mesh under node `node_hash`. Returns 0 if the hash doesn't resolve to any node (then
 * caller falls back to the whole model). The ABI reads are hardened: anything that fails
 * IsBadReadPtr is skipped, which keeps a corrupted shape from dereferencing junk. */
static int shape_aabb_world(unsigned char* inst, DWORD node_hash, float* mn, float* mx) {
    if (!inst || IsBadReadPtr(inst, 0x2C0)) return 0;
    unsigned char* model = *(unsigned char**)(inst + 0x1E0);
    unsigned char* node_mats = *(unsigned char**)(inst + 0x2B8);
    if (!model || !node_mats || IsBadReadPtr(model, 0x64)) return 0;
    int part_count = *(int*)(model + 0x4C);
    unsigned char* parts = *(unsigned char**)(model + 0x50);
    unsigned char* nodes = *(unsigned char**)(model + 0x60);
    void** meshes = *(void***)(model + 0x58);
    if (part_count <= 0 || part_count > 4096) return 0;
    if (!parts || !nodes || !meshes || IsBadReadPtr(parts, (SIZE_T)part_count * 4)) return 0;
    const float* W = (const float*)(inst + 0x40);

    for (int pass = 0; pass < 2; pass++) {
        int kept = 0;
        mn[0] = mn[1] = mn[2] = 1e30f;
        mx[0] = mx[1] = mx[2] = -1e30f;
        for (int i = 0; i < part_count; i++) {
            unsigned char* part = parts + i * 4;
            int node_idx = *(short*)part;
            if (node_idx < 0) continue;

            if (pass == 0) {
                int walker = node_idx, matched = 0, guard = 0;
                while (walker >= 0 && guard++ < 256) {
                    unsigned char* nd = nodes + walker * 0xB0;
                    if (IsBadReadPtr(nd, 8)) break;
                    if (*(DWORD*)nd == node_hash) { matched = 1; break; }
                    walker = *(short*)(nd + 4);
                }
                if (!matched) continue;
            }

            void* mesh = meshes[part[2]];
            if (!mesh || IsBadReadPtr(mesh, 4)) continue;
            void** vt = *(void***)mesh;
            if (IsBadReadPtr(vt, 0x2C)) continue;

            float* lo; float* hi;
            {
                void* self = mesh;
                void* fn = vt[0x24 / 4];
                __asm__ volatile("call *%2" : "=a"(lo), "+c"(self) : "r"(fn)
                                : "edx", "memory", "cc");
            }
            {
                void* self = mesh;
                void* fn = vt[0x28 / 4];
                __asm__ volatile("call *%2" : "=a"(hi), "+c"(self) : "r"(fn)
                                : "edx", "memory", "cc");
            }
            if (IsBadReadPtr(lo, 12) || IsBadReadPtr(hi, 12)) continue;

            const float* N = (const float*)(node_mats + node_idx * 0x40);
            if (IsBadReadPtr(N, 0x40)) continue;

            for (int corner = 0; corner < 8; corner++) {
                float v[3] = {
                    (corner & 1) ? hi[0] : lo[0],
                    (corner & 2) ? hi[1] : lo[1],
                    (corner & 4) ? hi[2] : lo[2],
                };
                float a[3], w[3];
                for (int j = 0; j < 3; j++)
                    a[j] = v[0] * N[j] + v[1] * N[4 + j] + v[2] * N[8 + j] + N[12 + j];
                for (int j = 0; j < 3; j++)
                    w[j] = a[0] * W[j] + a[1] * W[4 + j] + a[2] * W[8 + j] + W[12 + j];
                int ok = 1;
                for (int j = 0; j < 3; j++) if (!is_finite(w[j])) { ok = 0; break; }
                if (!ok) continue;
                for (int j = 0; j < 3; j++) {
                    if (w[j] < mn[j]) mn[j] = w[j];
                    if (w[j] > mx[j]) mx[j] = w[j];
                }
            }
            kept++;
        }
        if (kept > 0) return 1;
    }
    return 0;
}

/* Called from thunk_walker when the walker returned 0. Shape at ecx is a mesh-type emitter. */
int __cdecl ef_convert(unsigned char* shape) {
    unsigned char* effect = shape - SHAPE_OFF_IN_EFFECT;
    float mn[3], mx[3];
    *(DWORD*)shape = 0;
    InterlockedIncrement(&g_convert_hits);

    unsigned char* inst = *(unsigned char**)(shape + 4);
    DWORD node_hash = *(DWORD*)(shape + SHAPE_OFF_OWNER);
    if (!shape_aabb_world(inst, node_hash, mn, mx)) return 0;

    const float* at = (const float*)(effect + SHAPE_OFF_AT);
    float cx = (mn[0] + mx[0]) * 0.5f - at[0];
    float cy = (mn[1] + mx[1]) * 0.5f - at[1];
    float cz = (mn[2] + mx[2]) * 0.5f - at[2];
    if (cx * cx + cy * cy + cz * cz > 40.f * 40.f) return 0;

    for (int j = 0; j < 3; j++) {
        float c = (mn[j] + mx[j]) * 0.5f;
        float h = (mx[j] - mn[j]) * 0.5f * 0.8f;
        if (h > 15.f) h = 15.f;
        mn[j] = c - h;
        mx[j] = c + h;
    }

    LONG slot = (InterlockedIncrement(&g_box_next) - 1) & (NBOX - 1);
    ShapeBox* b = &g_box[slot];
    b->shape = NULL;
    MemoryBarrier();
    b->owner = *(DWORD*)(effect + SHAPE_OFF_OWNER);
    b->def = *(DWORD*)(effect + SHAPE_OFF_DEF);
    for (int j = 0; j < 3; j++) { b->mn[j] = mn[j]; b->mx[j] = mx[j]; }
    MemoryBarrier();
    b->shape = shape;
    InterlockedIncrement(&g_convert_boxes);
    return 0;
}

/* Called from thunk_emit when `*shape == 0` indicates a converted emitter. Fills pos with n
 * world-space random points inside the recorded box. */
void __cdecl ef_spread(int n, float* pos, unsigned char* shape) {
    if (n <= 0 || !pos || *(DWORD*)shape != 0) return;
    unsigned char* effect = shape - SHAPE_OFF_IN_EFFECT;
    DWORD owner = *(DWORD*)(effect + SHAPE_OFF_OWNER);
    DWORD def = *(DWORD*)(effect + SHAPE_OFF_DEF);
    for (int i = 0; i < NBOX; i++) {
        ShapeBox* b = &g_box[i];
        if (b->shape != shape) continue;
        if (b->owner != owner || b->def != def) { b->shape = NULL; return; }
        DWORD r = lcg_mix((DWORD)__builtin_ia32_rdtsc(), (DWORD)(uintptr_t)pos);
        int cap = n < 4096 ? n : 4096;
        for (int k = 0; k < cap; k++) {
            for (int j = 0; j < 3; j++) {
                r = r * 1664525u + 1013904223u;
                float t = (float)(r >> 8) * (1.0f / 16777216.0f);
                pos[k * 3 + j] = b->mn[j] + (b->mx[j] - b->mn[j]) * t;
            }
        }
        return;
    }
}

/* Trampoline target for the ORIGINAL `call 0x49BC90` instruction we overwrite. Set at install. */
static void* g_orig_walker_call_target;
/* Trampoline target for the ORIGINAL `call 0x48AF50` instruction we overwrite. */
static void* g_orig_emit_call_target;

__attribute__((naked, used))
static void thunk_walker(void) {
    __asm__ volatile(
        "push %%ecx\n\t"
        "pushl 0x18(%%esp)\n\t"
        "pushl 0x18(%%esp)\n\t"
        "pushl 0x18(%%esp)\n\t"
        "pushl 0x18(%%esp)\n\t"
        "pushl 0x18(%%esp)\n\t"
        "call *%0\n\t"
        "add $0x14, %%esp\n\t"
        "pop %%ecx\n\t"
        "test %%eax, %%eax\n\t"
        "jg 1f\n\t"
        "mov (%%ecx), %%edx\n\t"
        "cmp $1, %%edx\n\t"
        "je 2f\n\t"
        "cmp $4, %%edx\n\t"
        "je 2f\n\t"
        "cmp $5, %%edx\n\t"
        "jne 1f\n\t"
        "2:\n\t"
        "push %%ecx\n\t"
        "call _ef_convert\n\t"
        "add $4, %%esp\n\t"
        "xor %%eax, %%eax\n\t"
        "1:\n\t"
        "ret\n\t"
        : : "m"(g_orig_walker_call_target));
}

__attribute__((naked, used))
static void thunk_emit(void) {
    __asm__ volatile(
        "cmpl $0, (%%ecx)\n\t"
        "jne 3f\n\t"
        "mov 4(%%esp), %%edx\n\t"
        "test %%edx, %%edx\n\t"
        "je 4f\n\t"
        "cmpl $0, (%%edx)\n\t"
        "jne 3f\n\t"
        "4:\n\t"
        "xor %%eax, %%eax\n\t"
        "ret $0x1c\n\t"
        "3:\n\t"
        "push %%ecx\n\t"
        "push %%eax\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "pushl 0x24(%%esp)\n\t"
        "mov 0x1c(%%esp), %%eax\n\t"
        "mov 0x20(%%esp), %%ecx\n\t"
        "call *%0\n\t"
        "push %%eax\n\t"
        "pushl 8(%%esp)\n\t"
        "pushl 8(%%esp)\n\t"
        "push %%eax\n\t"
        "call _ef_spread\n\t"
        "add $12, %%esp\n\t"
        "pop %%eax\n\t"
        "add $8, %%esp\n\t"
        "ret $0x1c\n\t"
        : : "m"(g_orig_emit_call_target));
}

static int install_volume_fallback(void) {
    if (*(unsigned char*)VA_WALKER_CALL != CALL_OPCODE ||
        *(unsigned char*)VA_EMIT_CALL != CALL_OPCODE ||
        (DWORD)call_target(VA_WALKER_CALL) != VA_WALKER_FN ||
        (DWORD)call_target(VA_EMIT_CALL) != VA_EMIT_FN) {
        m2_logf("[feature_004] B skipped: call-site mismatch (walker=%d emit=%d)",
                (int)(*(unsigned char*)VA_WALKER_CALL == CALL_OPCODE),
                (int)(*(unsigned char*)VA_EMIT_CALL == CALL_OPCODE));
        return 0;
    }
    g_orig_walker_call_target = (void*)VA_WALKER_FN;
    g_orig_emit_call_target = (void*)VA_EMIT_FN;
    if (!patch_call_rel32(VA_WALKER_CALL, (void*)thunk_walker)) return 0;
    if (!patch_call_rel32(VA_EMIT_CALL,   (void*)thunk_emit))   return 0;
    m2_logf("[feature_004] B armed: volume-emitter fallback (walker 0x%08X, emit 0x%08X)",
            VA_WALKER_CALL, VA_EMIT_CALL);
    return 1;
}

/* ===== C: AREA submesh area build ===== */

static float half_to_float(WORD h) {
    DWORD sign = (DWORD)(h >> 15) << 31;
    DWORD exp = (h >> 10) & 31;
    DWORD mant = h & 1023;
    DWORD bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            int k = -1;
            do { k++; mant <<= 1; } while ((mant & 1024) == 0);
            mant &= 1023;
            bits = sign | ((DWORD)(112 - k) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float r;
    memcpy(&r, &bits, 4);
    return r;
}

static WORD float_to_half(float v) {
    if (!(v > 0)) return 0;
    if (v >= 65504.f) return 0x7BFF;
    DWORD bits;
    memcpy(&bits, &v, 4);
    int exp = (int)((bits >> 23) & 255) - 127 + 15;
    DWORD mant = bits & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return 0;
        mant |= 0x800000u;
        return (WORD)(mant >> (14 - exp));
    }
    return (WORD)((exp << 10) | (mant >> 13));
}

static void* game_alloc(DWORD size) {
    void* r;
    DWORD fn = VA_ALLOCATOR;
    __asm__ volatile(
        "push $1\n\t"
        "push %2\n\t"
        "call *%1\n\t"
        "add $8, %%esp\n\t"
        : "=a"(r) : "r"(fn), "r"(size), "a"(0), "c"(0)
        : "edx", "memory", "cc");
    return r;
}

static volatile LONG g_area_meshes;
static volatile LONG g_area_fixed_pos;

/* After the original index copy runs, build the per-triangle area array at `+0x15C`. The strip
 * is read as a triangle fan — triangle i uses indices [i, i+1, i+2]; cuts and bad indices store
 * area 0. The vertex positions may ignore the POSITION element offset the Xbox honours;
 * reconstruct from the vertex stream when the decl says the offset is non-zero. */
void __cdecl ef_build_area(unsigned char* E, const unsigned char* vb_stream, const unsigned char* ib_stream) {
    (void)ib_stream;
    if (IsBadReadPtr(E, 0x18)) return;
    int tri_count = *(int*)E;
    int vtx_count = *(int*)(E + 4);
    const WORD* idx = *(const WORD**)(E + 0xC);
    WORD* pos = *(WORD**)(E + 0x10);
    InterlockedIncrement(&g_area_meshes);
    if (!idx || !pos || tri_count < 3 || vtx_count <= 0 || *(void**)(E + 0x14)) return;

    unsigned char* sub = E - 0x148;
    IDirect3DVertexDeclaration9* decl = *(IDirect3DVertexDeclaration9**)(sub + 0x118);
    unsigned char* decl_desc = *(unsigned char**)(sub + 0x110);
    if (decl && decl_desc && vb_stream && !IsBadReadPtr(decl_desc, 0x10)) {
        D3DVERTEXELEMENT9 elems[MAXD3DDECLLENGTH + 1];
        UINT nelems = MAXD3DDECLLENGTH + 1;
        if (SUCCEEDED(decl->lpVtbl->GetDeclaration(decl, elems, &nelems))) {
            for (UINT k = 0; k < nelems && elems[k].Stream != 0xFF; k++) {
                if (elems[k].Usage == D3DDECLUSAGE_POSITION &&
                    elems[k].UsageIndex == 0 &&
                    elems[k].Type == D3DDECLTYPE_FLOAT16_4) {
                    DWORD stride = *(DWORD*)(decl_desc + 8);
                    DWORD off = elems[k].Offset;
                    if (off && stride >= off + 6 &&
                        !IsBadReadPtr(vb_stream, (SIZE_T)(vtx_count - 1) * stride + off + 6)) {
                        for (int v = 0; v < vtx_count; v++)
                            memcpy(pos + v * 3, vb_stream + v * stride + off, 6);
                        InterlockedIncrement(&g_area_fixed_pos);
                    }
                    break;
                }
            }
        }
    }

    WORD* area = (WORD*)game_alloc((DWORD)tri_count * 2);
    if (!area) return;
    for (int i = 0; i < tri_count; i++) {
        float a = 0.f;
        if (i + 2 < tri_count) {
            WORD i0 = idx[i], i1 = idx[i + 1], i2 = idx[i + 2];
            if (i0 != 0xFFFF && i1 != 0xFFFF && i2 != 0xFFFF &&
                i0 < vtx_count && i1 < vtx_count && i2 < vtx_count) {
                WORD trip[3] = {i0, i1, i2};
                float p[3][3];
                for (int t = 0; t < 3; t++)
                    for (int j = 0; j < 3; j++)
                        p[t][j] = half_to_float(pos[trip[t] * 3 + j]);
                float u[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
                float w[3] = {p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]};
                float cx = u[1] * w[2] - u[2] * w[1];
                float cy = u[2] * w[0] - u[0] * w[2];
                float cz = u[0] * w[1] - u[1] * w[0];
                float q = cx * cx + cy * cy + cz * cz;
                if (q > 0.f) a = 0.5f * sqrtf(q);
            }
        }
        area[i] = float_to_half(a);
    }
    MemoryBarrier();
    *(WORD**)(E + 0x14) = area;
}

static void* g_orig_idxcopy_target;
static void* g_resume_area_return = (void*)VA_AREA_RET;

/* Replaces the `call 0x85C070` at 0x4787D4. The loader passes (strm, ibuf) stdcall; we let it
 * run, then call ef_build_area with the submesh descriptor base from edi (which the loader
 * holds across the call as its submesh pointer), using the same (strm, ibuf) it just consumed. */
__attribute__((naked, used))
static void thunk_area(void) {
    __asm__ volatile(
        "pop %%eax\n\t"
        "call *%0\n\t"
        "pushal\n\t"
        "pushfl\n\t"
        "pushl 0x40(%%esp)\n\t"
        "pushl 0x40(%%esp)\n\t"
        "push %%edi\n\t"
        "call _ef_build_area\n\t"
        "add $12, %%esp\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *%1\n\t"
        : : "m"(g_orig_idxcopy_target), "m"(g_resume_area_return));
}

static int install_area_build(void) {
    if (*(unsigned char*)VA_AREA_CALL != CALL_OPCODE ||
        (DWORD)call_target(VA_AREA_CALL) != VA_IDXCOPY_FN) {
        m2_logf("[feature_004] C skipped: idxcopy call-site mismatch at 0x%08X", VA_AREA_CALL);
        return 0;
    }
    if (!expect_bytes((void*)(VA_AREA_CALL - 0x2A), AREA_PREFIX, sizeof(AREA_PREFIX)) ||
        !expect_bytes((void*)VA_AREA_RET, AREA_RET_SIG, sizeof(AREA_RET_SIG))) {
        m2_logf("[feature_004] C skipped: loader prefix/epilogue mismatch");
        return 0;
    }
    g_orig_idxcopy_target = (void*)VA_IDXCOPY_FN;
    if (!patch_call_rel32(VA_AREA_CALL, (void*)thunk_area)) return 0;
    m2_logf("[feature_004] C armed: AREA submesh area build at 0x%08X", VA_AREA_CALL);
    return 1;
}

/* ===== install ===== */

int feature_004_install(void) {
    if (fixpack_build() != FIXPACK_BUILD_V11) {
        m2_logf("FEATURE-004  skipped (v11-only)");
        return FIXPACK_SKIPPED;
    }
    int a = install_hidden_node_flag();
    int b = install_volume_fallback();
    int c = install_area_build();
    if (!a && !b && !c) return FIXPACK_FAILED;
    m2_logf("[feature_004] armed: A=%d B=%d C=%d", a, b, c);
    return FIXPACK_ARMED;
}
