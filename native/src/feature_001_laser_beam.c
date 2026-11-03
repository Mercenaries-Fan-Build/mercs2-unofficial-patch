/* FEATURE-001 — visible red laser beam for the laser designator (Phase 2 v2).
 *
 * Phase 2 v1 crashed with EIP=0 at pmc_blackbox timestamp 2026-10-17 15:28:40 because the
 * AimState hook used a C-ABI MinHook detour whose signature assumed Ghidra's `__cdecl`
 * labelling. The actual ABI of FUN_0051a880 is not standard cdecl; calling the trampoline
 * as cdecl corrupted the stack and the first game-side call eventually RET'd into NULL.
 *
 * v2 uses NAKED-WRAPPER hooks for every laser-related detour. Each wrapper:
 *   - `pushal` + `pushfl` to preserve all GPRs and flags (full snapshot of caller state),
 *   - pushes the few args we want to snoop onto the stack explicitly,
 *   - calls a tiny C handler (which may clobber EAX etc.; harmless, we restore from pushal),
 *   - `popfl` + `popal` to restore everything bit-exact,
 *   - tail-`jmp`s through MinHook's trampoline.
 * The hooked function then runs with its original register state and ABI. Our snooping
 * cannot corrupt its stack because we never call it from C — we tail-jump.
 *
 * Three hook points on PC v11:
 *   - 0x0051A880 FUN_0051a880 (weapon-aim-state): snoops the aim-state struct pointer from
 *     either [esp+4] or ECX (candidate check with IsBadReadPtr). aim-state[0..2] is the
 *     origin world position (confirmed by decomp lines 147349-147351 writing from
 *     FUN_00587d80() result).
 *   - 0x00628750 FUN_00628750 (per-frame beam-position update): snoops EAX (= Vec3* target
 *     per decomp lines 288552-553).
 *   - 0x00628890 FUN_00628890 (LaserState binder): snoops [esp+8] (= bActive byte).
 *
 * EndScene reads the three signals + tick freshness and draws a world-space line when all
 * three are current. If the hooks never fire (laser never activated), the per-frame draw is
 * skipped and the game runs unaffected.
 */
#include "../src/fixpack.h"

#include <windows.h>
#include <d3d9.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VA_LASER_AIM_STATE        0x0051A880u
#define VA_LASER_BEAM_UPDATE      0x00628750u
#define VA_LASER_STATE_BINDER     0x00628890u

#define VTBL_SLOT_END_SCENE          42

typedef struct {
    float x, y, z, rhw;
    DWORD color;
} ScreenVertex;

typedef struct {
    float x, y, z;
    DWORD color;
} WorldVertex;

typedef HRESULT (__stdcall *EndSceneFn)(IDirect3DDevice9*);

static EndSceneFn g_orig_end_scene;

/* Trampoline targets written by MinHook; naked wrappers tail-jmp through these. */
static void* g_orig_aim_state;
static void* g_orig_beam_update;
static void* g_orig_laser_binder;

static volatile LONG g_tick;
static volatile LONG g_origin_fresh_tick = -1000;
static volatile LONG g_target_fresh_tick = -1000;
static volatile LONG g_laser_active_signal;

static float g_origin[3];
static float g_target[3];

/* ===== Hook C handlers (called from the naked wrappers) ===== */

void __cdecl laser_aim_state_capture(float* stack_candidate, float* ecx_candidate) {
    float* ptr = NULL;
    if (stack_candidate && !IsBadReadPtr(stack_candidate, 32)) {
        ptr = stack_candidate;
    } else if (ecx_candidate && !IsBadReadPtr(ecx_candidate, 32)) {
        ptr = ecx_candidate;
    }
    if (!ptr) return;
    g_origin[0] = ptr[0];
    g_origin[1] = ptr[1];
    g_origin[2] = ptr[2];
    g_origin_fresh_tick = g_tick;
}

void __cdecl laser_beam_update_capture(float* target_vec3) {
    if (!target_vec3 || IsBadReadPtr(target_vec3, 12)) return;
    g_target[0] = target_vec3[0];
    g_target[1] = target_vec3[1];
    g_target[2] = target_vec3[2];
    g_target_fresh_tick = g_tick;
}

void __cdecl laser_binder_capture(unsigned int active_u32) {
    g_laser_active_signal = (LONG)(active_u32 & 0xFF);
}

/* ===== Naked wrappers =====
 *
 * Stack layout at function entry (before our wrapper runs):
 *     [esp+0]  = caller return address
 *     [esp+4]  = original first stack arg
 *     [esp+8]  = original second stack arg
 *     ...
 *
 * After `pushal` (-32) + `pushfl` (-4) + any additional pushes, the original [esp+N] is at
 * [current_esp + 36 + extra_pushes + N].
 */

__attribute__((naked, used))
static void thunk_aim_state(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushfl\n\t"
        "pushl %%ecx\n\t"           /* candidate2 (ECX) — c-handler arg #2 */
        "pushl 0x2C(%%esp)\n\t"     /* candidate1 — original [esp+4]
                                     * current_esp = esp0 - 40; original [esp+4] at
                                     * [current_esp + 40 + 4] = [current_esp + 0x2C]
                                     * c-handler arg #1 */
        "call _laser_aim_state_capture\n\t"
        "addl $8, %%esp\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *%0\n\t"
        : : "m"(g_orig_aim_state));
}

__attribute__((naked, used))
static void thunk_beam_update(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushfl\n\t"
        "pushl %%eax\n\t"           /* EAX = Vec3* target */
        "call _laser_beam_update_capture\n\t"
        "addl $4, %%esp\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *%0\n\t"
        : : "m"(g_orig_beam_update));
}

__attribute__((naked, used))
static void thunk_laser_binder(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushfl\n\t"
        "pushl 0x2C(%%esp)\n\t"     /* original [esp+8] = param_2 (bActive)
                                     * current_esp = esp0 - 36; original [esp+8] at
                                     * [current_esp + 36 + 8] = [current_esp + 0x2C] */
        "call _laser_binder_capture\n\t"
        "addl $4, %%esp\n\t"
        "popfl\n\t"
        "popal\n\t"
        "jmp *%0\n\t"
        : : "m"(g_orig_laser_binder));
}

/* ===== EndScene draw ===== */

/* SetVertexShaderConstantF hook was removed after a crash under Wine's d3d9.dll (blackbox
 * 2026-10-18 15:27:28 EIP=0x005AE362 EBP=0x48). Wine's d3d9 reimplementation has prologue
 * patterns MinHook can't reliably trampoline. We now snapshot the c0..c63 range via passive
 * GetVertexShaderConstantF from inside EndScene — read-only, no live hook on d3d9 code. */

static HRESULT __stdcall Hook_EndScene(IDirect3DDevice9* device) {
    LONG tick = InterlockedIncrement(&g_tick);
    IDirect3DStateBlock9* block = NULL;
    HRESULT bhr = IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &block);
    if (FAILED(bhr) || !block) return g_orig_end_scene(device);

    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetTexture(device, 0, NULL);

    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING,         FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE,          FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZWRITEENABLE,     FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE,         D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE,  FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_FOGENABLE,        FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE,
                                    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN
                                    | D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRGBWRITEENABLE,  FALSE);

    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP,   D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    int fresh = (g_laser_active_signal != 0)
             && (tick - g_target_fresh_tick <= 2)
             && (tick - g_origin_fresh_tick <= 2);
    if (fresh) {
        static volatile LONG s_beam_draws;
        LONG n = InterlockedIncrement(&s_beam_draws);

        /* First-frame dump: vertex shader constants c0..c63 (passive read via
         * GetVertexShaderConstantF). ×1000 int-cast so values in [-1,1] become visible. The
         * WVP (or V+P separately) matrix slot has to be in here somewhere; we didn't find it
         * in c0..c31 so this iteration extends to c63. */
        if (n == 1) {
            float vs[256];
            memset(vs, 0, sizeof(vs));
            IDirect3DDevice9_GetVertexShaderConstantF(device, 0, vs, 64);
            for (int cr = 0; cr < 64; cr++) {
                int off = cr * 4;
                m2_logf("[feature_001] vs[c%02d] = %ld, %ld, %ld, %ld   (x1000)",
                        cr,
                        (long)(vs[off+0] * 1000.0f),
                        (long)(vs[off+1] * 1000.0f),
                        (long)(vs[off+2] * 1000.0f),
                        (long)(vs[off+3] * 1000.0f));
            }
        }

        /* Make sure WORLD is identity so our XYZ verts aren't model-transformed. */
        D3DMATRIX world_identity;
        memset(&world_identity, 0, sizeof(world_identity));
        world_identity.m[0][0] = world_identity.m[1][1] = world_identity.m[2][2]
                              = world_identity.m[3][3] = 1.0f;
        IDirect3DDevice9_SetTransform(device, D3DTS_WORLD, &world_identity);

        WorldVertex verts[2] = {
            { g_origin[0], g_origin[1], g_origin[2], 0xFFFF0000 },
            { g_target[0], g_target[1], g_target[2], 0xFFFF0000 },
        };
        IDirect3DDevice9_SetFVF(device, D3DFVF_XYZ | D3DFVF_DIFFUSE);
        IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_LINELIST, 1, verts, sizeof(WorldVertex));

        /* Screen-space debug marker (green) — stays as the "hook+capture+draw works" tell. */
        ScreenVertex marker[2] = {
            { 300.0f, 300.0f, 0.5f, 1.0f, 0xFF00FF00 },
            { 500.0f, 300.0f, 0.5f, 1.0f, 0xFF00FF00 },
        };
        IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_LINELIST, 1, marker, sizeof(ScreenVertex));

        if (n <= 5 || (n % 60) == 0) {
            m2_logf("[feature_001] beam t=%ld #%ld O=%ld,%ld,%ld T=%ld,%ld,%ld",
                    tick, n,
                    (long)g_origin[0], (long)g_origin[1], (long)g_origin[2],
                    (long)g_target[0], (long)g_target[1], (long)g_target[2]);
        }
    } else if (tick == 60 || tick == 600 || tick == 1800) {
        m2_logf("[feature_001] idle t=%ld active=%ld orig_dt=%ld tgt_dt=%ld",
                tick, (long)g_laser_active_signal,
                (long)(tick - g_origin_fresh_tick), (long)(tick - g_target_fresh_tick));
    }

    IDirect3DStateBlock9_Apply(block);
    IDirect3DStateBlock9_Release(block);
    return g_orig_end_scene(device);
}

static void* resolve_end_scene_address(void) {
    HWND hidden = CreateWindowExA(0, "STATIC", NULL, 0, 0, 0, 1, 1, NULL, NULL,
                                  GetModuleHandleW(NULL), NULL);
    if (!hidden) {
        m2_logf("[feature_001] CreateWindowEx failed (gle=%lu)", GetLastError());
        return NULL;
    }
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { DestroyWindow(hidden); return NULL; }

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hidden;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dummy = NULL;
    HRESULT hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, hidden,
                                         D3DCREATE_SOFTWARE_VERTEXPROCESSING
                                         | D3DCREATE_NOWINDOWCHANGES,
                                         &pp, &dummy);
    if (FAILED(hr) || !dummy) {
        hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hidden,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING
                                     | D3DCREATE_NOWINDOWCHANGES,
                                     &pp, &dummy);
    }
    if (FAILED(hr) || !dummy) {
        IDirect3D9_Release(d3d);
        DestroyWindow(hidden);
        m2_logf("[feature_001] CreateDevice failed hr=0x%08X", (unsigned)hr);
        return NULL;
    }
    void** vtbl = *(void***)dummy;
    void* end_scene = vtbl[VTBL_SLOT_END_SCENE];
    IDirect3DDevice9_Release(dummy);
    IDirect3D9_Release(d3d);
    DestroyWindow(hidden);
    return end_scene;
}

int feature_001_install(void) {
    if (fixpack_build() != FIXPACK_BUILD_V11) {
        m2_logf("FEATURE-001  skipped (v11-only)");
        return FIXPACK_SKIPPED;
    }
    void* end_scene = resolve_end_scene_address();
    if (!end_scene) return FIXPACK_FAILED;
    if (!m2_hook_attach(end_scene, (void*)Hook_EndScene, (void**)&g_orig_end_scene)) {
        m2_logf("[feature_001] EndScene hook failed");
        return FIXPACK_FAILED;
    }
    if (!m2_hook_attach((void*)VA_LASER_AIM_STATE, (void*)thunk_aim_state,
                        (void**)&g_orig_aim_state)) {
        m2_logf("[feature_001] AimState hook failed");
        return FIXPACK_FAILED;
    }
    if (!m2_hook_attach((void*)VA_LASER_BEAM_UPDATE, (void*)thunk_beam_update,
                        (void**)&g_orig_beam_update)) {
        m2_logf("[feature_001] BeamUpdate hook failed");
        return FIXPACK_FAILED;
    }
    if (!m2_hook_attach((void*)VA_LASER_STATE_BINDER, (void*)thunk_laser_binder,
                        (void**)&g_orig_laser_binder)) {
        m2_logf("[feature_001] LaserStateBinder hook failed");
        return FIXPACK_FAILED;
    }
    m2_logf("[feature_001] armed: EndScene=%p AimState=0x%08X BeamUpdate=0x%08X Binder=0x%08X (naked wrappers)",
            end_scene, VA_LASER_AIM_STATE, VA_LASER_BEAM_UPDATE, VA_LASER_STATE_BINDER);
    return FIXPACK_ARMED;
}
