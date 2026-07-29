/* BUG-009 — `Pg.UnloadLayer` dereferences NULL on a co-op client with no current game state.
 *
 * WHAT THE PLAYER SEES
 *   A crash to desktop, on a co-op CLIENT only. The host never hits it.
 *
 * WHY
 *   `Pg.UnloadLayer` opens with a `Net.IsClient` gate and then immediately reads the current game
 *   state — with no null check between them:
 *
 *     0x005D4E49  cmp  byte ptr [0x00DFBD77], 0     ; Net.IsClient
 *     0x005D4E53  je   0x005D4E6C                   ; not a client -> skip the whole block
 *     0x005D4E55  mov  eax, dword ptr [0x01175C7C]  ; current game state
 *     0x005D4E5A  cmp  dword ptr [eax + 4], 0x7D0B162C   ; ★ eax is never tested
 *
 *   An unrecognised `Sys.RequestGameState` string leaves `DAT_01175C7C` NULL. So a client calling
 *   `Pg.UnloadLayer` while no game state is current reads `NULL+4`. Reachable, not theoretical.
 *
 * THE FIX
 *   Do the null check the function omits, before it runs. When we would fault, return zero Lua
 *   results — which is a shape the function already produces on several of its own paths (both the
 *   "no such layer" and "refused because static" branches push nil and return), so a caller is not
 *   being handed anything it could not already receive.
 *
 * ⚠ ADDRESS CORRECTIONS vs the register, from disassembling this build:
 *   The register put the `je` at 0x005D4E49 and the unguarded pair at 0x005D4E55/0x005D4E5A. The
 *   `cmp` is at 0x005D4E49 but the `je` is at 0x005D4E53 — three `push`es sit between them. The
 *   defect itself is exactly as described.
 *
 *   The entry point is 0x005D4E40, confirmed as the `Pg` binding-table slot at 0x00B99364. Its
 *   prologue is `push ebp; mov ebp,esp; and esp,-8`, so a 5-byte splice swallows the stack
 *   alignment — safe here because ebp is already saved and the epilogue restores through it.
 */
#include "../src/fixpack.h"

/* Verified against the target build; see fixpack.h for how the build is pinned. */
#define VA_PG_UNLOADLAYER   0x005D4E40u
#define VA_NET_IS_CLIENT    0x00DFBD77u   /* byte  — non-zero on a co-op client */
#define VA_CURRENT_STATE    0x01175C7Cu   /* void* — NULL when no game state is current */

typedef int(__cdecl* PgUnloadLayerFn)(void* L);
static PgUnloadLayerFn g_orig;

static int __cdecl Hook_PgUnloadLayer(void* L) {
    const unsigned char is_client = *(volatile unsigned char*)VA_NET_IS_CLIENT;
    void* const state = *(void* volatile*)VA_CURRENT_STATE;

    /* Exactly the condition the original walks into: a client, with no current state, about to read
     * [NULL+4]. Anything else — host, or a client with a live state — is untouched and runs the
     * original, because this fix must not change what already works. */
    if (is_client && state == 0) {
        m2_logf("BUG-009: refused Pg.UnloadLayer on a client with no game state (would read NULL+4)");
        return 0;   /* no Lua results; the function has push-nothing paths of its own */
    }
    return g_orig(L);
}

int bug_009_install(void) {
    return m2_hook_attach((void*)VA_PG_UNLOADLAYER, (void*)Hook_PgUnloadLayer, (void**)&g_orig);
}
