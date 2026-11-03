/* FEATURE-003 — restore the visible chain on the winch magnet.
 *
 * On PS3/Xbox the vehicle winch renders a visible chain between the winch head and the magnet
 * cargo attachment. On PC it doesn't. Investigation (see note
 * mercs2-console-parity-three-feature-investigation-2026-10-15 and verification/FEATURE-003.md)
 * proved PC and console use *different* implementations: PC builds a Havok constraint chain
 * (hkpBallSocketChainData + hkpConstraintChainInstance) via FUN_00443ff0, with no attached visual.
 * Console uses a skinned bone chain posed from a `WinchBone` Lua cfunc that PC doesn't ship.
 *
 * Approach for this release: ride on top of PC's existing Havok chain. Hook FUN_00443ff0's return
 * and iterate the rigid bodies the function just built, so we have a per-link transform source.
 * The actual mesh-instancing step is DEFERRED until PROBE-001 output (verification/PROBE-001.md,
 * scenario #3) confirms the segment count, the chain's resting length, and whether a chain mesh
 * ships on PC that we can reuse.
 *
 * ⚠ This seed does NOT yet instance a mesh. It records the per-chain state so the next iteration
 * can turn it on without re-deriving the shape of FUN_00443ff0's output.
 */
#include "../src/fixpack.h"

#include <stddef.h>

/* v11 addresses. */
#define VA_HAVOK_CHAIN  0x00444030u  /* Havok rigid-body chain builder */
#define VA_ATTACH_WINCH 0x005D1FD0u  /* Lua cfunc AttachCargoToWinch */

/* Record of the most recent chain the game built. Replaced on every build; a second winch would
 * overwrite the first — fine for the seed pass since vehicles usually only have one magnet
 * active. */
typedef struct {
    void*        instance;        /* `this` passed to FUN_00443ff0 */
    unsigned int seg_count;       /* [this+0x50] at return */
    unsigned int build_fires;     /* how many times FUN_00443ff0 ran since arm */
    unsigned int attach_fires;    /* how many times Lua AttachCargoToWinch ran */
} ChainRecord;

static ChainRecord g_chain;

typedef int (__attribute__((fastcall))* ThisCallI0_t)(void* this_ptr, void* edx);
typedef int (__cdecl* LuaCFn_t)(void* L);

static ThisCallI0_t g_orig_havok_chain;
static LuaCFn_t     g_orig_attach_winch;

static int __attribute__((fastcall)) Hook_HavokChain(void* this_ptr, void* edx) {
    int ret = g_orig_havok_chain(this_ptr, edx);
    g_chain.build_fires++;
    g_chain.instance = this_ptr;
    if (this_ptr) {
        unsigned char* p = (unsigned char*)this_ptr;
        g_chain.seg_count = *(volatile unsigned int*)(p + 0x50);
    } else {
        g_chain.seg_count = 0;
    }
    m2_logf("[feature_003] havok-chain built #%u this=%p segs=%u",
            g_chain.build_fires, g_chain.instance, g_chain.seg_count);
    /* NEXT ITERATION — once a scenario confirms mesh name + segment transform offsets, iterate
     * `seg_count` rigid bodies at `(this+0x54)` or equivalent and attach a per-link mesh. */
    return ret;
}

static int __cdecl Hook_AttachCargoToWinch(void* L) {
    g_chain.attach_fires++;
    m2_logf("[feature_003] AttachCargoToWinch Lua trigger #%u L=%p",
            g_chain.attach_fires, L);
    return g_orig_attach_winch(L);
}

int feature_003_install(void) {
    if (fixpack_build() != FIXPACK_BUILD_V11) {
        m2_logf("FEATURE-003  skipped (v11-only; current build is not v11)");
        return FIXPACK_SKIPPED;
    }
    if (!m2_hook_attach((void*)VA_HAVOK_CHAIN,  (void*)Hook_HavokChain,
                        (void**)&g_orig_havok_chain))  return 0;
    if (!m2_hook_attach((void*)VA_ATTACH_WINCH, (void*)Hook_AttachCargoToWinch,
                        (void**)&g_orig_attach_winch)) return 0;
    return 1;
}
