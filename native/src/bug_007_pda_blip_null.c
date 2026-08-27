/* BUG-007 — three PDA blip binders dereference NULL on a stale or out-of-range blip id. Crash, not a
 * glitch. See verification/BUG-007.md. */
#include "../src/fixpack.h"

#define VA_ADD_PDA_MAP_BLIPS 0x005BCE70u
#define VA_UPDATE_PDA_BLIP   0x005BCF90u
#define VA_REMOVE_PDA_BLIP   0x005BD0C0u
#define VA_GUI_SINGLETON     0x01175FB0u   /* void** — the Gui object */

#define GUI_OFF_BLIP_MANAGER 0x68
#define MGR_OFF_SLOT_ARRAY   0x44
#define MGR_OFF_SLOT_COUNT   0x4C

typedef int(__cdecl* BlipFn)(void* L);

static BlipFn g_orig_add;
static BlipFn g_orig_update;
static BlipFn g_orig_remove;

/* Would the original fault for the blip id in argument 1? Returns 1 only when the manager resolved
 * AND the id is unusable; ⚠ every uncertain outcome returns 0 so the original runs unchanged. */
static int WouldFault(void* L) {
    unsigned char* gui;
    unsigned char* mgr;
    void** slots;
    int count, id;
    double raw;

    if (!m2_lua_arg_number(L, 0, &raw)) return 0;   /* not a number: the original's own type check */
    id = (int)raw;

    gui = *(unsigned char* volatile*)VA_GUI_SINGLETON;
    if (!gui) return 0;
    mgr = *(unsigned char* volatile*)(gui + GUI_OFF_BLIP_MANAGER);
    if (!mgr) return 0;
    slots = *(void** volatile*)(mgr + MGR_OFF_SLOT_ARRAY);
    count = *(volatile int*)(mgr + MGR_OFF_SLOT_COUNT);
    if (!slots || count <= 0 || count > 0x10000) return 0;   /* implausible: do not trust it */

    /* ⚠ `id == count` is a real over-read: the slot array has zero slack. */
    if (id < 0 || id >= count) return 1;

    /* In range but empty: a deleted widget's slot, reachable because ids get reused. */
    return slots[id] == 0;
}

static int __cdecl Hook_Add(void* L) {
    if (WouldFault(L)) return 0;
    return g_orig_add(L);
}

static int __cdecl Hook_Update(void* L) {
    if (WouldFault(L)) return 0;
    return g_orig_update(L);
}

static int __cdecl Hook_Remove(void* L) {
    if (WouldFault(L)) return 0;
    return g_orig_remove(L);
}

int bug_007_install(void) {
    /* All three or none — a partial install leaves one binder still crashing. */
    if (!m2_hook_attach((void*)VA_ADD_PDA_MAP_BLIPS, (void*)Hook_Add, (void**)&g_orig_add))
        return 0;
    if (!m2_hook_attach((void*)VA_UPDATE_PDA_BLIP, (void*)Hook_Update, (void**)&g_orig_update))
        return 0;
    if (!m2_hook_attach((void*)VA_REMOVE_PDA_BLIP, (void*)Hook_Remove, (void**)&g_orig_remove))
        return 0;
    return 1;
}
