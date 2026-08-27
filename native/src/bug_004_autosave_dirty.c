/* BUG-004 — five profile setters never dirty the profile, so their changes are not autosaved.
 * See verification/BUG-004.md. */
#include "../src/fixpack.h"

#define VA_PROFILE_SINGLETON 0x01176054u   /* void** */

#define VA_SET_CASH              0x005DF480u
#define VA_SET_FUEL_CAPACITY     0x005DF720u
#define VA_SET_PROFILE_CHARACTER 0x005DF7D0u
#define VA_SET_PROFILE_COSTUME   0x005DF920u
#define VA_SET_AVAILABLE_COSTUMES 0x005DFB40u

#define PROF_OFF_DIRTY             0x11
#define PROF_OFF_CASH              0x2C
#define PROF_OFF_FUEL_CAPACITY     0x30C
#define PROF_OFF_PROFILE_CHARACTER 0x61
#define PROF_OFF_PROFILE_COSTUME   0x63
#define PROF_OFF_AVAILABLE_COSTUMES 0x25E
#define PROF_OFF_SECOND_GATE       0x25F

typedef int(__cdecl* SetterFn)(void* L);

static SetterFn g_orig_cash;
static SetterFn g_orig_fuel_cap;
static SetterFn g_orig_character;
static SetterFn g_orig_costume;
static SetterFn g_orig_costume_roster;

static long g_reported_gate;

static unsigned char* Profile(void) {
    return *(unsigned char* volatile*)VA_PROFILE_SINGLETON;
}

static void MarkDirty(unsigned char* prof) {
    prof[PROF_OFF_DIRTY] |= 1;

    /* ⚠ Whether the second gate at +0x25F is ever open decides if this module does anything; log once. */
    if (InterlockedCompareExchange(&g_reported_gate, 1, 0) == 0) {
        unsigned char gate = prof[PROF_OFF_SECOND_GATE];
        m2_logf("BUG-004: dirtied the profile; second autosave gate [+0x25F] = %u%s",
                (unsigned)gate,
                gate ? "" : "  <-- ZERO: autosave is still gated shut, this fix is INERT");
    }
}

/* Read the field, run the original, re-read, dirty only on a real change. Comparing (not dirtying
 * unconditionally) handles the undocumented optional bool arg that suppresses the write. */
#define DEFINE_DWORD_SETTER_HOOK(name, orig, field)                     \
    static int __cdecl name(void* L) {                                  \
        unsigned char* prof = Profile();                                \
        int before = prof ? *(volatile int*)(prof + (field)) : 0;       \
        int r = orig(L);                                                \
        prof = Profile();                     /* ⚠ re-read: it may not survive the call */ \
        if (prof && *(volatile int*)(prof + (field)) != before) MarkDirty(prof); \
        return r;                                                       \
    }

#define DEFINE_BYTE_SETTER_HOOK(name, orig, field)                      \
    static int __cdecl name(void* L) {                                  \
        unsigned char* prof = Profile();                                \
        unsigned char before = prof ? prof[(field)] : 0;                \
        int r = orig(L);                                                \
        prof = Profile();                                               \
        if (prof && prof[(field)] != before) MarkDirty(prof);           \
        return r;                                                       \
    }

DEFINE_DWORD_SETTER_HOOK(Hook_SetCash, g_orig_cash, PROF_OFF_CASH)
DEFINE_DWORD_SETTER_HOOK(Hook_SetFuelCapacity, g_orig_fuel_cap, PROF_OFF_FUEL_CAPACITY)
DEFINE_BYTE_SETTER_HOOK(Hook_SetProfileCharacter, g_orig_character, PROF_OFF_PROFILE_CHARACTER)
DEFINE_BYTE_SETTER_HOOK(Hook_SetProfileCostume, g_orig_costume, PROF_OFF_PROFILE_COSTUME)
DEFINE_BYTE_SETTER_HOOK(Hook_SetAvailableCostumes, g_orig_costume_roster,
                        PROF_OFF_AVAILABLE_COSTUMES)

int bug_004_install(void) {
    /* All five or none — a partial install leaves some edits saving and others not. */
    if (!m2_hook_attach((void*)VA_SET_CASH, (void*)Hook_SetCash, (void**)&g_orig_cash))
        return 0;
    if (!m2_hook_attach((void*)VA_SET_FUEL_CAPACITY, (void*)Hook_SetFuelCapacity,
                        (void**)&g_orig_fuel_cap))
        return 0;
    if (!m2_hook_attach((void*)VA_SET_PROFILE_CHARACTER, (void*)Hook_SetProfileCharacter,
                        (void**)&g_orig_character))
        return 0;
    if (!m2_hook_attach((void*)VA_SET_PROFILE_COSTUME, (void*)Hook_SetProfileCostume,
                        (void**)&g_orig_costume))
        return 0;
    if (!m2_hook_attach((void*)VA_SET_AVAILABLE_COSTUMES, (void*)Hook_SetAvailableCostumes,
                        (void**)&g_orig_costume_roster))
        return 0;
    return 1;
}
