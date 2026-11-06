/* Copyright (C) 2026, The AROS Development Team. All rights reserved. */
/* Finite native AROS GL smoke test; no persistent settings or files. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/posixc.h>
#include <proto/intuition.h>
#include <GL/gla.h>
#include <stdio.h>
#include <stdlib.h>
#include <aros/types/file_s.h>
#include <string.h>
#include "virglpair_protocol.h"

/* Compare every RGB channel against the uploaded texel selected by nearest
 * sampling. The 192-pixel viewport is exactly divisible by the four texels. */
static int check_texture(const unsigned char *texels, int blend)
{
    static unsigned char readback[192 * 192 * 4];
    unsigned bad = 0, maximum = 0;
    int x, y, c;
    glReadPixels(0, 0, 192, 192, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    for (y = 0; y < 192; ++y)
        for (x = 0; x < 192; ++x)
            for (c = 0; c < 3; ++c) {
                unsigned want = texels[((y / 48) * 4 + x / 48) * 4 + c];
                unsigned got = readback[(y * 192 + x) * 4 + c];
                unsigned error;
                if (blend) want = 128 + (want * 127 + 127) / 255;
                error = got > want ? got - want : want - got;
                if (error > maximum) maximum = error;
                if (error > 1) ++bad;
            }
    printf("%s: pixels=36864 bad_RGB_channels=%u max_error=%u\n",
        blend ? "alpha blend" : "texture", bad, maximum);
    return bad == 0;
}

static void textured_quad(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f( 1, -1);
    glTexCoord2f(1, 1); glVertex2f( 1,  1);
    glTexCoord2f(0, 1); glVertex2f(-1,  1);
    glEnd();
}

static int texture_and_blend(void)
{
    static const unsigned char palette[4][4] = {
        {255, 0, 0, 255}, {0, 255, 0, 255},
        {0, 0, 255, 255}, {255, 255, 0, 255}
    };
    static const unsigned char update[8] = {255, 0, 255, 255, 0, 255, 255, 255};
    unsigned char texels[4 * 4 * 4];
    GLuint texture;
    int i, ok;
    for (i = 0; i < 16; ++i) memcpy(texels + i * 4, palette[(i + i / 4) % 4], 4);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    textured_quad();
    ok = check_texture(texels, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 2, 2, 1,
        GL_RGBA, GL_UNSIGNED_BYTE, update);
    memcpy(texels + (2 * 4 + 1) * 4, update, sizeof(update));
    textured_quad();
    ok = check_texture(texels, 0) && ok;
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(255, 255, 255, 128);
    textured_quad();
    ok = check_texture(texels, 1) && ok;
    glDisable(GL_BLEND);
    glDeleteTextures(1, &texture);
    return ok;
}

static int test_stencil = 1;
static int fail_context = -1;

static GLAContext new_context(struct Window *win)
{
    struct TagItem attrs[] = {
        {GLA_Window, (IPTR)win},
        {GLA_Left, win->BorderLeft}, {GLA_Top, win->BorderTop},
        {GLA_Right, win->BorderRight}, {GLA_Bottom, win->BorderBottom},
        {GLA_DoubleBuf, GL_TRUE}, {GLA_RGBMode, GL_TRUE},
        {GLA_NoDepth, GL_FALSE}, {GLA_NoStencil, test_stencil ? GL_FALSE : GL_TRUE},
        {GLA_NoAccum, GL_TRUE},
        {GLA_ContextProfile, fail_context >= 0 ? GLA_PROFILE_CORE : GLA_PROFILE_COMPAT},
        {GLA_ContextMajorVersion, fail_context >= 0 ? 999 : 1},
        {GLA_ContextMinorVersion, 0}, {TAG_DONE, 0}
    };
    return glACreateContext(attrs);
}

static int pixel_is(int x, int y, int r, int g, int b)
{
    unsigned char p[4] = {0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    printf("pixel(%d,%d): %u %u %u %u expected=%d,%d,%d\n",
        x, y, p[0], p[1], p[2], p[3], r, g, b);
    return p[0] == r && p[1] == g && p[2] == b;
}

static void full_quad(float z)
{
    glBegin(GL_QUADS);
    glVertex3f(-1, -1, z); glVertex3f(1, -1, z);
    glVertex3f(1, 1, z); glVertex3f(-1, 1, z);
    glEnd();
}

static int depth_stencil(void)
{
    GLint depth = 0, stencil = 0;
    int ok;
    glGetIntegerv(GL_DEPTH_BITS, &depth);
    glGetIntegerv(GL_STENCIL_BITS, &stencil);
    printf("buffers: depth=%d stencil=%d\n", depth, stencil);
    if (depth < 16 || (test_stencil && stencil < 8)) return 0;
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClearColor(0, 0, 0, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glColor3f(1, 0, 0); full_quad(0);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.8f, -0.8f, -0.5f);
    glVertex3f(0.8f, -0.8f, -0.5f);
    glVertex3f(0, 0.8f, -0.5f);
    glEnd();
    glColor3f(0, 0, 1); full_quad(0.5f);
    ok = pixel_is(96, 96, 0, 255, 0);
    ok = pixel_is(4, 4, 255, 0, 0) && ok;
    printf("depth occlusion: %s\n", ok ? "PASS" : "FAIL");
    glDisable(GL_DEPTH_TEST);
    if (!test_stencil) return ok;
    glClearColor(0, 0, 1, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 1, 255);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(0, -1);
    glVertex2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 255);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor3f(1, 1, 1); full_quad(0);
    {
        int masked = pixel_is(48, 96, 255, 255, 255);
        masked = pixel_is(144, 96, 0, 0, 255) && masked;
        printf("stencil mask: %s\n", masked ? "PASS" : "FAIL");
        ok = masked && ok;
    }
    glDisable(GL_STENCIL_TEST);
    return ok;
}

static int simultaneous_context(GLAContext *first_context, struct Window *first_window,
    const char *expected_renderer)
{
    GLAContext first = *first_context;
    struct Window *win = OpenWindowTags(NULL, WA_Title, (IPTR)"Second GL context",
        WA_Left, 250, WA_Top, 20, WA_InnerWidth, 64, WA_InnerHeight, 64,
        WA_DragBar, TRUE, TAG_DONE);
    GLAContext second;
    int ok;
    if (!win) return 0;
    second = new_context(win);
    if (!second) {
        puts("LIMIT: second simultaneous GL context unavailable");
        glAMakeCurrent(first); CloseWindow(win); return 0;
    }
    glAMakeCurrent(second);
    {
        const char *name = (const char *)glGetString(GL_RENDERER);
        printf("second GL_RENDERER: %s\n", name ? name : "(null)");
        ok = name && strstr(name, expected_renderer);
    }
    glViewport(0, 0, 64, 64);
    glReadBuffer(GL_BACK);
    glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    ok = pixel_is(32, 32, 255, 0, 255) && ok;
    glASwapBuffers(second);
    glAMakeCurrent(first);
    if (test_stencil) {
        ok = pixel_is(48, 96, 255, 255, 255) && ok;
        ok = pixel_is(144, 96, 0, 0, 255) && ok;
    } else {
        ok = pixel_is(96, 96, 0, 255, 0) && ok;
        ok = pixel_is(4, 4, 255, 0, 0) && ok;
    }
    glADestroyContext(second);
    glAMakeCurrent(first);
    glClearColor(0, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    ok = pixel_is(96, 96, 0, 255, 255) && ok;
    ok = glGetError() == GL_NO_ERROR && ok;
    glASwapBuffers(first);
    printf("survivor after second close: %s\n", ok ? "PASS" : "FAIL");

    second = new_context(win);
    if (second) {
        glAMakeCurrent(second);
        glADestroyContext(first);
        *first_context = NULL;
        glAMakeCurrent(second);
        glViewport(0, 0, 64, 64); glReadBuffer(GL_BACK);
        glClearColor(1, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        ok = pixel_is(32, 32, 255, 255, 0) && ok;
        ok = glGetError() == GL_NO_ERROR && ok;
        glASwapBuffers(second);
        printf("survivor after first close: %s\n", ok ? "PASS" : "FAIL");
        glADestroyContext(second);
        *first_context = new_context(first_window);
        if (*first_context) glAMakeCurrent(*first_context);
        else ok = 0;
    } else {
        puts("FAIL: recreate second simultaneous context");
        ok = 0;
    }
    CloseWindow(win);
    printf("simultaneous contexts: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static int pair_barrier(const char *name, ULONG role)
{
    struct MsgPort *reply = CreateMsgPort(), *parent;
    struct avgl_pair_ready ready = {0};
    if (!reply) return 0;
    ready.message.mn_Node.ln_Type = NT_MESSAGE;
    ready.message.mn_ReplyPort = reply;
    ready.message.mn_Length = sizeof(ready);
    ready.role = role;
    Forbid();
    parent = FindPort((STRPTR)name);
    if (parent) PutMsg(parent, &ready.message);
    Permit();
    if (!parent) { DeleteMsgPort(reply); return 0; }
    printf("pair ready: role=%lu\n", (unsigned long)role); fflush(stdout);
    WaitPort(reply);
    int okay = GetMsg(reply) == &ready.message;
    DeleteMsgPort(reply);
    return okay;
}

int main(int argc, char **argv)
{
    struct Window *win;
    GLAContext ctx;
    const char *renderer;
    unsigned char pixel[4] = {0};
    int ok;
    int empty = 0, single = 0, multi = 0, openonly = 0, refresh = 0, arg;
    int handles = 0, nativeonly = 0, log_error = -1, setrast = 0;
    const char *pair_port = NULL;
    int pair_role = -1;
    test_stencil = 1;
    fail_context = -1;
    for (arg = 2; arg < argc; ++arg) {
        if (strcmp(argv[arg], "empty") == 0) empty = 1;
        else if (strcmp(argv[arg], "single") == 0) single = 1;
        else if (strcmp(argv[arg], "nostencil") == 0) test_stencil = 0;
        else if (strcmp(argv[arg], "multi") == 0) multi = 1;
        else if (strcmp(argv[arg], "openonly") == 0) openonly = 1;
        else if (strcmp(argv[arg], "refresh") == 0) refresh = 1;
        else if (strcmp(argv[arg], "handles") == 0) handles = 1;
        else if (strcmp(argv[arg], "nativeonly") == 0) nativeonly = 1;
        else if (!strncmp(argv[arg], "logerror=", 9)) log_error = atoi(argv[arg] + 9);
        else if (!strncmp(argv[arg], "failcontext=", 12)) fail_context = atoi(argv[arg] + 12);
        else if (!strcmp(argv[arg], "setrast")) setrast = 1;
        else if (!strncmp(argv[arg], "pairport=", 9)) pair_port = argv[arg] + 9;
        else if (!strncmp(argv[arg], "pairrole=", 9)) pair_role = atoi(argv[arg] + 9);
        else { fprintf(stderr, "Unknown mode: %s\n", argv[arg]); return 10; }
    }
    if ((pair_port && (pair_role < 0 || pair_role > 1 || empty || multi || !single)) ||
        (!pair_port && pair_role != -1)) return 10;
    if (handles) {
        BPTR handle = 0;
        struct Process *process = (struct Process *)FindTask(NULL);
        int mapped = __get_default_file(1, (long *)&handle);
        /* Read the descriptor mapping without writing through that handle.
         * A resident GL library can retain posixc state across Shell commands.
         */
        FPrintf(Output(), "handles: output=%p fd1=%p mapped=%ld posixc=%p returnaddr=%p\n",
            (APTR)Output(), (APTR)handle, (LONG)mapped,
            __aros_getbase_PosixCBase(), process->pr_ReturnAddr);
        Flush(Output());
    }
    if (refresh) updatestdio();
    if (nativeonly) {
        FPrintf(Output(), "nativeonly: PASS\n");
        Flush(Output());
        return 0;
    }
    FPrintf(Output(), "native: output=%p stdout=%p stdout_fh=%p flags=%lx\n",
        (APTR)Output(), (APTR)stdout, (APTR)stdout->fh, (IPTR)stdout->flags);
    Flush(Output());
    if (openonly) { puts("openonly: PASS"); return 0; }
    printf("mode: empty=%d contexts=%d stencil=%d\n", empty, single ? 1 : 4, test_stencil);

    puts("probe: opening window");
    fflush(stdout);
    win = OpenWindowTags(NULL, WA_Title, (IPTR)"AROS renderer probe",
        WA_InnerWidth, 192, WA_InnerHeight, 192,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
        TAG_DONE);
    if (!win) return 10;
    puts("probe: creating context");
    fflush(stdout);
    ctx = new_context(win);
    if (fail_context >= 0) {
        int expected = ctx == NULL;
        if (ctx) glADestroyContext(ctx);
        printf("STDIO_CLIENT[failed-context-client-%02d]\n", fail_context);
        int flushed = fflush(stdout);
        printf("failed-context: result=%s flush=%d\n", expected ? "PASS" : "FAIL", flushed);
        CloseWindow(win);
        return expected && flushed == 0 ? 0 : 10;
    }
    if (!ctx) { puts("FAIL: context"); CloseWindow(win); return 10; }
    glAMakeCurrent(ctx);
    if (setrast) {
        struct RastPort invalid = {0};
        struct TagItem valid_tags[] = {{GLA_Window, (IPTR)win}, {TAG_DONE, 0}};
        struct TagItem invalid_tags[] = {{GLA_RastPort, (IPTR)&invalid}, {TAG_DONE, 0}};
        glASetRast(ctx, valid_tags);
        glASetRast(ctx, invalid_tags);
        puts("setrast: replaced valid target, rejected target without layer");
    }
    renderer = (const char *)glGetString(GL_RENDERER);
    printf("GL_RENDERER: %s\nGL_VERSION: %s\nGL_VENDOR: %s\n",
        renderer ? renderer : "(null)",
        (const char *)glGetString(GL_VERSION),
        (const char *)glGetString(GL_VENDOR));
    fflush(stdout);
    ok = renderer && argc > 1 && strstr(renderer, argv[1]);
    if (!ok) {
        puts("FAIL: unexpected renderer");
        glADestroyContext(ctx); CloseWindow(win); return 10;
    }
    SetWindowTitles(win, (STRPTR)renderer, (STRPTR)-1);
    if (log_error >= 0) {
        glEnable(0x71000000U + log_error);
        int expected = glGetError() == GL_INVALID_ENUM;
        printf("STDIO_CLIENT[logging-client-%02d]\n", log_error);
        int flushed = fflush(stdout);
        printf("log-error: expected=%s flush=%d\n", expected ? "PASS" : "FAIL", flushed);
        ok = expected && flushed == 0 && ok;
    }
    if (empty) goto cycles;
    glViewport(0, 0, 192, 192);
    glReadBuffer(GL_BACK);
    glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(96, 96, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("clear pixel: %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    ok = ok && pixel[0] >= 120 && pixel[0] <= 136 && pixel[1] < 8 && pixel[2] < 8;
    fflush(stdout);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.8f); glVertex2f(0.8f, -0.8f); glVertex2f(0.0f, 0.8f);
    glEnd();
    glReadPixels(96, 96, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    printf("triangle pixel: %u %u %u %u\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    ok = ok && pixel[0] < 8 && pixel[1] > 245 && pixel[2] < 8;
    ok = texture_and_blend() && ok;
    ok = depth_stencil() && ok;
    if (pair_port) {
        int survivor = pair_barrier(pair_port, pair_role);
        glClearColor(pair_role ? 1 : 0, 1, pair_role ? 0 : 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        survivor = pixel_is(96, 96, pair_role ? 255 : 0, 255, pair_role ? 0 : 255) && survivor;
        survivor = glGetError() == GL_NO_ERROR && survivor;
        printf("pair survivor: role=%d %s\n", pair_role, survivor ? "PASS" : "FAIL");
        ok = survivor && ok;
    }
    if (multi) {
        ok = simultaneous_context(&ctx, win, argv[1]) && ok;
        if (!ctx) { puts("FAIL: restore first context"); CloseWindow(win); return 10; }
    }
    {
        GLenum err = glGetError();
        printf("GL_ERROR: %u\n", (unsigned)err);
        ok = ok && err == GL_NO_ERROR;
    }
    glASwapBuffers(ctx);
    fflush(stdout);
    Delay(500);
cycles:
    {
        int cycle;
        for (cycle = 0; cycle < (single ? 0 : 3); ++cycle) {
            const char *name;
            glADestroyContext(ctx);
            ctx = new_context(win);
            if (!ctx) { puts("FAIL: recreate context"); ok = 0; break; }
            glAMakeCurrent(ctx);
            name = (const char *)glGetString(GL_RENDERER);
            ok = name && strstr(name, argv[1]) && ok;
            if (!empty) {
                glViewport(0, 0, 192, 192); glReadBuffer(GL_BACK);
                glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
                ok = pixel_is(96, 96, 0, 0, 255) && ok;
                ok = glGetError() == GL_NO_ERROR && ok;
            }
            printf("recreated context %d: renderer=%s\n", cycle + 1, name ? name : "(null)");
        }
    }
    puts("probe: destroying final context"); fflush(stdout);
    if (ctx) glADestroyContext(ctx);
    puts("probe: closing window"); fflush(stdout);
    CloseWindow(win);
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 10;
}
