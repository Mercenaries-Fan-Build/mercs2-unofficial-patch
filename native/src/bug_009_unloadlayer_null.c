/* BUG-009 — `Pg.UnloadLayer` dereferences NULL on a co-op client with no current game state.
 * See verification/BUG-009.md. */
#include "../src/fixpack.h"

#define VA_PG_UNLOADLAYER   0x005D4E40u
#define VA_NET_IS_CLIENT    0x00DFBD77u   /* byte  — non-zero on a co-op client */
#define VA_CURRENT_STATE    0x01175C7Cu   /* void* — NULL when no game state is current */

typedef int(__cdecl* PgUnloadLayerFn)(void* L);
static PgUnloadLayerFn g_orig;

static int __cdecl Hook_PgUnloadLayer(void* L) {
    const unsigned char is_client = *(volatile unsigned char*)VA_NET_IS_CLIENT;
    void* const state = *(void* volatile*)VA_CURRENT_STATE;

    /* The exact condition the original walks into: a client, no current state, about to read [NULL+4].
     * Anything else runs the original untouched. */
    if (is_client && state == 0) {
        m2_logf("BUG-009: refused Pg.UnloadLayer on a client with no game state (would read NULL+4)");
        return 0;   /* no Lua results; the function has push-nothing paths of its own */
    }
    return g_orig(L);
}

int bug_009_install(void) {
    return m2_hook_attach((void*)VA_PG_UNLOADLAYER, (void*)Hook_PgUnloadLayer, (void**)&g_orig);
}
