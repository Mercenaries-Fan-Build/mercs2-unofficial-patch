/* BUG-008 — `Gui.ShowLoadingHints(false)` is a one-way switch: the flag is written in exactly one
 * place and an explicit `false` branches past it, so hints turn on but never off.
 * See verification/BUG-008.md. */
#include "../src/fixpack.h"

#define VA_SHOW_LOADING_HINTS 0x005B4C30u
#define VA_GUI_SINGLETON      0x01175FB0u   /* void** */
#define GUI_OFF_HINTS_FLAG    0x39

typedef int(__cdecl* ShowLoadingHintsFn)(void* L);
static ShowLoadingHintsFn g_orig;

static int __cdecl Hook_ShowLoadingHints(void* L) {
    unsigned char* gui;
    int want = 1;      /* an absent argument means TRUE — the original's own default */
    int present;

    present = m2_lua_arg_bool(L, 0, &want);
    if (!present) want = 1;

    /* Only the false case needs us; true and absent already write. We clear the flag and let the
     * original run — ⚠ we do NOT force the notify past its undocumented gate at [0x00DF67F4]. */
    if (!want) {
        gui = *(unsigned char* volatile*)VA_GUI_SINGLETON;
        if (gui) gui[GUI_OFF_HINTS_FLAG] = 0;
    }
    return g_orig(L);
}

int bug_008_install(void) {
    return m2_hook_attach((void*)VA_SHOW_LOADING_HINTS, (void*)Hook_ShowLoadingHints,
                          (void**)&g_orig);
}
