/* BUG-014 — the billboard-tree Z-prepass draws with the previous draw's world matrix.
 * See verification/BUG-014.md.
 *
 * The Z-prepass loop pushes no world matrix, so its packets carry the matrix index last pushed before
 * it; in frame captures, an occlusion box's (a unit cube scaled to an object's bounds, ~9-200x). The
 * shadow tree loop pushes the identity before it draws. The hook does the same push, then runs the
 * original loop. Every site comes from m2-sdk (m2_render.h), resolved in the running build; in the
 * reference builds the Z-prepass loop is 0x004A3EA0 (v1.1) and 0x004A3E40 (v1.0). */
#include "../src/fixpack.h"

#include <string.h>

typedef void(__cdecl* TreeLoopFn)(void);
static TreeLoopFn g_orig;
static m2_tree_sites g_sites;

/* The shadow loop's push: i = counter++, entry (parity * cap + i) = identity, index = i, record = 1. */
static void __cdecl Hook_TreeZPass(void) {
    unsigned short i = *g_sites.matrices;
    *g_sites.matrices = (unsigned short)(i + 1);
    memcpy(g_sites.matrix_table + ((size_t)*g_sites.parity * g_sites.matrix_cap + i) * 16, g_sites.identity,
           16 * sizeof(float));
    *g_sites.matrix_index = i;
    *g_sites.draw_record = 1;
    g_orig();
}

int bug_014_install(void) {
    g_sites.size = sizeof g_sites;
    if (!m2_render_tree_sites(&g_sites)) {
        m2_logf("BUG-014: %s", m2_render_last_error());
        return FIXPACK_FAILED;
    }
    if (!m2_hook_attach(g_sites.zpass_loop, (void*)Hook_TreeZPass, (void**)&g_orig)) {
        m2_logf("BUG-014: the hook on the Z-prepass loop %p would not attach", g_sites.zpass_loop);
        return FIXPACK_FAILED;
    }
    m2_logf("BUG-014: Z-prepass loop %p, identity %p, matrix index %p, table %p x %u", g_sites.zpass_loop,
            (const void*)g_sites.identity, (void*)g_sites.matrix_index, (void*)g_sites.matrix_table,
            g_sites.matrix_cap);
    return FIXPACK_ARMED;
}
