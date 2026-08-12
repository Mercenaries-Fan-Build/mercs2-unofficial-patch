/* BUG-004 — five profile setters never mark the profile dirty, so their changes are not autosaved.
 *
 * WHAT THE PLAYER SEES
 *   A change to cash, fuel capacity, profile character, profile costume or the available-costume
 *   roster — ALONE, with nothing else touched — is silently not saved.
 *
 * WHY
 *   The profile singleton at [0x01176054] carries a dirty byte at +0x11, and the autosave is gated
 *   on it:
 *
 *     0x0061488C  mov  eax, [0x01176054]
 *     0x00614891  cmp  byte ptr [eax + 0x11], 0    ; dirty?
 *     0x00614895  je   0x006148CE                  ;   no -> skip the save
 *     0x00614897  cmp  byte ptr [eax + 0x25F], 0   ; second gate (see the warning below)
 *     0x0061489E  je   0x006148CE
 *     0x006148C2  call 0x00634460                  ; ★ the save
 *
 *   The well-behaved setters compare-then-`setne`-then-`or`, so they dirty only on a real change:
 *     SetFuel           0x005DF64E..57   cmp/mov/setne/or byte [eax+0x11], dl
 *     SetProfileUpgrade 0x005DF8C3..D0   movzx/cmp/setne/or byte [eax+0x11], dl
 *   and so do AddCash (0x005DF56C) and AddFuel (0x005DF6D8).
 *
 *   These five contain no `or byte ptr [.. + 0x11]` at all — every one is a bare store:
 *     SetCash             entry 0x005DF480   mov [eax+0x2C],  edx   (dword)
 *     SetFuelCapacity     entry 0x005DF720   mov [ecx+0x30C], eax   (dword)
 *     SetProfileCharacter entry 0x005DF7D0   mov [ecx+0x61],  al    (byte)
 *     SetProfileCostume   entry 0x005DF920   mov [ecx+0x63],  al    (byte)
 *     SetAvailableCostumes entry 0x005DFB40  mov [ecx+0x25E], al    (byte)
 *
 * THE FIX
 *   Read the field, run the original, read it again, and dirty only if it actually changed — the
 *   same rule the well-behaved setters follow.
 *
 *   Comparing rather than dirtying unconditionally is not fussiness. `SetCash` and `SetFuel` take an
 *   undocumented optional second boolean that SUPPRESSES the write entirely; dirtying unconditionally
 *   would mark the profile changed when nothing had been, and force a save of nothing. Comparing
 *   handles that case for free, with no need to model the argument.
 *
 * ⚠ ENTRY POINTS, not the addresses in the register. The register cites the STORE inside each body
 *   (0x005DF4FE, 0x005DF778, 0x005DF828, 0x005DF978, 0x005DFB98). Those are not hookable. Each entry
 *   below was taken from its `Player` binding-table slot, which is the only pointer to it in the
 *   image.
 *
 * ⚠ SECOND GATE at +0x25F, ANDed with the dirty byte, gates the same save. Whether it is ever
 *   non-zero at runtime — i.e. whether dirtying +0x11 does anything — could not be settled statically
 *   (the save path runs through a SecuROM thunk), so MarkDirty logs it once. Observed = 1: gate open,
 *   fix effective. The log stays as a per-build check (= 0 means inert on that build).
 *   See verification/BUG-004.md.
 */
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

static long g_reported_gate;   /* log the +0x25F finding once, not once per transaction */

static unsigned char* Profile(void) {
    return *(unsigned char* volatile*)VA_PROFILE_SINGLETON;
}

static void MarkDirty(unsigned char* prof) {
    prof[PROF_OFF_DIRTY] |= 1;

    /* The one observation that decides whether this whole module does anything. Logged once. */
    if (InterlockedCompareExchange(&g_reported_gate, 1, 0) == 0) {
        unsigned char gate = prof[PROF_OFF_SECOND_GATE];
        m2_logf("BUG-004: dirtied the profile; second autosave gate [+0x25F] = %u%s",
                (unsigned)gate,
                gate ? "" : "  <-- ZERO: autosave is still gated shut, this fix is INERT");
    }
}

/* dword-field setters: read, run, re-read, dirty on a real change. */
#define DEFINE_DWORD_SETTER_HOOK(name, orig, field)                     \
    static int __cdecl name(void* L) {                                  \
        unsigned char* prof = Profile();                                \
        int before = prof ? *(volatile int*)(prof + (field)) : 0;       \
        int r = orig(L);                                                \
        prof = Profile();                     /* never assume it survived the call */ \
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
    /* All five or none: a partial install would leave some profile edits saving and others not,
     * which is harder to diagnose than the original bug. */
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
