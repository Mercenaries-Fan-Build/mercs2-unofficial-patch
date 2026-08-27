/* fixpack.h — the contract every bug module implements. main.c reads the .ini and calls these. */
#ifndef FIXPACK_H
#define FIXPACK_H

#include "m2.h"

/* Install this module's hooks. Returns 1 armed, 0 failed. ⚠ On failure it must leave the game exactly
 * as it found it — a half-installed detour set is worse than none. */
typedef int (*fixpack_install_fn)(void);

typedef struct {
    const char* id;        /* register id, e.g. "BUG-004"; also the .ini key `bug_004 = 1` */
    const char* summary;   /* one line for the log — what a player notices */
    fixpack_install_fn install;
    int enabled;           /* from the .ini; default ON */
} FixpackModule;

/* The pack arms only against the one build these addresses were verified on (see main.c). The three
 * fields come from that image's PE header; all three are checked because SizeOfImage alone is coarse. */
#define FIXPACK_TARGET_SIZE_OF_IMAGE 53485568u
#define FIXPACK_TARGET_CHECKSUM      0x00A5438Du
#define FIXPACK_TARGET_IMAGE_BASE    0x00400000u

int fixpack_target_is_supported(void);

int bug_004_install(void);   /* profile setters that never dirty the autosave flag */
int bug_007_install(void);   /* three PDA blip binders that dereference NULL */
int bug_008_install(void);   /* Gui.ShowLoadingHints(false) is a one-way switch */
int bug_009_install(void);   /* Pg.UnloadLayer NULL-derefs on a co-op client */

/* NOT SHIPPED — BUG-005 and BUG-011. Both real and confirmed; neither has a fix derivable from static
 * evidence alone, and a detour built on a guess corrupts a live process rather than failing.
 *
 *   BUG-005 (SetAllWeapons drops weapons / re-classes primaries): blocker cleared — the applier's
 *     slot mapping was captured live 2026-08-11. Fix not yet written/verified. See verification/BUG-005.md.
 *
 *   BUG-011 (Object.GetInfiniteAmmo absent): the read primitive is identified
 *     (`FUN_00520EF0(esi=0x00DF9B10, eax=object)`), but adding a binding needs either an `.rdata`
 *     table write (the SecuROM-anti-tampered class this `.text`-only SDK avoids) or a runtime
 *     `luaL_register` path the pack does not have yet. NEEDED: that registration path, plus the
 *     container's runtime shape (its cap/stride are not statically knowable).
 */

#endif /* FIXPACK_H */
