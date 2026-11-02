/* fixpack.h — the contract every bug module implements. main.c reads the .ini and calls these. */
#ifndef FIXPACK_H
#define FIXPACK_H

#include "m2.h"

/* Install this module's hooks. ⚠ On failure the module MUST leave the game exactly as it found
 * it — a half-installed detour set is worse than none. Return codes:
 *   1  armed      — all hooks attached successfully.
 *   0  failed     — a hook refused to attach; something is wrong.
 *  -1  skipped    — module intentionally does not apply to this build. Not an error; log line
 *                   already emitted by the module.
 */
typedef int (*fixpack_install_fn)(void);
#define FIXPACK_ARMED   1
#define FIXPACK_FAILED  0
#define FIXPACK_SKIPPED (-1)

typedef struct {
    const char* id;        /* register id, e.g. "BUG-004"; also the .ini key `bug_004 = 1` */
    const char* summary;   /* one line for the log — what a player notices */
    fixpack_install_fn install;
    int enabled;           /* from the .ini; default ON */
} FixpackModule;

/* The pack targets the v11-patched-securom retail build only. On a mismatch we refuse to arm
 * any module rather than overwrite the first 5 bytes of whatever happens to live at that VA. */
typedef enum {
    FIXPACK_BUILD_UNKNOWN = 0,
    FIXPACK_BUILD_V11,     /* v11-patched-securom — the official 1.1 patch, SecuROM intact */
} FixpackBuild;

#define FIXPACK_V11_SIZE_OF_IMAGE 53944080u    /* 0x03371F10 */
#define FIXPACK_V11_CHECKSUM      0x00A4F080u
#define FIXPACK_TARGET_IMAGE_BASE 0x00400000u

/* Classifies the currently loaded Mercenaries2.exe. Cached after the first call. */
FixpackBuild fixpack_build(void);

/* 1 iff the current build is the recognised pin. */
int fixpack_target_is_supported(void);

int bug_004_install(void);   /* profile setters that never dirty the autosave flag */
int bug_008_install(void);   /* Gui.ShowLoadingHints(false) is a one-way switch */
int bug_009_install(void);   /* Pg.UnloadLayer NULL-derefs on a co-op client */

/* Console-parity work. Not bug fixes — these restore PS3/Xbox behaviour that PC never shipped.
 * See verification/FEATURE-003.md / verification/FEATURE-004.md. */
int feature_001_install(void);/* visible red laser beam (Phase 1 smoke test; D3D9 EndScene hook) */
int feature_003_install(void);/* winch magnet chain visual (seed form; mesh-attach pending probe) */
int feature_004_install(void);/* mesh-emitter fire visuals (hidden-node flag + volume fallback + AREA build) */

/* NOT SHIPPED — BUG-005, BUG-007, BUG-011. Real and confirmed; neither has a v11 fix derivable
 * from static evidence alone, and a detour built on a guess corrupts a live process rather than
 * failing.
 *
 *   BUG-005 (SetAllWeapons drops weapons / re-classes primaries): blocker cleared — the applier's
 *     slot mapping was captured live 2026-08-11. Fix not yet written/verified. See verification/BUG-005.md.
 *
 *   BUG-007 (three PDA blip binders NULL-deref): originally shipped v10-only; v11 blip binders
 *     were refactored and offsets are unverified. The v11 build may already perform the
 *     validate-and-skip the v10 fix provided — needs a live null-blip crash capture on v11 to
 *     decide whether a fix is still needed and, if so, where to attach. See verification/BUG-007.md.
 *
 *   BUG-011 (Object.GetInfiniteAmmo absent): the read primitive is identified
 *     (`FUN_00520EF0(esi=0x00DF9B10, eax=object)`), but adding a binding needs either an `.rdata`
 *     table write (the SecuROM-anti-tampered class this `.text`-only SDK avoids) or a runtime
 *     `luaL_register` path the pack does not have yet.
 */

#endif /* FIXPACK_H */
