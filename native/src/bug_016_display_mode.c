/* BUG-016 — the game cannot start on a display that lists no mode it knows. See verification/BUG-016.md.
 *
 * The render state's constructor keeps an EnumAdapterModes mode only at 85 Hz or less and only near the
 * aspect ratios it knows (v1.0: 4:3 and 16:9; v1.1: five bands). When the Mercs2.ini mode is not kept,
 * RenderSystem::ApplySettings searches for 640x480 at 60 Hz; when that is not kept either it returns
 * D3DERR_NOTAVAILABLE before creating a device, its caller ignores the result, and the renderer reads a
 * NULL device. Three patches, every site from m2-sdk (m2_render.h), resolved in the running build:
 *
 *   filter       the loop's `cmp esi, 0x55; ja skip` jumps to code that keeps every mode whose refresh
 *                rate and enumeration index fit the record's bytes (255), without the aspect test.
 *   first search ApplySettings' search for the settings' mode goes through a thunk that notes the mode it
 *                asked for (the fallback overwrites the settings before searching) and logs the list.
 *   fallback     the search for 640x480 at 60 Hz is replaced: the ini resolution, else the desktop's, at
 *                the refresh rate closest to 60 Hz (m2_render_pick_display_mode), written back to the
 *                settings; when the list has neither, a message box saying what to set, then exit.
 *
 * In the reference builds the filter's refresh test is 0x00754E07 (v1.1) and 0x00754C0D (v1.0), the first
 * search call 0x0074CCCB and 0x0074CABB, the fallback call 0x0074CD2E and 0x0074CB1E. */
#include "../src/fixpack.h"

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

/* The record keeps the refresh rate and the enumeration index in one byte each; SetMode re-queries the
 * mode by that index byte, so a mode past either limit would be set as a different mode. */
#define MAX_REFRESH 255u
#define MAX_INDEX   255u
#define RECORD_BYTES 8u
#define FILTER_PATCH_BYTES 9u
#define CODE_BYTES 32u
#define BPP_INDEX_32 1u

static m2_display_mode_sites g_s;
/* The search the two call sites called (the thunks' tail-jump target). */
void* bug016_find_mode;

/* The mode the first search asked for, before the fallback overwrites the settings. */
static void* g_req_state;
static DWORD g_req_w, g_req_h, g_req_bpp, g_req_refresh;
/* The render state whose list was last logged; the list is logged once per state. */
static void* g_logged_state;

typedef struct {
    unsigned char index, refresh;
    unsigned short height, width;
    unsigned char bpp, wide;
} ModeRecord;

static ModeRecord record_at(const unsigned char* list, unsigned i) {
    ModeRecord r;
    memcpy(&r, list + i * RECORD_BYTES, sizeof r);
    return r;
}

static const unsigned char* state_list(void* state) {
    return *(const unsigned char**)((unsigned char*)state + g_s.list_offset);
}

static unsigned state_count(void* state) {
    return *(unsigned*)((unsigned char*)state + g_s.count_offset);
}

static unsigned state_adapter(void* state) {
    return *(unsigned*)((unsigned char*)state + g_s.adapter_offset);
}

/* The adapter's desktop mode and its raw X8R8G8B8 mode count, from our own IDirect3D9. 0 on failure, with
 * the reason logged. */
static int query_adapter(unsigned adapter, D3DDISPLAYMODE* desktop, unsigned* raw_count) {
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    HRESULT hr;
    if (!d3d) {
        m2_logf("BUG-016: Direct3DCreate9 returned NULL; the desktop mode is unknown");
        return 0;
    }
    hr = IDirect3D9_GetAdapterDisplayMode(d3d, adapter, desktop);
    *raw_count = IDirect3D9_GetAdapterModeCount(d3d, adapter, D3DFMT_X8R8G8B8);
    IDirect3D9_Release(d3d);
    if (FAILED(hr)) {
        m2_logf("BUG-016: GetAdapterDisplayMode(adapter %u) failed (0x%08lX); the desktop mode is unknown", adapter,
                (unsigned long)hr);
        return 0;
    }
    return 1;
}

static void log_list(void* state) {
    const unsigned char* list = state_list(state);
    unsigned n = state_count(state), i;
    m2_logf("BUG-016: the game kept %u mode(s) on adapter %u:", n, state_adapter(state));
    for (i = 0; i < n; i++) {
        ModeRecord r = record_at(list, i);
        m2_logf("BUG-016:   %ux%u %u bpp %u Hz (EnumAdapterModes index byte %u)", r.width, r.height, r.bpp, r.refresh,
                r.index);
    }
}

/* Called by thunk_first_search with the search's own arguments (width, height, bpp, refresh). */
void __cdecl bug016_first_search(void* state, const DWORD* args) {
    g_req_state = state;
    g_req_w = args[0];
    g_req_h = args[1];
    g_req_bpp = args[2];
    g_req_refresh = args[3];
    if (state != g_logged_state) {
        g_logged_state = state;
        m2_logf("BUG-016: Mercs2.ini asks for %lux%lu %lu bpp %lu Hz", (unsigned long)g_req_w, (unsigned long)g_req_h,
                (unsigned long)g_req_bpp, (unsigned long)g_req_refresh);
        log_list(state);
    }
}

static void fatal(const char* text) {
    m2_logf("BUG-016: FATAL: %s", text);
    MessageBoxA(NULL, text, "Mercenaries 2 - no usable display mode",
                MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    ExitProcess(1);
}

/* Called by thunk_fallback in place of the search for 640x480 at 60 Hz: fills `out` with the mode to use. */
void __cdecl bug016_fallback(void* state, unsigned char* out) {
    char text[1024];
    D3DDISPLAYMODE desktop;
    unsigned raw = 0, n, adapter;
    int used_desktop = 0, i, have_desktop;
    ModeRecord r;

    if (state != g_req_state) {
        snprintf(text, sizeof text,
                 "The display-mode fix saw the fallback search run for render state %p, but the first search ran "
                 "for %p, so the mode Mercs2.ini asked for is unknown. Please report this with "
                 "scripts\\unofficial_patch.log.",
                 state, g_req_state);
        fatal(text);
    }
    n = state_count(state);
    adapter = state_adapter(state);
    memset(&desktop, 0, sizeof desktop);
    have_desktop = query_adapter(adapter, &desktop, &raw);
    i = m2_render_pick_display_mode(state_list(state), n, g_req_w, g_req_h, have_desktop ? desktop.Width : 0,
                                    have_desktop ? desktop.Height : 0, &used_desktop);
    if (i < 0) {
        snprintf(text, sizeof text,
                 "Mercenaries 2 cannot start: the display offers no fullscreen mode the game can use at the resolution "
                 "Mercs2.ini asks for or at the desktop's.\n\n"
                 "Mercs2.ini asks for %lux%lu at %lu Hz. The desktop is %ux%u at %u Hz%s. Display adapter %u lists %u "
                 "32-bit modes; the game kept %u (at least 640x480, at most 255 Hz, among the first 256 the driver "
                 "lists).\n\n"
                 "Set ScrW, ScrH and RefreshRate in the [Render] section of Mercs2.ini, in the game folder, to a "
                 "resolution and refresh rate from the list in scripts\\unofficial_patch.log, then start the game "
                 "again.",
                 (unsigned long)g_req_w, (unsigned long)g_req_h, (unsigned long)g_req_refresh, desktop.Width,
                 desktop.Height, desktop.RefreshRate, have_desktop ? "" : " (unknown: the query failed)", adapter, raw,
                 n);
        fatal(text);
    }
    memcpy(out, state_list(state) + (unsigned)i * RECORD_BYTES, RECORD_BYTES);
    r = record_at(out, 0);
    *(unsigned*)g_s.settings_width = r.width;
    *(unsigned*)g_s.settings_height = r.height;
    *(unsigned*)g_s.settings_bpp_index = BPP_INDEX_32;
    *(unsigned*)g_s.settings_refresh = r.refresh;
    m2_logf("BUG-016: Mercs2.ini's %lux%lu %lu Hz is not listed; using the %s resolution %ux%u at %u Hz (the rate "
            "closest to 60 Hz) instead of retail's 640x480 at 60 Hz",
            (unsigned long)g_req_w, (unsigned long)g_req_h, (unsigned long)g_req_refresh,
            used_desktop ? "desktop" : "Mercs2.ini", r.width, r.height, r.refresh);
    if (raw > MAX_INDEX + 1)
        m2_logf("BUG-016: adapter %u lists %u 32-bit modes; the game's records index only the first %u", adapter, raw,
                MAX_INDEX + 1);
}

/* ECX = render state, EAX = out record, [esp+4..] = width, height, bpp, refresh (stdcall, ret 0x10). Notes the
 * request, then tail-jumps to the original search with every register it reads restored. */
__attribute__((naked, used)) static void thunk_first_search(void) {
    __asm__ volatile(
        "pushl %%eax\n\t"
        "pushl %%ecx\n\t"
        "leal 12(%%esp), %%edx\n\t" /* the four arguments, past ECX, EAX and the return address */
        "pushl %%edx\n\t"
        "pushl %%ecx\n\t"
        "call _bug016_first_search\n\t"
        "addl $8, %%esp\n\t"
        "popl %%ecx\n\t"
        "popl %%eax\n\t"
        "jmp *_bug016_find_mode\n\t" ::);
}

/* Same convention as the search; returns EAX = the out record, popping the four arguments. */
__attribute__((naked, used)) static void thunk_fallback(void) {
    __asm__ volatile(
        "pushl %%eax\n\t"
        "pushl %%ecx\n\t"
        "call _bug016_fallback\n\t"
        "addl $4, %%esp\n\t"
        "popl %%eax\n\t"
        "ret $0x10\n\t" ::);
}

/* ===== patching ===== */

typedef struct {
    unsigned char* at;
    unsigned char saved[FILTER_PATCH_BYTES];
    unsigned len;
} Saved;

static int write_code(unsigned char* at, const unsigned char* bytes, unsigned len, Saved* save) {
    DWORD old, unused;
    if (!VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &old)) return 0;
    if (save) {
        save->at = at;
        save->len = len;
        memcpy(save->saved, at, len);
    }
    memcpy(at, bytes, len);
    VirtualProtect(at, len, old, &unused);
    FlushInstructionCache(GetCurrentProcess(), at, len);
    return 1;
}

static void restore(const Saved* s) {
    if (s->at) write_code(s->at, s->saved, s->len, NULL);
}

/* The call at `site` must be `call find_mode`; it becomes `call to`. */
static int redirect_call(void* site, void* to, Saved* save) {
    unsigned char* p = (unsigned char*)site;
    unsigned char call[5];
    LONG rel;
    if (p[0] != 0xE8 || (unsigned char*)site + 5 + *(LONG*)(p + 1) != (unsigned char*)bug016_find_mode) {
        m2_logf("BUG-016: the call at %p is not a call to the mode search %p", site, bug016_find_mode);
        return 0;
    }
    rel = (LONG)((unsigned char*)to - (p + 5));
    call[0] = 0xE8;
    memcpy(call + 1, &rel, 4);
    return write_code(p, call, 5, save);
}

int bug_016_install(void) {
    unsigned char patch[FILTER_PATCH_BYTES];
    unsigned char* code;
    unsigned len;
    Saved first = {0}, fallback = {0};

    g_s.size = sizeof g_s;
    if (!m2_render_display_mode_sites(&g_s)) {
        m2_logf("BUG-016: %s", m2_render_last_error());
        return FIXPACK_FAILED;
    }
    bug016_find_mode = g_s.find_mode;
    /* The list is built when ApplySettings constructs the pending render state; a fix armed after that sees
     * a list already filtered and changes nothing until the next settings change. */
    if (*g_s.pending_state) {
        m2_logf("BUG-016: the render state %p already exists: the mode list was built before the fix armed",
                *g_s.pending_state);
        return FIXPACK_FAILED;
    }
    code = (unsigned char*)VirtualAlloc(NULL, CODE_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code) {
        m2_logf("BUG-016: VirtualAlloc for the filter code failed (%lu)", (unsigned long)GetLastError());
        return FIXPACK_FAILED;
    }
    len = m2_render_display_mode_filter(code, MAX_REFRESH, MAX_INDEX, code, CODE_BYTES, patch);
    if (!len) {
        m2_logf("BUG-016: %s", m2_render_last_error());
        VirtualFree(code, 0, MEM_RELEASE);
        return FIXPACK_FAILED;
    }
    FlushInstructionCache(GetCurrentProcess(), code, len);
    if (!redirect_call(g_s.first_search_call, (void*)thunk_first_search, &first) ||
        !redirect_call(g_s.fallback_call, (void*)thunk_fallback, &fallback) ||
        !write_code((unsigned char*)g_s.refresh_cmp, patch, FILTER_PATCH_BYTES, NULL)) {
        restore(&fallback);
        restore(&first);
        VirtualFree(code, 0, MEM_RELEASE);
        m2_logf("BUG-016: a patch would not apply; the game is as it was");
        return FIXPACK_FAILED;
    }
    m2_logf("BUG-016: filter %p -> %p (%u bytes: keep up to %u Hz and index %u, no aspect test), first search %p, "
            "fallback %p, settings %p/%p/%p/%p",
            g_s.refresh_cmp, (void*)code, len, MAX_REFRESH, MAX_INDEX, g_s.first_search_call, g_s.fallback_call,
            (void*)g_s.settings_width, (void*)g_s.settings_height, (void*)g_s.settings_bpp_index,
            (void*)g_s.settings_refresh);
    return FIXPACK_ARMED;
}
