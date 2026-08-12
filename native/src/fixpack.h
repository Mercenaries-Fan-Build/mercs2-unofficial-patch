/* fixpack.h — the contract every bug module implements.
 *
 * One `.asi` ships, but the readable unit is one FILE PER BUG: a reader who has the register entry
 * open should be able to go straight to `bug_004_*.c` and find only BUG-004 in it. `main.c` does
 * nothing but read the .ini and call these in order.
 *
 * Each module is independently switchable at runtime, because a detour that misbehaves on someone's
 * machine must be disableable without a rebuild — and because "which fix broke it" is the first
 * question when a fix pack misbehaves.
 */
#ifndef FIXPACK_H
#define FIXPACK_H

#include "m2.h"

/* Install this module's hooks.
 *
 * Returns 1 on success, 0 if the module could not arm. A module that cannot arm must leave the game
 * exactly as it found it — a half-installed set of detours is worse than none, because the
 * behaviour then depends on which hook happened to take.
 */
typedef int (*fixpack_install_fn)(void);

typedef struct {
    /* Register id, e.g. "BUG-004". Also the .ini key, lowercased: `bug_004 = 1`. */
    const char* id;
    /* One line, for the log. What a player would notice, not what the code does. */
    const char* summary;
    fixpack_install_fn install;
    /* Set from the .ini before install runs. Default ON: someone who installed a fix pack wants the
     * fixes, and a fix nobody enables is a fix nobody tests. */
    int enabled;
} FixpackModule;

/* Every VA this pack touches was verified against ONE build. Hooking a different build would splice
 * a JMP into whatever happens to live at that address — so the failure mode of guessing wrong is not
 * "the fix does not work", it is arbitrary corruption. The pack refuses to arm instead.
 *
 * The target is the cracked de-SecuROM'd retail build: 53,482,288 bytes on disk, sha256
 * `958eb227…`, and for it `file_offset == VA - 0x400000` holds across all 13 sections. Retail v1.1
 * (53,944,080 bytes) has different SecuROM sections and is NOT a valid target.
 *
 * These three come from that image's own PE header, read directly rather than assumed:
 *   SizeOfImage  53,485,568 (0x03302000)  — the LOADED footprint, deliberately not the file size
 *   CheckSum     0x00A5438D
 *   ImageBase    0x00400000
 *
 * All three are checked. SizeOfImage alone is too coarse — two builds can round to the same loaded
 * footprint while their `.text` differs entirely, and `.text` is what these addresses point into.
 */
#define FIXPACK_TARGET_SIZE_OF_IMAGE 53485568u
#define FIXPACK_TARGET_CHECKSUM      0x00A5438Du
#define FIXPACK_TARGET_IMAGE_BASE    0x00400000u

int fixpack_target_is_supported(void);

/* Per-bug modules. One `install` each; see the matching file for the whole story. */
int bug_004_install(void);   /* profile setters that never dirty the autosave flag */
int bug_007_install(void);   /* three PDA blip binders that dereference NULL */
int bug_008_install(void);   /* Gui.ShowLoadingHints(false) is a one-way switch */
int bug_009_install(void);   /* Pg.UnloadLayer NULL-derefs on a co-op client */

/* NOT SHIPPED YET — BUG-005 and BUG-011.
 *
 * Both are real and both are confirmed in the register, but neither has a fix that can be written
 * from static evidence alone, and a detour built on a guess corrupts a live process rather than
 * merely failing. Each is blocked on ONE specific observation:
 *
 *   BUG-005 (SetAllWeapons drops weapons / re-classes primaries)
 *     The control-flow defect is confirmed exactly — `cmp ebp,4 / jge 0x005BF337` jumps INTO the
 *     secondary-store block. But the applier `FUN_006F8EF0` takes 1 character + 4 weapon slots, and
 *     which frame slot maps to which parameter is ambiguous under static analysis: the call site
 *     reuses stack slots heavily and shares one `add esp,0x10` cleanup with the destroy-all call.
 *     Writing a replacement without that mapping means guessing which weapon lands in which slot.
 *     ⚠ The register's "2 primary + 2 secondary" is also wrong for this build — both caps are
 *     `cmp reg,4`, and both buckets are 4 dwords wide.
 *     RESOLVED 2026-08-11 (live capture, two hero swaps): eax->slot1, ecx->slot2, [esp+0x34]->slot3,
 *     [esp+0x38]->slot4. See verification/BUG-005.md. Stays here until the fix is written and verified.
 *
 *   BUG-011 (Object.GetInfiniteAmmo absent)
 *     The read primitive is identified — `FUN_00520EF0(esi = 0x00DF9B10, eax = object)`, the same
 *     membership test the ammo-decrement path itself uses at 0x0051A2D5. But adding a binding means
 *     either writing the `Object` table in `.rdata` — which is the anti-tampered class of write that
 *     crashed early init under SecuROM, and the reason this SDK is `.text`-only — or registering
 *     into the live Lua state through the game's custom-ABI `luaL_register`. The latter is the right
 *     route and has working precedent, but it is a mechanism this pack does not have yet.
 *     ⚠ The register's "CheatInfiniteAmmo, stride 1, cap 128" does not hold up: that name is a
 *     string at 0x00BC5D98 returned by a stub, not a container name, and the container's hash fields
 *     are populated at runtime by a SecuROM-split ctor, so cap and stride are not statically knowable.
 *     NEEDED: the runtime registration path, plus confirmation of the container's shape.
 *
 * Both are tracked in verification/. They ship when the observation exists, not before.
 */

#endif /* FIXPACK_H */
