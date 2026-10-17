/* The fix pack's entry point: reads the .ini, then installs each enabled bug module. */
#include "fixpack.h"

#include <string.h>

/* BUG-005, BUG-007 and BUG-011 are absent by design — see the note in fixpack.h. */
static FixpackModule g_modules[] = {
    { "BUG-004", "profile changes to cash/fuel-capacity/character/costume are not autosaved",
      bug_004_install, 1 },
    { "BUG-008", "Gui.ShowLoadingHints(false) never turns loading hints off",
      bug_008_install, 1 },
    { "BUG-009", "Pg.UnloadLayer crashes a co-op client with no current game state",
      bug_009_install, 1 },
    /* Console-parity work: ships DISABLED by default. feature_003 is a seed that logs today
     * and will attach a mesh in a later iteration. feature_004 is the mesh-emitter fix for
     * canopies, trees and wrecks. See src/unofficial_patch.ini. */
    { "FEATURE-003", "winch magnet chain visual (seed form; logs Havok-chain builds)",
      feature_003_install, 0 },
    { "FEATURE-004", "mesh-emitter fire visuals (canopies, trees, wrecks)",
      feature_004_install, 0 },
};

#define MODULE_COUNT ((int)(sizeof(g_modules) / sizeof(g_modules[0])))

/* Build classification. Cached after the first call so every module can query it cheaply.
 * ⚠ On a build NOT in this table MinHook would splice a JMP into whatever lives at that VA —
 * the failure mode of a wrong address is CORRUPTION, not a no-op — so we refuse instead. */
FixpackBuild fixpack_build(void) {
    static int cached = 0;
    static FixpackBuild build = FIXPACK_BUILD_UNKNOWN;
    HMODULE exe;
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS32* nt;

    if (cached) return build;
    cached = 1;

    exe = GetModuleHandleA(NULL);
    if (!exe) return build;
    dos = (IMAGE_DOS_HEADER*)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return build;
    nt = (IMAGE_NT_HEADERS32*)((BYTE*)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return build;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return build;
    if (nt->OptionalHeader.ImageBase != FIXPACK_TARGET_IMAGE_BASE) {
        m2_logf("  target check: ImageBase 0x%08lX, expected 0x%08lX",
                (unsigned long)nt->OptionalHeader.ImageBase,
                (unsigned long)FIXPACK_TARGET_IMAGE_BASE);
        return build;
    }

    /* SizeOfImage is the loaded footprint, not the on-disk size. Checked WITH the checksum
     * because SizeOfImage alone is coarse — two builds can share a footprint while .text
     * differs, and .text is what these addresses point into. */
    if (nt->OptionalHeader.SizeOfImage == FIXPACK_V11_SIZE_OF_IMAGE &&
        nt->OptionalHeader.CheckSum == FIXPACK_V11_CHECKSUM) {
        build = FIXPACK_BUILD_V11;
        m2_logf("  target check: v11-patched-securom (SoI 0x%08lX, CS 0x%08lX)",
                (unsigned long)nt->OptionalHeader.SizeOfImage,
                (unsigned long)nt->OptionalHeader.CheckSum);
    } else {
        m2_logf("  target check: unknown build (SoI 0x%08lX, CS 0x%08lX) — expected v11",
                (unsigned long)nt->OptionalHeader.SizeOfImage,
                (unsigned long)nt->OptionalHeader.CheckSum);
    }
    return build;
}

int fixpack_target_is_supported(void) {
    return fixpack_build() != FIXPACK_BUILD_UNKNOWN;
}

static void OnIniKey(void* ud, const char* key, const char* value) {
    int i, j;
    (void)ud;
    for (i = 0; i < MODULE_COUNT; i++) {
        /* `bug_004 = 0` disables BUG-004. Case-insensitive, underscore form of the register id.
         * We rewrite every '-' to '_', not just position 3, so ids of varying length work the
         * same way: PROBE-001 -> PROBE_001, FEATURE-003 -> FEATURE_003. */
        char want[24];
        int n = (int)strlen(g_modules[i].id);
        if (n >= (int)sizeof(want)) continue;
        memcpy(want, g_modules[i].id, (size_t)n + 1);
        for (j = 0; j < n; j++) if (want[j] == '-') want[j] = '_';
        if (lstrcmpiA(key, want) == 0) {
            g_modules[i].enabled = m2_ini_bool(value);
            return;
        }
    }
}

static DWORD WINAPI Install(LPVOID unused) {
    char ini[MAX_PATH];
    int i, armed = 0, failed = 0;
    (void)unused;

    m2_module_path(M2_SELF_MODULE, "unofficial_patch.ini", ini, sizeof(ini));
    m2_ini_parse(ini, OnIniKey, NULL);

    if (!m2_hook_init()) {
        m2_logf("FATAL: MinHook would not initialise; no fixes installed");
        return 0;
    }

    for (i = 0; i < MODULE_COUNT; i++) {
        int rc;
        if (!g_modules[i].enabled) {
            m2_logf("%s  skipped (disabled in unofficial_patch.ini)", g_modules[i].id);
            continue;
        }
        rc = g_modules[i].install();
        if (rc == FIXPACK_ARMED) {
            m2_logf("%s  armed   — %s", g_modules[i].id, g_modules[i].summary);
            armed++;
        } else if (rc == FIXPACK_SKIPPED) {
            /* Module already logged why; count as neither armed nor failed. */
            continue;
        } else {
            m2_logf("%s  FAILED  — %s", g_modules[i].id, g_modules[i].summary);
            failed++;
        }
    }
    m2_logf("%d armed, %d failed", armed, failed);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);

    /* ⚠ Refuse a mismatched m2-sdk.dll: the loader binds by name only, so a changed signature would
     * link and then corrupt the stack. */
    if (!m2_abi_ok()) return FALSE;

    m2_log_init(inst);
    m2_logf("Mercenaries 2 unofficial patch, m2 %s", m2_version_string());

    if (!fixpack_target_is_supported()) {
        m2_logf("FATAL: this is not the build the addresses were verified against; nothing installed");
        return TRUE;   /* stay loaded so the log survives; just do nothing */
    }

    /* ⚠ Hook off the loader lock: this runs nested in pmc_bb's DllMain, and MinHook's Freeze()
     * suspends every thread — not safe to do while holding the loader lock. */
    CreateThread(NULL, 0, Install, NULL, 0, NULL);
    return TRUE;
}
