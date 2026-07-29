/* BUG-008 — `Gui.ShowLoadingHints(false)` is a one-way switch.
 *
 * WHAT THE PLAYER SEES
 *   Loading hints cannot be turned off. Script asks for them to stop; they keep appearing.
 *
 * WHY
 *   The flag `[gui+0x39]` is written in exactly ONE place, and an explicit `false` branches past it:
 *
 *     0x005B4C4E  test eax, eax
 *     0x005B4C50  jg   0x005B4C5A
 *     0x005B4C52  mov  al, 1                     ; ★ an ABSENT argument defaults to TRUE
 *     0x005B4C58  jmp  0x005B4C62
 *     0x005B4C5A  mov  al, byte ptr [esp+0x10]
 *     0x005B4C5E  test al, al
 *     0x005B4C60  je   0x005B4C86                ; ★ explicit false -> epilogue, before the store
 *     0x005B4C62  mov  ecx, dword ptr [0x01175FB0]
 *     0x005B4C68  mov  byte ptr [ecx + 0x39], al ; the only write to the flag
 *     0x005B4C6B  cmp  byte ptr [0x00DF67F4], 0  ; ⚠ a SECOND gate, not in the register
 *     0x005B4C72  je   0x005B4C86
 *     0x005B4C81  call 0x00608590                ; the notify
 *
 *   So `true` and an omitted argument both write; `false` writes nothing. The flag can be set and
 *   never cleared. 8 Lua call sites.
 *
 * THE FIX
 *   Write the flag for `false` too — which is all the defect is.
 *
 *   Done by writing the flag ourselves and then calling the original: the original will re-write the
 *   same value on the `true`/absent paths (harmless, identical), and on the `false` path it returns
 *   early without touching what we just wrote. That keeps the original's return value, its argument
 *   handling and its notify behaviour exactly as shipped, rather than reimplementing a function
 *   whose epilogue we would then have to match.
 *
 * ⚠ THE NOTIFY IS DELIBERATELY NOT FORCED.
 *   The register says a correct build "also needs the notify at 0x005B4C74 made reachable". This
 *   build shows why that is not ours to force: an UNDOCUMENTED gate at 0x005B4C6B —
 *   `cmp byte ptr [0x00DF67F4], 0; je` — sits between the store and the notify, so the notify is
 *   conditional on a global whose meaning we have not established. Forcing it would be firing a
 *   subsystem notification under a condition the game deliberately excludes, on a guess.
 *
 *   The flag is what the 8 call sites are actually asking to change, and it is now correct in both
 *   directions. If an in-game repro shows hints still displayed with the flag clear, the notify is
 *   the next thing to look at — and by then we would know what 0x00DF67F4 is.
 */
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

    /* Only the false case needs us; true and absent already work. Writing just that case keeps the
     * intercept as small as the defect. */
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
