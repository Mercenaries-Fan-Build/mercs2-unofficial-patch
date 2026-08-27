/* The fix pack's entry point: reads the .ini, then installs each enabled bug module. */
#include "fixpack.h"

#include <string.h>

/* BUG-005 and BUG-011 are absent by design — see the note in fixpack.h. */
static FixpackModule g_modules[] = {
    { "BUG-004", "profile changes to cash/fuel-capacity/character/costume are not autosaved",
      bug_004_install, 1 },
    { "BUG-007", "three PDA blip binders crash on a stale or out-of-range blip id",
      bug_007_install, 1 },
    { "BUG-008", "Gui.ShowLoadingHints(false) never turns loading hints off",
      bug_008_install, 1 },
    { "BUG-009", "Pg.UnloadLayer crashes a co-op client with no current game state",
      bug_009_install, 1 },
};

#define MODULE_COUNT ((int)(sizeof(g_modules) / sizeof(g_modules[0])))

/* Only hook the build the addresses were verified against. On any other build MinHook would splice a
 * JMP into whatever lives at that VA — ⚠ the failure mode of a wrong address is corruption, not a
 * no-op — so refuse instead. */
int fixpack_target_is_supported(void) {
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS32* nt;

    if (!exe) return 0;
    dos = (IMAGE_DOS_HEADER*)exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS32*)((BYTE*)exe + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return 0;

    /* SizeOfImage is the loaded footprint (53,485,568), not the on-disk size. Checked with the
     * checksum because SizeOfImage alone is coarse — two builds can share a footprint while .text
     * differs, and .text is what these addresses point into. */
    if (nt->OptionalHeader.SizeOfImage != FIXPACK_TARGET_SIZE_OF_IMAGE) {
        m2_logf("  target check: SizeOfImage %lu, expected %lu",
                (unsigned long)nt->OptionalHeader.SizeOfImage,
                (unsigned long)FIXPACK_TARGET_SIZE_OF_IMAGE);
        return 0;
    }
    if (nt->OptionalHeader.CheckSum != FIXPACK_TARGET_CHECKSUM) {
        m2_logf("  target check: CheckSum 0x%08lX, expected 0x%08lX",
                (unsigned long)nt->OptionalHeader.CheckSum,
                (unsigned long)FIXPACK_TARGET_CHECKSUM);
        return 0;
    }
    if (nt->OptionalHeader.ImageBase != FIXPACK_TARGET_IMAGE_BASE) {
        m2_logf("  target check: ImageBase 0x%08lX, expected 0x%08lX",
                (unsigned long)nt->OptionalHeader.ImageBase,
                (unsigned long)FIXPACK_TARGET_IMAGE_BASE);
        return 0;
    }
    return 1;
}

static void OnIniKey(void* ud, const char* key, const char* value) {
    int i;
    (void)ud;
    for (i = 0; i < MODULE_COUNT; i++) {
        /* `bug_004 = 0` disables BUG-004. Case-insensitive, underscore form of the register id. */
        char want[16];
        int n = (int)strlen(g_modules[i].id);
        if (n >= (int)sizeof(want)) continue;
        memcpy(want, g_modules[i].id, (size_t)n + 1);
        want[3] = '_';                       /* "BUG-004" -> "BUG_004" */
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
        if (!g_modules[i].enabled) {
            m2_logf("%s  skipped (disabled in unofficial_patch.ini)", g_modules[i].id);
            continue;
        }
        if (g_modules[i].install()) {
            m2_logf("%s  armed   — %s", g_modules[i].id, g_modules[i].summary);
            armed++;
        } else {
            m2_logf("%s  FAILED  — %s", g_modules[i].id, g_modules[i].summary);
            failed++;
        }
    }
    m2_logf("%d armed, %d failed, %d disabled", armed, failed,
            MODULE_COUNT - armed - failed);
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
