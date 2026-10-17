/* PROBE-001 — console-parity diagnostic probe.
 *
 * The three parity features (laser red line, bush burn-over-time, winch magnet chain) can't be
 * confirmed statically — every "obvious" static hypothesis was adversarially falsified across seven
 * agent rounds. This module hooks the suspect engine functions and logs when they fire, so a run of
 * the game on retail PC yields the data the next iteration needs. Enable via `PROBE_001 = 1` in
 * unofficial_patch.ini and run the three scenarios named in verification/PROBE-001.md.
 *
 * All hooks are read-only: log, then tail into the trampoline. ⚠ A line out of this probe is NOT a
 * behavior change — if a hook here starts changing game state, something has drifted.
 *
 * __thiscall interop: Mercs2 internal methods pass `this` in ECX. In MinGW GCC on i686, declaring
 * the hook as `__attribute__((fastcall))` with a dummy EDX arg maps the register layout correctly
 * (ECX = arg1, EDX = arg2, stack = rest, callee pops), which matches __thiscall when the EDX slot
 * is treated as garbage.
 */
#include "../src/fixpack.h"

#include <stddef.h>

/* ========================================================================
 * Hook target VAs (retail v1.1-patched-securom, SizeOfImage=0x03371F10, CheckSum=0x00A4F080).
 * See the investigation note mercs2-console-parity-three-feature-investigation-2026-10-15.
 * ======================================================================== */

/* Feature #1 — laser beam */
#define VA_WEAPONFX_CTOR    0x0066F250u  /* builds the WeaponFX dispatch array */
#define VA_RENDER_VMETHOD   0x00666190u  /* render vmethod (candidate for laser) */
#define VA_BEAM_EFFECT_CTOR 0x00494EA0u  /* beam-effect object constructor */

/* Feature #2 — SetState dispatcher (destruction FSM; also observed in bush-burn investigation) */
#define VA_SET_STATE        0x004D3FA0u
#define VA_PG_SPAWN         0x005D5D10u  /* Pg.Spawn Lua cfunc — standard __cdecl(void* L).
                                          * Logs every spawned template name. A/B of "C4 near bush"
                                          * vs "Bunker Buster near bush" vs "Fuel Air Bomb near bush"
                                          * will show which named Explosion template the burn
                                          * pipeline is or isn't reached from. */
/* Feature #3's hooks (Havok chain, AttachCargoToWinch) live in feature_003_winch_chain.c. */

/* Rate limiter — one log line per hook per N calls after warmup, so a per-frame hook does not
 * flood the file. First N calls log unconditionally so a cold-boot scenario captures the opening
 * event sequence. */
#define WARMUP_LINES  16
#define SAMPLE_EVERY  256

typedef struct {
    unsigned int fires;      /* total times the hook fired */
    unsigned int logged;     /* total log lines emitted */
    const char*  tag;        /* label for the log line */
} RateCounter;

static int rate_should_log(RateCounter* c) {
    c->fires++;
    if (c->fires <= WARMUP_LINES) { c->logged++; return 1; }
    if ((c->fires - WARMUP_LINES) % SAMPLE_EVERY == 0) { c->logged++; return 1; }
    return 0;
}

/* ========================================================================
 * Hook trampolines.
 *
 * Each hook stores its MinHook trampoline (`g_orig_*`) and uses __fastcall(ecx, edx, …) to adapt
 * to the game's __thiscall. The Lua cfunc hook uses plain __cdecl like every other Lua cfunc hook
 * in this pack.
 * ======================================================================== */

typedef void (__attribute__((fastcall))* ThisCall0_t)(void* this_ptr, void* edx);
typedef int  (__attribute__((fastcall))* ThisCallI0_t)(void* this_ptr, void* edx);
typedef int  (__cdecl* LuaCFn_t)(void* L);

static ThisCall0_t  g_orig_weaponfx_ctor;
static ThisCallI0_t g_orig_render_vmethod;
static ThisCall0_t  g_orig_beam_effect_ctor;
static LuaCFn_t     g_orig_pg_spawn;           /* v11 Pg.Spawn Lua cfunc: standard __cdecl(void* L) */

/* SetState v11 FUN_004D3FA0 uses a CUSTOM calling convention (EDI = caller-set state slot ptr,
 * [esp+4] = state hash). Standard C ABIs don't match, so we hook via a naked assembly stub
 * that preserves every register and the stack bit-for-bit, then tail-jumps to the trampoline.
 * See HookStub_SetState below.
 *
 * ⚠ The trampoline and the logger are extern-linkage (not static) because the inline asm in
 * HookStub_SetState references them by bare symbol name — GCC otherwise elides static funcs it
 * can't see called from C, and the asm reference isn't visible to its optimiser. */
void* g_tramp_setstate;

/* ========================================================================
 * Logger shims — kept short. One line per interesting event.
 * ======================================================================== */

static RateCounter rc_weaponfx     = { 0, 0, "WeaponFX.ctor" };
static RateCounter rc_render       = { 0, 0, "Render.vmethod" };
static RateCounter rc_beam         = { 0, 0, "Beam.ctor" };
static RateCounter rc_setstate     = { 0, 0, "SetState" };
static RateCounter rc_pgspawn      = { 0, 0, "Pg.Spawn" };

/* Feature #1 — WeaponFX class ctor fills a dispatch array at &DAT_00ed3d20[DAT_01175d44] on v10.
 * The equivalent data VAs on v11 have NOT been translated yet (the exemap match graph is a
 * function-pair map and does not translate data globals), so this hook currently only logs that
 * the ctor fired. The next iteration adds a v11 data-VA scan and restores the dispatch dump. */
static void __attribute__((fastcall)) Hook_WeaponFXCtor(void* this_ptr, void* edx) {
    g_orig_weaponfx_ctor(this_ptr, edx);
    if (rate_should_log(&rc_weaponfx)) {
        m2_logf("[probe] %s this=%p (dispatch-array dump pending v11 data-VA lookup)",
                rc_weaponfx.tag, this_ptr);
    }
}

/* Feature #1 — Render vmethod. If this hook never fires in-game while a laser designator is
 * active, the laser is dispatched through a different function entirely. If it fires but the
 * beam-effect ctor never does, the type-3 branch at 0x006669BE is rejecting. */
static int __attribute__((fastcall)) Hook_RenderVMethod(void* this_ptr, void* edx) {
    if (rate_should_log(&rc_render)) {
        m2_logf("[probe] %s this=%p", rc_render.tag, this_ptr);
    }
    return g_orig_render_vmethod(this_ptr, edx);
}

/* Feature #1 — Beam-effect ctor. Reached only when the type-3 branch takes AND the pool alloc
 * returned non-null. If this fires in a laser-active scenario, the beam-effect object IS built —
 * which means the fault must be downstream in queueing or draw-state. */
static void __attribute__((fastcall)) Hook_BeamEffectCtor(void* this_ptr, void* edx) {
    if (rate_should_log(&rc_beam)) {
        m2_logf("[probe] %s this=%p", rc_beam.tag, this_ptr);
    }
    g_orig_beam_effect_ctor(this_ptr, edx);
}

/* Feature #2 — SetState. Static disasm of v11 FUN_004D3FA0 revealed a custom calling convention
 * (reconstructed from the prologue bytes before the SecuROM wrapper swallows control):
 *
 *   push ecx                          ; LOCAL slot only — ecx is never read as `this`
 *   and  byte ptr [edi + 0x10], 0xf8  ; uses EDI as a caller-set state-slot pointer BEFORE
 *                                     ; any arg load — EDI is a non-standard register arg
 *   push ebx
 *   mov  ebx, [esp + 0xC]             ; EBX = first stack arg (the state hash)
 *   push ebp
 *   push esi
 *   jmp  [0x24d3160]                  ; SecuROM dispatch — body is virtualised past here
 *   mov  [edi + 4], ebx               ; after unwrap: writes state hash to EDI[+4]
 *
 * No standard C ABI matches, so our hook is a NAKED stub that:
 *   1. Saves every GP register (pushal) and flags (pushfl) — 36 bytes.
 *   2. Reads EDI (saved at [esp+4]) and the original first stack arg (at [esp+40] after
 *      pushal+pushfl shifted the caller frame by 36, plus the 4-byte retaddr).
 *   3. Calls a __cdecl logger with those two values.
 *   4. Restores every register exactly, so the trampoline inherits EDI, ECX, stack layout
 *      unmodified from the game's call site.
 *   5. Tail-jumps to the trampoline.
 *
 * ⚠ If a GCC optimisation ever clobbered EDI across a mid-function call, SetState would crash
 * exactly the way my __fastcall interpretation did. Naked assembly is the only safe interop
 * for a register-passed-arg custom convention. */
void __cdecl ProbeLog_SetState(unsigned int arg0, unsigned int edi_at_entry) {
    if (rate_should_log(&rc_setstate)) {
        m2_logf("[probe] %s edi=0x%08X arg0=0x%08X",
                rc_setstate.tag, edi_at_entry, arg0);
    }
}

__attribute__((naked, used)) static void HookStub_SetState(void) {
    /* MinGW GCC 14 on i686-w64-mingw32 DOES use the classic leading-underscore ABI (symbol
     * dump confirms _ProbeLog_SetState, _g_tramp_setstate). Inline asm must reference the
     * mangled form. */
    __asm__ volatile (
        "pushal                     \n"  /* 32 bytes; edi lands at [esp+0] after pushal, then flags push shifts it to [esp+4] */
        "pushfl                     \n"  /* +4 bytes */
        /* Stack map now:
         *   [esp+0]  flags
         *   [esp+4]  edi  (top of pushal — pushal pushes EDI last)
         *   [esp+8]  esi
         *   [esp+36] retaddr-into-game
         *   [esp+40] arg0 (state hash slot)
         */
        "pushl 4(%esp)              \n"  /* arg2 to logger: edi value */
        "pushl 44(%esp)             \n"  /* arg1 to logger: arg0 (now at +40+4 after the push) */
        "call _ProbeLog_SetState    \n"
        "addl $8, %esp              \n"  /* __cdecl: caller cleans two 4-byte args */
        "popfl                      \n"
        "popal                      \n"  /* restores EVERY GP reg including EDI */
        "jmp *_g_tramp_setstate     \n"  /* tail into MinHook's trampoline — never returns here */
    );
}

/* Feature #2 — Pg.Spawn Lua cfunc. Logs the TEMPLATE NAME every time Lua spawns anything via
 * `Pg.Spawn("<name>", x, y, z, ...)`. The user's A/B test needs this: Bunker Buster → fire on
 * bushes; handheld C4 → no fire on bushes. The hook captures the exact template name each
 * weapon spawns so we can tell whether C4 uses "Explosion (C4)" (same as the Satellite Smart
 * Bomb finale) or a different template, and whether that template differs from the ones that
 * DO trigger fire ("Explosion (Bunker Buster Stage 2)", likely "Explosion (Fuel Air Bomb)"). */
static int __cdecl Hook_PgSpawn(void* L) {
    if (rate_should_log(&rc_pgspawn)) {
        char name[128];
        int n = m2_lua_arg_string(L, 0, name, (int)sizeof(name));
        if (n > 0) {
            m2_logf("[probe] %s template=\"%s\"", rc_pgspawn.tag, name);
        } else {
            m2_logf("[probe] %s template=<non-string arg0>", rc_pgspawn.tag);
        }
    }
    return g_orig_pg_spawn(L);
}

/* ========================================================================
 * Install — six hooks for features #1 and #2.
 *
 * The #3 (winch chain) hooks live in feature_003_winch_chain.c, not here.
 * ======================================================================== */

int probe_001_install(void) {
    if (fixpack_build() != FIXPACK_BUILD_V11) {
        m2_logf("PROBE-001  skipped (v11-only; current build is not v11)");
        return FIXPACK_SKIPPED;
    }
    if (!m2_hook_attach((void*)VA_WEAPONFX_CTOR,    (void*)Hook_WeaponFXCtor,    (void**)&g_orig_weaponfx_ctor))    return 0;
    if (!m2_hook_attach((void*)VA_RENDER_VMETHOD,   (void*)Hook_RenderVMethod,   (void**)&g_orig_render_vmethod))   return 0;
    if (!m2_hook_attach((void*)VA_BEAM_EFFECT_CTOR, (void*)Hook_BeamEffectCtor,  (void**)&g_orig_beam_effect_ctor)) return 0;
    if (!m2_hook_attach((void*)VA_SET_STATE,        (void*)HookStub_SetState,    (void**)&g_tramp_setstate))        return 0;
    if (!m2_hook_attach((void*)VA_PG_SPAWN,         (void*)Hook_PgSpawn,         (void**)&g_orig_pg_spawn))         return 0;
    m2_logf("[probe] PROBE-001 armed: WeaponFXCtor RenderVMethod BeamCtor SetState(naked) Pg.Spawn");
    return 1;
}
