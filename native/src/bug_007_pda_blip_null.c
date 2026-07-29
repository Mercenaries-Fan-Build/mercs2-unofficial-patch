/* BUG-007 — three PDA blip binders dereference NULL. Crash, not a glitch.
 *
 * WHAT THE PLAYER SEES
 *   A crash to desktop around the PDA map — closing and reopening it with a stale blip id is the
 *   reported trigger.
 *
 * WHY
 *   All three binders resolve a blip the same way: `[gui+0x68]` is the blip manager, `+0x44` its
 *   slot array, `+0x4C` the slot count. On the bounds-check failure path all three do `xor eax,eax`,
 *   control MERGES, and the very next instruction dereferences the null they just made:
 *
 *     AddPdaMapBlips  0x005BCE70   fails at 0x005BCEE8 -> faults 0x005BCEEF
 *     UpdatePdaBlip   0x005BCF90   fails at 0x005BD008 -> faults 0x005BD00F
 *     RemovePdaBlip   0x005BD0C0   fails at 0x005BD135 -> faults 0x005BD139
 *
 *   each `cmp dword ptr [eax + 0x10], 6`, i.e. a read of linear address 0x00000010.
 *
 *   There is no null check because the author folded "not a flash widget => NULL" into branchless
 *   arithmetic (`setne bl / sub ebx,esi / and ebx,eax`) to pass 0 to the callee — and overlooked
 *   that reading the tag at +0x10 dereferences before that arithmetic ever runs. Every other widget
 *   binder does `test reg,reg / jne` first.
 *
 *   TWO reaching conditions, not one: the id is out of range, OR it is in range but names a slot
 *   holding NULL — reachable because ids are reused, and a deleted widget clears its slot. The slot
 *   array has ZERO slack (`push 0x200`, cap 0x80), so `id == cap` is a real 4-byte heap over-read
 *   whose garbage pointer is then dereferenced.
 *
 * THE FIX
 *   Validate the blip id the way the binders should have, before letting them run: in range, and
 *   the slot actually occupied. When it is not, return zero Lua results instead of faulting.
 *
 * ★ FAIL TOWARDS THE ORIGINAL, NOT TOWARDS BLOCKING. If the manager chain cannot be resolved — an
 *   unexpected layout, the PDA not yet constructed — this calls the original rather than refusing.
 *   Refusing on a chain we failed to read would silently break every blip call in the game, which is
 *   a far worse outcome than the crash being fixed. We only ever intercept when we have positively
 *   established that the original WOULD fault.
 */
#include "../src/fixpack.h"

/* Verified against the target build. */
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

/* Would the original fault for the blip id in argument 1?
 *
 * Returns 1 only when we positively resolved the manager AND the id is unusable. Every uncertain
 * outcome returns 0, so the original runs and behaviour is unchanged.
 */
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

    /* Out of range — including `id == count`, which is the 4-byte over-read past an array with no
     * slack, not merely an off-by-one that happens to read a zero. */
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
    /* All three or none. A partial install would leave one binder still crashing while the log said
     * the fix was armed — and "which of the three" is not something a player can report. */
    if (!m2_hook_attach((void*)VA_ADD_PDA_MAP_BLIPS, (void*)Hook_Add, (void**)&g_orig_add))
        return 0;
    if (!m2_hook_attach((void*)VA_UPDATE_PDA_BLIP, (void*)Hook_Update, (void**)&g_orig_update))
        return 0;
    if (!m2_hook_attach((void*)VA_REMOVE_PDA_BLIP, (void*)Hook_Remove, (void**)&g_orig_remove))
        return 0;
    return 1;
}
