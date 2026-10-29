/*
 * Copyright (C) 2026 John KNIPPER <code@jkn.me>
 *
 * Licensed under the AROS Public License (APL) Version 1.1
 *
 * Desc: Prove the dependency-complete core RTF_COLDSTART layer.
 */

#include <aros/apple/runtime.h>
#include <aros/apple/startup.h>
#include <aros/apple/bootinfo.h>
#include <aros/asmcall.h>
#include <aros/bootloader.h>

#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/tasks.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <devices/timer.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <dos/filehandler.h>
#include <dos/filesystemids.h>
#include <graphics/clip.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <aros/apple/platformtimer.h>
#include "cpu_monitor.h"
#include "guard_proof.h"
#include <aros/apple/crashlog.h>
#include <proto/kernel.h>
#include <aros/kernel.h>
#include <libraries/expansionbase.h>
#include <proto/exec.h>
#include <exec_platform.h>
#if defined(__AROSEXEC_SMP__)
#include "smp.h"
#endif
#include <resources/filesysres.h>
#include <resources/task.h>
#include <workbench/startup.h>

#define BootLoaderBase proof_bootloader_base
#include <proto/bootloader.h>

#define DOSBase proof_dos_base
#include <proto/dos.h>

#define __GRAPHICS_LIBBASE proof_graphics_base
#include <proto/graphics.h>

#define __INTUITION_NOLIBBASE__
#define __INTUITION_LIBBASE proof_intuition_base
#include <proto/intuition.h>

#define __TIMER_LIBBASE timer_device
#include <proto/timer.h>

static APTR proof_task_base;
#define TaskResBase proof_task_base
#include <proto/task.h>

#include <stdint.h>

#include "page_allocator.h"
#include "code_window.h"
#include "vm_irq.h"
#include "coldstart_platform.h"
#include <resources/kernel.h>
#include "kernel_intern.h"
#include "cpu_monitor.h"

static const struct ColdStartPlatform *const platform = COLDSTART_PLATFORM;
static struct AROSAppleColdStartProof *active_proof;
static struct DosLibrary *proof_dos_base;
static APTR proof_bootloader_base;
static struct GfxBase *proof_graphics_base;
static struct IntuitionBase *proof_intuition_base;
#define WIDGET_DESKTOP_VAR_ONLY
#include "../include/widgetstart.h"

struct graphics_probe_state {
    struct View view;
    struct ViewPort viewport;
    struct RasInfo rasinfo;
    struct RastPort rastport;
};

/*
 * SetRGB32() takes 32-bit components, and the colour reaching the aperture is
 * that value narrowed to the channel's width. For eight bits the operation is
 * the identity; for ten it rounds rather than shifting an 8-bit value up, so
 * 224 becomes 899 and not 896.
 */
static uint32_t probe_component(uint32_t component, uint32_t width)
{
    return (component * UINT32_C(0x01010101)) >> (32U - width);
}

static uint32_t graphics_probe_pixel(
    uint32_t argb, uint32_t framebuffer_format)
{
    uint32_t red = (argb >> 16) & UINT32_C(0xff);
    uint32_t green = (argb >> 8) & UINT32_C(0xff);
    uint32_t blue = argb & UINT32_C(0xff);

    if (framebuffer_format == AROS_BOOTINFO_FB_RGBX8888)
        return red | (green << 8) | (blue << 16);
    if (framebuffer_format == AROS_BOOTINFO_FB_X2R10G10B10) {
        return (probe_component(red, 10) << 20) |
            (probe_component(green, 10) << 10) |
            probe_component(blue, 10);
    }
    return blue | (green << 8) | (red << 16);
}

/* Every supported framebuffer format is one 32-bit pixel; the VesaInfo the
 * bootloader synthesizes for it is fixed by the format's own definition.
 */
static int graphics_probe_expected_masks(uint32_t framebuffer_format,
    ULONG *masks, ULONG *shifts)
{
    if (framebuffer_format == AROS_BOOTINFO_FB_X2R10G10B10) {
        masks[VI_Red] = 0x3ff00000UL;
        masks[VI_Green] = 0x000ffc00UL;
        masks[VI_Blue] = 0x000003ffUL;
        masks[VI_Alpha] = 0xc0000000UL;
        shifts[VI_Red] = 2;
        shifts[VI_Green] = 12;
        shifts[VI_Blue] = 22;
        shifts[VI_Alpha] = 0;
        return 1;
    }
    if (framebuffer_format != AROS_BOOTINFO_FB_RGBX8888 &&
        framebuffer_format != AROS_BOOTINFO_FB_BGRX8888)
        return 0;
    masks[VI_Green] = 0x0000ff00UL;
    masks[VI_Alpha] = 0xff000000UL;
    shifts[VI_Green] = 16;
    shifts[VI_Alpha] = 0;
    if (framebuffer_format == AROS_BOOTINFO_FB_RGBX8888) {
        masks[VI_Red] = 0x000000ffUL;
        masks[VI_Blue] = 0x00ff0000UL;
        shifts[VI_Red] = 24;
        shifts[VI_Blue] = 8;
    } else {
        masks[VI_Red] = 0x00ff0000UL;
        masks[VI_Blue] = 0x000000ffUL;
        shifts[VI_Red] = 8;
        shifts[VI_Blue] = 24;
    }
    return 1;
}

/* Bits one pixel of this format carries, or zero when it carries none we can
 * drive.  Kept beside the mask table so the two never disagree.
 */
static uint32_t graphics_probe_depth(uint32_t framebuffer_format)
{
    if (framebuffer_format == AROS_BOOTINFO_FB_RGBX8888 ||
        framebuffer_format == AROS_BOOTINFO_FB_BGRX8888)
        return 32;
    if (framebuffer_format == AROS_BOOTINFO_FB_X2R10G10B10)
        return 30;
    return 0;
}

/*
 * Build and display a View using the public graphics.library API, then prove
 * that its four RectFill() operations reached the inherited linear aperture.
 * The state intentionally remains allocated: LoadView() retains this View and
 * the candidate halts with it displayed after the complete cold-start proof.
 */
static uint32_t graphics_render_probe(
    const struct AROSAppleFramebufferInfo *framebuffer)
{
    static const uint32_t colors[4] = {
        UINT32_C(0xffff2020), UINT32_C(0xff20ff40),
        UINT32_C(0xff2040ff), UINT32_C(0xffffe020)
    };
    struct TagItem bitmap_tags[] = {
        {BMATags_DisplayID, INVALID_ID},
        {TAG_DONE, 0}
    };
    struct graphics_probe_state *state;
    struct BitMap *bitmap;
    volatile uint32_t *pixels;
    uint32_t sample_x[4];
    uint32_t sample_y[4];
    uint32_t mode;
    uint32_t index;

    if (framebuffer == NULL || proof_graphics_base == NULL ||
        framebuffer->width < 4 || framebuffer->height < 4 ||
        framebuffer->width > INT16_MAX || framebuffer->height > INT16_MAX ||
        graphics_probe_depth(framebuffer->format) == 0 ||
        framebuffer->depth != graphics_probe_depth(framebuffer->format))
        return UINT32_C(0x601);

    mode = BestModeID(
        BIDTAG_DesiredWidth, framebuffer->width,
        BIDTAG_DesiredHeight, framebuffer->height,
        BIDTAG_Depth, framebuffer->depth,
        TAG_DONE);
    if (mode == INVALID_ID)
        return UINT32_C(0x602);
    bitmap_tags[0].ti_Data = mode;
    bitmap = AllocBitMap(
        framebuffer->width, framebuffer->height, framebuffer->depth,
        BMF_CLEAR | BMF_REQUESTVMEM | BMF_CHECKVALUE,
        (struct BitMap *)bitmap_tags);
    if (bitmap == NULL)
        return UINT32_C(0x603);

    state = AllocMem(sizeof(*state), MEMF_PUBLIC | MEMF_CLEAR);
    if (state == NULL) {
        FreeBitMap(bitmap);
        return UINT32_C(0x604);
    }
    InitView(&state->view);
    InitVPort(&state->viewport);
    InitRastPort(&state->rastport);
    state->rastport.BitMap = bitmap;
    state->rasinfo.BitMap = bitmap;
    state->viewport.RasInfo = &state->rasinfo;
    state->viewport.DWidth = framebuffer->width;
    state->viewport.DHeight = framebuffer->height;
    state->view.ViewPort = &state->viewport;

    if (MakeVPort(&state->view, &state->viewport) != MVP_OK) {
        FreeBitMap(bitmap);
        FreeMem(state, sizeof(*state));
        return UINT32_C(0x605);
    }
    if (MrgCop(&state->view) != MCOP_OK) {
        FreeBitMap(bitmap);
        FreeMem(state, sizeof(*state));
        return UINT32_C(0x606);
    }
    LoadView(&state->view);

    for (index = 0; index < 4; ++index) {
        uint32_t red = (colors[index] >> 16) & UINT32_C(0xff);
        uint32_t green = (colors[index] >> 8) & UINT32_C(0xff);
        uint32_t blue = colors[index] & UINT32_C(0xff);

        SetRGB32(&state->viewport, index,
            red * UINT32_C(0x01010101),
            green * UINT32_C(0x01010101),
            blue * UINT32_C(0x01010101));
    }
    SetAPen(&state->rastport, 0);
    RectFill(&state->rastport, 0, 0,
        framebuffer->width / 2U - 1U, framebuffer->height / 2U - 1U);
    SetAPen(&state->rastport, 1);
    RectFill(&state->rastport, framebuffer->width / 2U, 0,
        framebuffer->width - 1U, framebuffer->height / 2U - 1U);
    SetAPen(&state->rastport, 2);
    RectFill(&state->rastport, 0, framebuffer->height / 2U,
        framebuffer->width / 2U - 1U, framebuffer->height - 1U);
    SetAPen(&state->rastport, 3);
    RectFill(&state->rastport, framebuffer->width / 2U,
        framebuffer->height / 2U, framebuffer->width - 1U,
        framebuffer->height - 1U);

    sample_x[0] = framebuffer->width / 4U;
    sample_y[0] = framebuffer->height / 4U;
    sample_x[1] = framebuffer->width * 3U / 4U;
    sample_y[1] = framebuffer->height / 4U;
    sample_x[2] = framebuffer->width / 4U;
    sample_y[2] = framebuffer->height * 3U / 4U;
    sample_x[3] = framebuffer->width * 3U / 4U;
    sample_y[3] = framebuffer->height * 3U / 4U;
    pixels = (volatile uint32_t *)(uintptr_t)framebuffer->physical_address;
    for (index = 0; index < 4; ++index) {
        uint32_t expected = graphics_probe_pixel(
            colors[index], framebuffer->format);
        uint64_t offset = (uint64_t)sample_y[index] * framebuffer->pitch +
            (uint64_t)sample_x[index] * sizeof(*pixels);

        if (offset > framebuffer->size - sizeof(*pixels) ||
            pixels[offset / sizeof(*pixels)] != expected)
            return UINT32_C(0x607) + index * 2U;
        /*
         * graphics.library currently exposes only the low native byte from
         * ReadPixel() on truecolor HIDD bitmaps. Keep that public-API check,
         * while the aperture comparison above proves the complete pixel.
         */
        if ((uint32_t)ReadPixel(&state->rastport,
                sample_x[index], sample_y[index]) !=
            (expected & UINT32_C(0xff)))
            return UINT32_C(0x608) + index * 2U;
    }
    return 0;
}

/*
 * Open an ordinary custom Screen and bordered Window through Intuition, then
 * render two deterministic content colors through the Window's layer-backed
 * RastPort.  The objects intentionally remain open so the final QMP capture is
 * evidence of Intuition's screen/window path rather than a restored G2 View.
 */
static uint32_t intuition_render_probe(
    const struct AROSAppleFramebufferInfo *framebuffer)
{
    static const uint32_t background = UINT32_C(0xff0c1830);
    static const uint32_t inner = UINT32_C(0xff20d0e0);
    struct Screen *screen;
    struct Window *window;
    volatile uint32_t *pixels;
    uint32_t mode;
    uint32_t local_x;
    uint32_t local_y;
    uint32_t global_x;
    uint32_t global_y;
    uint32_t expected;
    ULONG screen_error = 0;
    uint64_t offset;

    if (framebuffer == NULL || proof_intuition_base == NULL ||
        framebuffer->width < 640 || framebuffer->height < 480)
        return UINT32_C(0x701);
    if (OpenLibrary("intuition.library", 0) !=
        (struct Library *)proof_intuition_base)
        return UINT32_C(0x702);

    mode = BestModeID(
        BIDTAG_DesiredWidth, framebuffer->width,
        BIDTAG_DesiredHeight, framebuffer->height,
        BIDTAG_Depth, framebuffer->depth,
        TAG_DONE);
    if (mode == INVALID_ID)
        return UINT32_C(0x703);
    screen = OpenScreenTags(NULL,
        SA_DisplayID, mode,
        SA_Width, framebuffer->width,
        SA_Height, framebuffer->height,
        SA_Type, CUSTOMSCREEN,
        SA_Title, (IPTR)"AROS Apple Silicon G3",
        SA_ShowTitle, TRUE,
        SA_ErrorCode, (IPTR)&screen_error,
        TAG_DONE);
    if (screen == NULL)
        return UINT32_C(0x740) + screen_error;
    if (screen->Width != framebuffer->width ||
        screen->Height != framebuffer->height ||
        screen->RastPort.BitMap == NULL)
        return UINT32_C(0x705);

    SetRGB32(&screen->ViewPort, 0,
        UINT32_C(0x0c0c0c0c), UINT32_C(0x18181818),
        UINT32_C(0x30303030));
    SetAPen(&screen->RastPort, 0);
    RectFill(&screen->RastPort, 0, 0,
        framebuffer->width - 1U, framebuffer->height - 1U);

    window = OpenWindowTags(NULL,
        WA_CustomScreen, (IPTR)screen,
        WA_Left, 80,
        WA_Top, 70,
        WA_Width, framebuffer->width - 160U,
        WA_Height, framebuffer->height - 140U,
        WA_Title, (IPTR)"Intuition G3 screen + window",
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET |
            WFLG_CLOSEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, 0,
        TAG_DONE);
    if (window == NULL)
        return UINT32_C(0x706);
    if (window->WScreen != screen || screen->FirstWindow != window ||
        window->RPort == NULL || window->WLayer == NULL ||
        window->Width != framebuffer->width - 160U ||
        window->Height != framebuffer->height - 140U)
        return UINT32_C(0x707);

    SetRGB32(&screen->ViewPort, 4,
        UINT32_C(0xf0f0f0f0), UINT32_C(0x70707070),
        UINT32_C(0x20202020));
    SetRGB32(&screen->ViewPort, 5,
        UINT32_C(0x20202020), UINT32_C(0xd0d0d0d0),
        UINT32_C(0xe0e0e0e0));
    SetAPen(window->RPort, 4);
    RectFill(window->RPort,
        window->BorderLeft, window->BorderTop,
        window->Width - window->BorderRight - 1,
        window->Height - window->BorderBottom - 1);
    SetAPen(window->RPort, 5);
    RectFill(window->RPort,
        window->BorderLeft + 48, window->BorderTop + 56,
        window->Width - window->BorderRight - 49,
        window->Height - window->BorderBottom - 57);

    local_x = window->Width / 2U;
    local_y = window->Height / 2U;
    global_x = window->LeftEdge + local_x;
    global_y = window->TopEdge + local_y;
    expected = graphics_probe_pixel(inner, framebuffer->format);
    offset = (uint64_t)global_y * framebuffer->pitch +
        (uint64_t)global_x * sizeof(*pixels);
    pixels = (volatile uint32_t *)(uintptr_t)framebuffer->physical_address;
    if (offset > framebuffer->size - sizeof(*pixels) ||
        pixels[offset / sizeof(*pixels)] != expected)
        return UINT32_C(0x708);
    if ((uint32_t)ReadPixel(window->RPort, local_x, local_y) !=
        (expected & UINT32_C(0xff)))
        return UINT32_C(0x709);

    expected = graphics_probe_pixel(background, framebuffer->format);
    global_x = framebuffer->width - 16U;
    global_y = framebuffer->height - 16U;
    offset = (uint64_t)global_y * framebuffer->pitch +
        (uint64_t)global_x * sizeof(*pixels);
    if (offset > framebuffer->size - sizeof(*pixels) ||
        pixels[offset / sizeof(*pixels)] != expected)
        return UINT32_C(0x70a);
    return 0;
}

static int payload_digest_nonzero(const uint8_t digest[32])
{
    uint8_t value = 0;
    uint32_t index;

    for (index = 0; index < 32; ++index)
        value |= digest[index];
    return value != 0;
}

static int payload_identity_equal(
    const struct AROSApplePayloadIdentity *left,
    const struct AROSApplePayloadIdentity *right)
{
    uint8_t difference = 0;
    uint32_t index;

    for (index = 0; index < 32; ++index)
        difference |= left->sha256[index] ^ right->sha256[index];
    return difference == 0 &&
        left->generation_sequence == right->generation_sequence &&
        left->size == right->size && left->slot == right->slot &&
        left->flags == right->flags && left->producer == right->producer &&
        left->reserved == right->reserved;
}

AROS_UFP3S(APTR, dos_takeover_observer,
    AROS_UFPA(ULONG, dummy, D0),
    AROS_UFPA(BPTR, seg_list, A0),
    AROS_UFPA(struct ExecBase *, sys_base, A6));

/*
 * The desktop profile is recognised by what it carries, not by how many
 * residents it counts: muimaster.library and con-handler ship only in the
 * desktop bundle. Its exact resident order lives in desktop_residents[], so
 * adding a component to the desktop means adding one row there and one line
 * to the bundle, nothing else. Set once, before the resident checks.
 */
static int vm_graphics_profile_flag;
static int vm_pci_profile_flag;
static int desktop_profile_flag;

/* Built only after the ROM scan so it cannot alter the production resident set. */
static struct Resident dos_takeover_observer_resident;

static const UBYTE sentinel_expected[] =
    "AROS Apple Silicon B2.8 boot volume\n";
static const UBYTE boot_signature_expected[] = "apple-aarch64\n";
static const char *const standard_assigns[] = {
    "C:", "LIBS:", "DEVS:", "L:", "S:", "FONTS:"
};

static int name_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int bytes_equal(
    const UBYTE *left, const UBYTE *right, uint32_t length)
{
    uint32_t index;

    for (index = 0; index < length; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

struct workbench_exit_state {
    volatile IPTR result;
    volatile uint32_t called;
};

static void workbench_probe_exit(IPTR result, IPTR data)
{
    struct workbench_exit_state *state =
        (struct workbench_exit_state *)data;

    state->result = result;
    state->called = 1;
}

/*
 * The RAM image's marker and probe files. After BootStartup hands SYS: to the
 * installed AFS+ volume they are still read from the RAM volume, through its
 * own device name SYS0:; before the hand-over SYS: is that volume.
 */
static int proof_pivoted;

static const char *marker_path(char *buffer, const char *name)
{
    const char *prefix = proof_pivoted ? "SYS0:" : "SYS:";
    char *out = buffer;

    while (*prefix != '\0')
        *out++ = *prefix++;
    while (*name != '\0')
        *out++ = *name++;
    *out = '\0';
    return buffer;
}

static int workbench_probe_required(void)
{
    static const UBYTE expected[] = "workbench-abi-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "WBTEST.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

static int tls_probe_required(void)
{
    static const UBYTE expected[] = "tls-concurrency-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "TLSTEST.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

static int small_probe_required(void)
{
    static const UBYTE expected[] = "small-model-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "SMLTEST.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

static int settings_probe_required(void)
{
    static const UBYTE expected[] = "settings-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "SETTEST.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

/* PWRTEST.REQ: the [PWR7] freeze probe is wanted on this image. */
static int power_probe_required(void)
{
    static const UBYTE expected[] = "power-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "PWRTEST.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

static int wanderer_required(void)
{
    static const UBYTE expected[] = "wanderer-ram-v1\n";
    UBYTE contents[sizeof(expected) - 1U];
    char path[32];
    BPTR file = Open(marker_path(path, "WANDER.REQ"), MODE_OLDFILE);
    LONG actual;
    LONG eof;

    if (file == BNULL)
        return 0;
    actual = Read(file, contents, sizeof(contents));
    eof = actual == (LONG)sizeof(contents) ? Read(file, contents, 1) : -1;
    Close(file);
    return actual == (LONG)sizeof(contents) && eof == 0 &&
        bytes_equal(contents, expected, sizeof(contents)) ? 1 : -1;
}

static int run_workbench_probe(
    struct AROSAppleColdStartProof *proof, BPTR sys_lock)
{
    static const char probe_name[] = "WBPROBE";
    struct workbench_exit_state exit_state = {0};
    struct WBArg argument = {0};
    struct WBStartup startup = {0};
    struct MsgPort *reply_port = NULL;
    struct Message *reply = NULL;
    struct Process *child = NULL;
    char path[32];
    BPTR probe = BNULL;
    ULONG previous_child_signal;
    int valid = 0;
    static struct TagItem launch_tags[] = {
        { NP_Seglist, 0 },
        { NP_Name, (IPTR)probe_name },
        { NP_StackSize, AROS_STACKSIZE },
        { NP_Input, (IPTR)BNULL },
        { NP_Output, (IPTR)BNULL },
        { NP_Error, (IPTR)BNULL },
        { NP_CurrentDir, (IPTR)BNULL },
        { NP_HomeDir, (IPTR)BNULL },
        { NP_CopyVars, (IPTR)FALSE },
        { NP_FreeSeglist, (IPTR)FALSE },
        { NP_NotifyOnDeath, (IPTR)TRUE },
        { NP_ExitCode, (IPTR)workbench_probe_exit },
        { NP_ExitData, 0 },
        { TAG_DONE, 0 }
    };

    if (sys_lock == BNULL)
        return 0;
    probe = LoadSeg(marker_path(path, "WBPROBE"));
    if (probe == BNULL)
        return 0;
    launch_tags[0].ti_Data = (IPTR)probe;
    launch_tags[12].ti_Data = (IPTR)&exit_state;
    reply_port = CreateMsgPort();
    if (reply_port == NULL)
        goto done;

    previous_child_signal = SetSignal(0, SIGF_CHILD) & SIGF_CHILD;
    child = CreateNewProc(launch_tags);
    if (child == NULL) {
        SetSignal(previous_child_signal, SIGF_CHILD);
        goto done;
    }

    argument.wa_Lock = sys_lock;
    argument.wa_Name = (STRPTR)probe_name;
    startup.sm_Message.mn_Node.ln_Type = NT_MESSAGE;
    startup.sm_Message.mn_Length = sizeof(startup);
    startup.sm_Message.mn_ReplyPort = reply_port;
    startup.sm_Process = &child->pr_MsgPort;
    startup.sm_Segment = probe;
    startup.sm_NumArgs = 1;
    startup.sm_ArgList = &argument;
    PutMsg(startup.sm_Process, &startup.sm_Message);
    WaitPort(reply_port);
    reply = GetMsg(reply_port);
    Wait(SIGF_CHILD);
    SetSignal(previous_child_signal, SIGF_CHILD);

    proof->workbench_probe_seglist = (uintptr_t)BADDR(probe);
    proof->workbench_probe_process = (uintptr_t)child;
    proof->workbench_probe_reply = (uintptr_t)reply;
    proof->workbench_probe_return = (uint64_t)(int64_t)exit_state.result;
    proof->workbench_probe_numargs = startup.sm_NumArgs;
    valid = reply == &startup.sm_Message && exit_state.called == 1 &&
        exit_state.result == 73 && startup.sm_NumArgs == 1;

done:
    if (reply_port != NULL)
        DeleteMsgPort(reply_port);
    if (probe != BNULL)
        UnLoadSeg(probe);
    return valid;
}

struct wanderer_launch_state {
    struct WBArg argument;
    struct WBStartup startup;
    struct MsgPort *reply_port;
    struct Process *process;
    BPTR seglist;
};

static struct wanderer_launch_state wanderer_launch;
static struct Process *wanderer_launcher_process;
static struct Task *wanderer_initial_cli_task;
static BPTR wanderer_ready_seglist;

static uint32_t initialize_wanderer_components(void);

/*
 * The deterministic Startup-Sequence invokes this internal proof command
 * immediately before EndCLI.  Raising the already-created launcher here
 * proves that DOS and the real Shell read and executed the RAM-disk script;
 * no scheduler timing heuristic stands in for that event.
 */
/* Keep this entry global so the assembly segment carries a relocation against
 * the function symbol itself.  A local function is emitted as .text+0 by
 * Clang; the AROS kernel's partial-link step then loses the input-section
 * offset and turns the segment jump into `b .`. */
AROS_UFH2(LONG, AROSAppleG4ReadyEntry,
    AROS_UFHA(char *, argstr, A0),
    AROS_UFHA(ULONG, argsize, D0))
{
    AROS_USERFUNC_INIT

    (void)argstr;
    (void)argsize;
    if (active_proof == NULL || wanderer_launcher_process == NULL)
        return RETURN_FAIL;
    wanderer_initial_cli_task = FindTask(NULL);
    active_proof->wanderer_initial_cli_process =
        (uintptr_t)wanderer_initial_cli_task;
    active_proof->wanderer_initial_cli_seen = 1;
    SetTaskPri(&wanderer_launcher_process->pr_Task, 1);
    Signal(&wanderer_launcher_process->pr_Task, SIGBREAKF_CTRL_F);
    return RETURN_OK;

    AROS_USERFUNC_EXIT
}

/* AddSegment() expects a BPTR link word immediately followed by executable
 * entry code.  Keep both in the kernel's RX text mapping; a writable phony
 * segment would violate the Apple port's W^X contract. */
__asm__(
    ".pushsection .text.aros_apple_g4ready,\"ax\",@progbits\n"
    ".balign 8\n"
    ".global AROSAppleG4ReadySegment\n"
    "AROSAppleG4ReadySegment:\n"
    ".xword 0\n"
    "b AROSAppleG4ReadyEntry\n"
    ".popsection\n");
extern BPTR AROSAppleG4ReadySegment;

/*
 * Shell temporarily replaces its task name with each command name while
 * RunCommand() is active.  Consequently FindTask("Initial CLI") returning
 * NULL only proves that G4Ready or EndCLI is running, not that the boot Shell
 * has exited.  Compare the stable task address captured by G4Ready against
 * task.resource's locked running/ready/waiting view.  This avoids both the
 * transient command name and any dependency on Exec's private UP/SMP lists.
 */
static int task_is_live(const struct Task *task)
{
    struct TaskList *task_list;
    struct Task *candidate;
    int live = 0;

    if (task == NULL)
        return 0;
    if (proof_task_base == NULL)
        return 1;
    task_list = LockTaskList(LTF_ALL);
    if (task_list == NULL)
        return 1;
    while ((candidate = NextTaskEntry(task_list, LTF_ALL)) != NULL) {
        if (candidate == task) {
            live = 1;
            break;
        }
    }
    UnLockTaskList(task_list, LTF_ALL);
    return live;
}

/*
 * Launch the real desktop as a Workbench process and retain its message,
 * lock, segment and reply port until the observer exits to stage two.  The
 * G4 candidate intentionally halts with Wanderer alive for the final capture.
 */
static void hold_puts(const char *text);

/* Crashes the kernel suspended so far (aros/apple/crashlog.h). */
static uint64_t crash_suspended(void)
{
    IPTR attr = KrnGetSystemAttr(AROS_APPLE_KATTR_CRASH_LOG);
    const struct AROSAppleCrashLog *log = (attr != (IPTR)-1 && attr != 0) ?
        (const struct AROSAppleCrashLog *)attr : NULL;

    return log != NULL && log->version == AROS_APPLE_CRASH_LOG_VERSION ?
        log->suspended : 0;
}

static int run_wanderer(
    struct AROSAppleColdStartProof *proof, BPTR sys_lock)
{
    int fault_required = 0;
    static const char program_name[] = "Wanderer";
    /* Wanderer opens datatypes from its own task, and a datatype's first
       open runs the InitLib of its whole library chain on this stack
       (png.datatype, png.library, z1.library, posixc.library) on top of
       MUI's. Past its stack, the switch-time stack check suspends
       Wanderer for good while it holds the loader's lock, and every
       library open in the system waits behind it. */
    static struct TagItem launch_tags[] = {
        { NP_Seglist, 0 },
        { NP_Name, (IPTR)program_name },
        { NP_Priority, 1 },
        { NP_StackSize, 256U * 1024U },
        { NP_Input, (IPTR)BNULL },
        { NP_Output, (IPTR)BNULL },
        { NP_Error, (IPTR)BNULL },
        { NP_CurrentDir, (IPTR)BNULL },
        { NP_HomeDir, (IPTR)BNULL },
        { NP_CopyVars, (IPTR)FALSE },
        { NP_FreeSeglist, (IPTR)FALSE },
        { NP_NotifyOnDeath, (IPTR)FALSE },
        { TAG_DONE, 0 }
    };
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    BPTR home_lock;
    uint32_t attempt;
    uint32_t stable = 0;

    if (proof == NULL || sys_lock == BNULL ||
        wanderer_launch.process != NULL)
        return 0;
    proof->detail = UINT32_C(0x901);
    /* The desktop the Startup-Sequence chose, through its WANDERER:
     * assign (the standard one names SYS:System/Wanderer); the RAM
     * image's SYS:Wanderer when no assign names another. */
    wanderer_launch.seglist = LoadSeg("WANDERER:Wanderer");
    if (wanderer_launch.seglist == BNULL)
        wanderer_launch.seglist = LoadSeg("SYS:WANDERER");
    if (wanderer_launch.seglist == BNULL)
        return 0;
    proof->detail = UINT32_C(0x902);
    launch_tags[0].ti_Data = (IPTR)wanderer_launch.seglist;
    home_lock = DupLock(sys_lock);
    if (home_lock == BNULL) {
        proof->detail = UINT32_C(0x906);
        return 0;
    }
    launch_tags[8].ti_Data = (IPTR)home_lock;
    wanderer_launch.reply_port = CreateMsgPort();
    if (wanderer_launch.reply_port == NULL) {
        UnLock(home_lock);
        return 0;
    }
    proof->detail = UINT32_C(0x903);
    wanderer_launch.process = CreateNewProc(launch_tags);
    if (wanderer_launch.process == NULL) {
        UnLock(home_lock);
        return 0;
    }
    proof->detail = UINT32_C(0x904);

    wanderer_launch.argument.wa_Lock = sys_lock;
    wanderer_launch.argument.wa_Name = (STRPTR)program_name;
    wanderer_launch.startup.sm_Message.mn_Node.ln_Type = NT_MESSAGE;
    wanderer_launch.startup.sm_Message.mn_Length =
        sizeof(wanderer_launch.startup);
    wanderer_launch.startup.sm_Message.mn_ReplyPort =
        wanderer_launch.reply_port;
    wanderer_launch.startup.sm_Process =
        &wanderer_launch.process->pr_MsgPort;
    wanderer_launch.startup.sm_Segment = wanderer_launch.seglist;
    wanderer_launch.startup.sm_NumArgs = 1;
    wanderer_launch.startup.sm_ArgList = &wanderer_launch.argument;
    PutMsg(wanderer_launch.startup.sm_Process,
        &wanderer_launch.startup.sm_Message);
    proof->detail = UINT32_C(0x905);

    /* The task-fault qualification: the fixture carries FAULT.REQ and its
     * startup runs a command that crashes in a process of its own. The
     * kernel suspends that process (task_crash.c): the crash log then
     * counts a suspended crash, and the desktop stays exactly as the
     * ordinary run requires it, backdrop in front and no requester, with
     * the Initial CLI gone as always. */
    {
        BPTR requirement = Lock("SYS:FAULT.REQ", SHARED_LOCK);
        fault_required = requirement != BNULL;
        if (requirement != BNULL)
            UnLock(requirement);
    }

    /* Let Initial CLI finish EndCLI and let the priority-one Wanderer reach
     * its normal event wait before the observer tests final foreground state. */
    SetTaskPri(FindTask(NULL), -1);
    Reschedule();

    for (attempt = 0; attempt < 1024U && stable < 64U; ++attempt) {
        ULONG intuition_lock;
        struct Window *candidate;
        struct Window *front_window;
        int requester = 0;
        int failure_in_front = 0;

        /* Opening Wanderer performs asynchronous graphics, filesystem and
         * datatype work on both VM and native targets. A bare Reschedule()
         * loop can consume all 1024 observations before those tasks finish,
         * especially when the installed Workbench uses a scaled screen. Pace
         * the observer with real timer ticks so the bound measures elapsed
         * time and retains all readiness conditions plus 64 stable samples. */
        Delay(1);
        intuition_lock = LockIBase(0);
        screen = proof_intuition_base != NULL ?
            proof_intuition_base->FirstScreen : NULL;
        front_window = screen != NULL ? screen->FirstWindow : NULL;
        window = NULL;
        for (candidate = screen != NULL ? screen->FirstWindow : NULL;
            candidate != NULL; candidate = candidate->NextWindow) {
            if (candidate->Title != NULL &&
                (name_equal((const char *)candidate->Title,
                    "System requester") ||
                name_equal((const char *)candidate->Title,
                    "Software Failure!")))
                requester = 1;
            if (candidate == front_window && candidate->Title != NULL &&
                name_equal((const char *)candidate->Title, "Software Failure!"))
                failure_in_front = 1;
            if (screen != NULL && candidate->LeftEdge == 0 &&
                candidate->Title == NULL &&
                candidate->Width == screen->Width &&
                candidate->Height + candidate->TopEdge >= screen->Height - 1 &&
                candidate->RPort != NULL && candidate->WLayer != NULL)
                window = candidate;
        }
        UnlockIBase(intuition_lock);
        if (proof->wanderer_initial_cli_closed == 0 &&
            proof->wanderer_initial_cli_seen == 1 &&
            proof->wanderer_initial_cli_process != 0 &&
            !task_is_live(wanderer_initial_cli_task))
            proof->wanderer_initial_cli_closed = 1;
        if (FindTask(program_name) != NULL && window != NULL &&
            window == front_window && !requester && !failure_in_front &&
            (!fault_required || crash_suspended() != 0) &&
            proof->wanderer_initial_cli_closed == 1)
            ++stable;
        else
            stable = 0;
    }
    if (fault_required && stable == 64U)
        hold_puts("[G4F] task fault suspended, desktop alive PASS\n");

    proof->wanderer_seglist = (uintptr_t)BADDR(wanderer_launch.seglist);
    proof->wanderer_process = (uintptr_t)FindTask(program_name);
    proof->wanderer_screen = (uintptr_t)screen;
    proof->wanderer_window = (uintptr_t)window;
    if (window != NULL && window->RPort != NULL)
        proof->wanderer_pixel = UINT64_C(0x8000000000000000) |
            (uint32_t)ReadPixel(window->RPort,
                window->Width / 2U, window->Height / 2U);
    return stable == 64U && proof->wanderer_process != 0 &&
        proof->wanderer_screen != 0 && proof->wanderer_window != 0 &&
        proof->wanderer_pixel != 0;
}

/*
 * Once the desktop is stable the loader used to get the CPU back, which
 * froze every AROS task behind a painted screen. A usable desktop keeps the
 * scheduler alive instead: a Shell is opened on it and the launcher parks
 * itself below every other task, yielding, for a bounded window. It then
 * returns to the loader so the run still ends in the terminal record the
 * verdict is built on. An explicit desktop-hold-forever=1 instead keeps the
 * installed session running; zero and absent retain bounded gate behavior.
 */
#define HOLD_PROPERTY "aros,desktop-hold-seconds"
#define HOLD_FOREVER_PROPERTY "aros,desktop-hold-forever"
#define HOLD_MAX_SECONDS UINT64_C(3600)

extern int krnPutC(int chr, struct KernelBase *KernelBase);
extern uint64_t AROSAppleKernelChosenDecimal(const char *name);
extern int AROSAppleKernelChosenProperty(
    const void *fdt, uint64_t size, const char *name,
    const uint8_t **value, uint32_t *length);
extern int AROSAppleKernelRetainedDeviceTree(const void **fdt, uint64_t *size);
extern void AROSAppleKernelPublishSystemTime(
    const struct AROSAppleSystemTimeInfo *info);

static void hold_zero(void *target, size_t size)
{
    volatile uint8_t *bytes = target;
    size_t index;

    for (index = 0; index < size; ++index)
        bytes[index] = 0;
}
extern void AROSAppleKernelScreenLogStop(void);


#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
static volatile uint32_t irq_probe_first, irq_probe_second;
static void irq_probe_callback(void *data, void *cookie)
{
    volatile uint32_t *count = data;
    if (cookie != (void *)(uintptr_t)0x495251) *count = UINT32_MAX;
    else ++*count;
}
#endif

static void hold_puts(const char *text)
{
    while (*text != 0)
        krnPutC(*text++, NULL);
}

static void hold_put_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;

    krnPutC('0', NULL);
    krnPutC('x', NULL);
    for (shift = 60; shift >= 0; shift -= 4)
        krnPutC(digits[(value >> shift) & 0xfU], NULL);
}

static void hold_put_decimal(uint64_t value)
{
    char digits[21];
    int length = 0;

    do {
        digits[length++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0);
    while (length > 0)
        krnPutC(digits[--length], NULL);
}

/* Called directly by Supervisor from EL0, before any nested exception. The
 * architectural saved status is the caller's, unlike DAIF at EL1 (masked by
 * exception entry), or the dispatch snapshot (before Wait's final Enable). */
static uintptr_t proof_caller_spsr(void)
{
    uint64_t spsr;
    __asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
    return (uintptr_t)spsr;
}

/*
 * The return mask rule, observed through timer.device on a machine whose
 * deadline timer is live: a task that waited inside Disable() resumes with
 * I and F set; a task that waited plainly while the timer stays armed
 * resumes with the platform-ready lines unmasked. Prints one line; returns
 * 0 on a violation (FIQ on physical Apple, IRQ on the VM).
 */
extern volatile uint64_t AROSAppleKernelLastDispatchSPSR;
extern volatile uint64_t AROSAppleKernelIdleEntries;

static int prove_dispatch_masks(struct timerequest *request, ULONG frequency)
{
    struct KrnPlatformTimer *platform;
    struct timerequest keep;
    uint64_t disabled_spsr;
    uint64_t plain_spsr;
    uint64_t expected_plain;
    int passed;

    platform = (struct KrnPlatformTimer *)KrnGetSystemAttr(KATTR_PlatformTimer);
    if (frequency == 0 || platform == NULL || platform == (APTR)-1 ||
            (platform->kpt_Flags & KPTF_DEADLINE_READY) == 0) {
        hold_puts("[KI] dispatch masks: no deadline timer, not proven\n");
        return 1;
    }
    /* A long request keeps the timer armed across the short waits. */
    keep = *request;
    keep.tr_node.io_Command = TR_ADDREQUEST;
    keep.tr_time.tv_secs = 2;
    keep.tr_time.tv_micro = 0;
    SendIO(&keep.tr_node);
    Disable();
    request->tr_node.io_Command = TR_ADDREQUEST;
    request->tr_time.tv_secs = 0;
    request->tr_time.tv_micro = 20000;
    passed = DoIO(&request->tr_node) == 0;
    disabled_spsr = AROSAppleRuntimeSupervisor(proof_caller_spsr);
    Enable();
    request->tr_node.io_Command = TR_ADDREQUEST;
    request->tr_time.tv_secs = 0;
    request->tr_time.tv_micro = 20000;
    passed = DoIO(&request->tr_node) == 0 && passed;
    plain_spsr = AROSAppleRuntimeSupervisor(proof_caller_spsr);
    expected_plain = (AROSAppleRuntimeLocal()->irq_ready ? 0 : UINT64_C(0x80)) |
        (AROSAppleRuntimeLocal()->fiq_ready ? 0 : UINT64_C(0x40));
    AbortIO(&keep.tr_node);
    WaitIO(&keep.tr_node);
    /* WaitIO's final Enable makes an SVC and may repair a bad dispatch mask.
     * Check the dispatch snapshot itself, and prove an idle entry occurred,
     * before checking continued delivery in a syscall-free compute loop. */
    keep.tr_node.io_Command = TR_ADDREQUEST;
    keep.tr_time.tv_secs = 0;
    keep.tr_time.tv_micro = 100000;
    SendIO(&keep.tr_node);
    request->tr_node.io_Command = TR_ADDREQUEST;
    request->tr_time.tv_secs = 0;
    request->tr_time.tv_micro = 10000;
    uint64_t idle_before = AROSAppleKernelIdleEntries;
    passed = DoIO(&request->tr_node) == 0 && passed;
    uint64_t wake_spsr = AROSAppleKernelLastDispatchSPSR;
    uint64_t idle_after = AROSAppleKernelIdleEntries;
    /* Wait saves the task with Disable nesting held; its dispatch must keep
     * I/F masked until Wait's tail Enable. This is a delivery/lifecycle check,
     * not proof of the nesting=-1 dispatch policy (host regression covers it). */
    int wake_passed = idle_after > idle_before &&
        (wake_spsr & UINT64_C(0xc0)) == UINT64_C(0xc0);
    volatile UBYTE *reply_type = &keep.tr_node.io_Message.mn_Node.ln_Type;
    int pending = *reply_type != NT_REPLYMSG;
    uint64_t began, current;
    __asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(began));
    do {
        __asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(current));
    } while (*reply_type != NT_REPLYMSG && current-began < frequency/4U);
    int compute_passed = wake_passed && pending && *reply_type == NT_REPLYMSG;
    if (!wake_passed) {
        hold_puts("[KI] idle wake dispatch masks="); hold_put_hex(wake_spsr);
        hold_puts(" expected="); hold_put_hex(UINT64_C(0xc0));
        hold_puts(" idle-before="); hold_put_hex(idle_before);
        hold_puts(" idle-after="); hold_put_hex(idle_after);
        hold_puts("\n");
    }
    if (!compute_passed) AbortIO(&keep.tr_node);
    WaitIO(&keep.tr_node);
    hold_puts(compute_passed ? "[KI] idle wake compute PASS\n" :
        "[KI] idle wake compute FAIL\n");
    passed = passed && compute_passed && (disabled_spsr & UINT64_C(0xc0)) == UINT64_C(0xc0) &&
        (plain_spsr & UINT64_C(0xc0)) == expected_plain;
    hold_puts("[KI] dispatch masks: disabled ");
    hold_put_hex(disabled_spsr & UINT64_C(0xc0));
    hold_puts(" plain ");
    hold_put_hex(plain_spsr & UINT64_C(0xc0));
    hold_puts(passed ? " PASS\n" : " FAIL\n");
    return passed;
}

/* Where a curated component lives, so an exception address can be placed. */
static void log_component(const char *name)
{
    struct Resident *resident = FindResident(name);
    struct Node *node = FindName(&SysBase->LibList, name);

    if (node == NULL)
        node = FindName(&SysBase->DeviceList, name);
    hold_puts("[KI] component ");
    hold_puts(name);
    hold_puts(" resident=");
    hold_put_hex((uint64_t)(uintptr_t)resident);
    hold_puts(" init=");
    hold_put_hex(resident != NULL ? (uint64_t)(uintptr_t)resident->rt_Init : 0);
    hold_puts(" base=");
    hold_put_hex((uint64_t)(uintptr_t)node);
    hold_puts("\n");
}

/* The loaded module map, once, so any later exception address can be placed. */
static void log_residents(void)
{
    IPTR *modules = SysBase->ResModules;
    uint32_t index;

    if (modules == NULL)
        return;
    for (index = 0; index < AROS_APPLE_STARTUP_MAX_RESIDENTS &&
            modules[index] != 0; ++index) {
        struct Resident *resident = (struct Resident *)modules[index];

        hold_puts("[KI] resident ");
        hold_puts(resident->rt_Name != NULL ? resident->rt_Name : "?");
        hold_puts(" at ");
        hold_put_hex((uint64_t)(uintptr_t)resident);
        hold_puts(" init ");
        hold_put_hex((uint64_t)(uintptr_t)resident->rt_Init);
        hold_puts(" end ");
        hold_put_hex((uint64_t)(uintptr_t)resident->rt_EndSkip);
        hold_puts("\n");
    }
}

static uint64_t hold_counter(void)
{
    uint64_t value;

    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(value) :: "memory");
    return value;
}

static uint64_t hold_frequency(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

/*
 * A small window the launcher redraws itself, once a second, from the CPU
 * counter: the wall clock and the seconds since the desktop settled. It
 * depends on no timer interrupt and no extra module, so a moving clock is
 * evidence the system is scheduling, not merely painted.
 */
/* The timer base the hold opens; GetSysTime() resolves through this name. */
static struct Device *alive_timer_device;
/* The hold's own timer request; the drain sets the system time through it. */
static struct timerequest *alive_timer_request;

/* Seconds from 1970-01-01 to AROS's epoch, 1978-01-01. */
#define HOLD_UNIX_TO_AMIGA UINT64_C(252460800)

/*
 * A virtual machine has no PMIC clock. Its UEFI loader read the firmware's
 * wall clock (GetTime) and the virtual counter at the same moment, and passed
 * both in /chosen (aros,boot-unix-seconds, aros,boot-counter). The system
 * time is set from them, adding the time elapsed since; without them it
 * stays at the boot volume's date.
 */
static int rtc_disabled_by_cell(void)
{
    const void *fdt;
    uint64_t size;
    const uint8_t *value;
    uint32_t length;

    if (!AROSAppleKernelRetainedDeviceTree(&fdt, &size) ||
        !AROSAppleKernelChosenProperty(fdt, size, "aros,desktop-rtc",
            &value, &length))
        return 0;
    return AROSAppleKernelChosenDecimal("aros,desktop-rtc") == 0;
}

static void publish_system_time(uint64_t unix_seconds, int32_t rtc_status)
{
    struct AROSAppleSystemTimeInfo info;

    hold_zero(&info, sizeof(info));
    info.version = AROS_APPLE_SYSTEM_TIME_VERSION;
    info.size = sizeof(info);
    info.rtc_status = rtc_status;
    if (unix_seconds != 0) {
        info.flags = AROS_APPLE_SYSTEM_TIME_RTC_SET;
        info.unix_seconds = unix_seconds;
        info.counter_at_set = hold_counter();
    }
    AROSAppleKernelPublishSystemTime(&info);
}

static uint64_t set_system_time_from_firmware(void)
{
    uint64_t firmware_seconds =
        AROSAppleKernelChosenDecimal("aros,boot-unix-seconds");
    uint64_t firmware_counter =
        AROSAppleKernelChosenDecimal("aros,boot-counter");
    uint64_t now = hold_counter();
    uint64_t frequency = hold_frequency();
    uint64_t ticks, unix_seconds;

    if (firmware_seconds < HOLD_UNIX_TO_AMIGA || firmware_counter == 0 ||
            firmware_counter > now || frequency == 0 ||
            alive_timer_request == NULL) {
        hold_puts("[KI] rtc unavailable: no firmware clock\n");
        return 0;
    }
    ticks = now - firmware_counter;
    unix_seconds = firmware_seconds + ticks / frequency;
    alive_timer_request->tr_node.io_Command = TR_SETSYSTIME;
    alive_timer_request->tr_time.tv_secs =
        (ULONG)(unix_seconds - HOLD_UNIX_TO_AMIGA);
    alive_timer_request->tr_time.tv_micro =
        (ULONG)(((ticks % frequency) * 1000000U) / frequency);
    DoIO(&alive_timer_request->tr_node);
    hold_puts("[KI] rtc from the firmware clock: unix ");
    hold_put_decimal(unix_seconds);
    hold_puts("\n");
    return unix_seconds;
}

/*
 * The injection mailbox. The host writes slots and advances head through
 * the proxy while the guest is stopped in a trap; the hold loop drains them
 * into input.device as ordinary input events, the same path the drivers
 * feed. Its address is printed once so the host can find it.
 */
#define INJECT_MAGIC UINT32_C(0x4e494b41)
#define INJECT_SLOTS 64U
#define INJECT_KIND_RAWKEY UINT32_C(1)
#define INJECT_KIND_MOTION UINT32_C(2)
#define INJECT_KIND_BUTTON UINT32_C(3)
/* code = seconds since 1978-01-01 (local wall time), to set the system clock. */
#define INJECT_KIND_SETTIME UINT32_C(4)

struct AROSAppleInjectSlot {
    uint32_t kind;
    uint32_t code;
    int32_t dx;
    int32_t dy;
};

struct AROSAppleInjectMailbox {
    uint32_t magic;
    uint32_t head;
    uint32_t tail;
    uint32_t reserved;
    struct AROSAppleInjectSlot slots[INJECT_SLOTS];
};

static volatile struct AROSAppleInjectMailbox inject_mailbox
    __attribute__((aligned(64)));

static void inject_drain(struct IOStdReq *input_request)
{
    uint32_t pending = inject_mailbox.head - inject_mailbox.tail;

    if (pending == 0)
        return;
    hold_puts("[KI] inject draining ");
    hold_put_hex(pending);
    hold_puts("\n");
    while (inject_mailbox.tail != inject_mailbox.head) {
        volatile struct AROSAppleInjectSlot *slot =
            &inject_mailbox.slots[inject_mailbox.tail % INJECT_SLOTS];
        struct InputEvent event;
        uint32_t kind = slot->kind;

        event.ie_NextEvent = NULL;
        event.ie_SubClass = 0;
        event.ie_TimeStamp.tv_secs = 0;
        event.ie_TimeStamp.tv_micro = 0;
        event.ie_X = 0;
        event.ie_Y = 0;
        if (kind == INJECT_KIND_RAWKEY) {
            event.ie_Class = IECLASS_RAWKEY;
            event.ie_Code = (UWORD)slot->code;
            event.ie_Qualifier = (UWORD)slot->dx;
        } else if (kind == INJECT_KIND_MOTION) {
            event.ie_Class = IECLASS_RAWMOUSE;
            event.ie_Code = IECODE_NOBUTTON;
            event.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
            event.ie_X = (WORD)slot->dx;
            event.ie_Y = (WORD)slot->dy;
        } else if (kind == INJECT_KIND_BUTTON) {
            event.ie_Class = IECLASS_RAWMOUSE;
            event.ie_Code = (UWORD)slot->code;
            event.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
        } else if (kind == INJECT_KIND_SETTIME) {
            if (alive_timer_request != NULL) {
                alive_timer_request->tr_node.io_Command = TR_SETSYSTIME;
                alive_timer_request->tr_time.tv_secs = slot->code;
                alive_timer_request->tr_time.tv_micro = 0;
                DoIO(&alive_timer_request->tr_node);
                hold_puts("[KI] system time set\n");
            }
            inject_mailbox.tail = inject_mailbox.tail + 1U;
            continue;
        } else {
            inject_mailbox.tail = inject_mailbox.tail + 1U;
            continue;
        }
        if (input_request != NULL) {
            input_request->io_Command = IND_WRITEEVENT;
            input_request->io_Data = &event;
            input_request->io_Length = sizeof(event);
            DoIO((struct IORequest *)input_request);
        }
        inject_mailbox.tail = inject_mailbox.tail + 1U;
    }
    hold_puts("[KI] inject drained\n");
}

static void report_cpu_probe(void)
{
    const struct AROSCPUTopology *t=AROSCPUMonitorTopology();
    for (unsigned cpu=0;cpu<t->count;cpu++) if (AROSCPUProbeCPU(cpu)) {
        struct AROSCPUSample s;
        if (!AROSCPUMonitorSample(cpu,&s)) continue;
        hold_puts("[SMP] sample cpu=");hold_put_hex(cpu);
        hold_puts(" affinity=");hold_put_hex(t->cpu[cpu].affinity);
        hold_puts(" state=");hold_put_hex(s.state);
        hold_puts(" busy=");hold_put_hex(s.busy);
        hold_puts(" idle=");hold_put_hex(s.idle);
        hold_puts(" transitions=");hold_put_hex(s.transitions);
        uint32_t sent,handled,acked;AROSCPUProbeIPICounters(&sent,&handled,&acked);
        hold_puts(" ipi_sent=");hold_put_hex(sent);
        hold_puts(" ipi_handled=");hold_put_hex(handled);
        hold_puts(" ipi_acked=");hold_put_hex(acked);hold_puts("\n");
    }
}

/* Inspect assign names without resolving a relative filesystem path or
 * opening a volume requester. -1 leaves startup configuration untouched. */
static int boot_assign_exists(const char *name)
{
    const ULONG flags = LDF_ASSIGNS | LDF_READ;
    struct DosList *list = LockDosList(flags);
    int found;

    if (list == NULL)
        return -1;
    found = FindDosEntry(list, name, LDF_ASSIGNS) != NULL;
    UnLockDosList(flags);
    return found;
}

extern volatile uint32_t AROSAppleKernelIdleMode;
extern volatile uint64_t AROSAppleKernelIdleEntries;

static void open_shell_and_hold(struct AROSAppleColdStartProof *proof)
{
    BPTR console;
    uint64_t frequency;
    uint64_t seconds;
    int forever;
    int hide_shell = AROSAppleKernelChosenDecimal("aros,desktop-hide-shell") == 1;
    uint64_t deadline;
    struct MsgPort *timer_port = NULL;
    struct timerequest *timer_request = NULL;
    struct MsgPort *input_port = NULL;
    struct IOStdReq *input_request = NULL;
    int input_open = 0;
    uint64_t started;
    uint64_t next_draw;
    uint64_t next_tick;

    /* Each step leaves its number in the proof, so a run that never comes
     * back from here still says where it stopped. */
    proof->detail = UINT32_C(0x9a1);
    /* The architectural counter runs from the SoC's reset: the time to a
     * stable desktop, firmware and loader included, for boot-time checks. */
    hold_puts("[KI] desktop stable at ");
    hold_put_decimal(hold_frequency() >= 1000 ?
        hold_counter() / (hold_frequency() / 1000) : 0);
    hold_puts(" ms after reset\n");
    hold_puts("[KI] idle mode ");
    hold_put_hex(AROSAppleKernelIdleMode);
    hold_puts(" entries ");
    hold_put_hex(AROSAppleKernelIdleEntries);
    hold_puts("\n");
    proof->detail = UINT32_C(0x9a2);
    frequency = hold_frequency();
    proof->detail = UINT32_C(0x9a3);
    seconds = AROSAppleKernelChosenDecimal(HOLD_PROPERTY);
    forever = AROSAppleKernelChosenDecimal(HOLD_FOREVER_PROPERTY) == 1;
    proof->detail = UINT32_C(0x9a4);
    /*
     * The window is a property of the cell, set by the guest composer. A
     * tree without either setting - the QEMU gate, whose verdict reads the
     * final frame and would reject a console on it - returns to the loader
     * at once. Only the explicit decimal value one requests an endless run.
     */
    if ((!forever && (seconds == 0 || seconds > HOLD_MAX_SECONDS)) ||
        frequency == 0) {
        proof->detail = 0;
        return;
    }
    proof->detail = UINT32_C(0x9a5);
    console = hide_shell ? BNULL :
        Open("CON:40/60/1100/620/AROS Shell/CLOSE", MODE_NEWFILE);
    if (console != BNULL) {
        /* NewShell's own recipe: a foreground user shell that keeps its
         * console after each command instead of ending with the first.
         * Match CLI/NewShell's optional S:Shell-Startup input; successful
         * asynchronous launch owns both streams. Missing startup is normal. */
        BPTR startup = Open("S:Shell-Startup", MODE_OLDFILE);
        /* SYS: as its current directory, not this task's RAM volume: a
         * library is looked up in the caller's current directory before
         * LIBS:, and the RAM image's Libs hold the boot image's copies. The
         * new process owns the lock. */
        BPTR home = Lock("SYS:", SHARED_LOCK);
        LONG status = SystemTags("",
            SYS_Asynch, TRUE,
            SYS_Background, FALSE,
            SYS_Input, (IPTR)console,
            SYS_Output, (IPTR)BNULL,
            SYS_Error, (IPTR)BNULL,
            SYS_UserShell, TRUE,
            SYS_ScriptInput, (IPTR)startup,
            NP_StackSize, AROS_STACKSIZE * 2U,
            home != BNULL ? NP_CurrentDir : TAG_IGNORE, (IPTR)home,
            TAG_DONE);

        /* An asynchronous shell answers with anything but -1 on success;
         * NewShell tests exactly that, and the console now belongs to it. */
        if (status != -1)
            hold_puts("[KI] shell launched\n");
        else {
            hold_puts("[KI] shell launch failed\n");
            if (home != BNULL)
                UnLock(home);
            if (startup != BNULL)
                Close(startup);
            Close(console);
        }
    } else if (!hide_shell)
        hold_puts("[KI] shell console failed\n");

    proof->detail = UINT32_C(0x9a6);
    timer_port = CreateMsgPort();
    if (timer_port != NULL)
        timer_request = (struct timerequest *)CreateIORequest(
            timer_port, sizeof(*timer_request));
    if (timer_request != NULL &&
        OpenDevice(TIMERNAME, UNIT_VBLANK, &timer_request->tr_node, 0) == 0)
        alive_timer_device = timer_request->tr_node.io_Device;
    alive_timer_request = timer_request;
    /* The wall clock, before anything dates a file or checks a certificate. */
    set_system_time_from_firmware();
    uintptr_t probe_started=0;
    if (AROSAppleKernelChosenDecimal("aros,smp-probe")==1) {
        unsigned current_cpu=AROSCPUCurrentIndex();
        hold_puts(current_cpu==AROSCPUMonitorTopology()->boot_cpu &&
            AROSCPUCurrentExecIndex()==current_cpu ?
            "[SMP] EL0 CPU anchor PASS\n" : "[SMP] FAIL: EL0 CPU anchor\n");
#if defined(__AROSEXEC_SMP__)
        if (AROSAppleSMPPrepare())
            probe_started=AROSAppleRuntimeSupervisor(AROSAppleSMPStart);
        if (!probe_started) hold_puts("[SMP] FAIL: Exec secondary startup\n");
#else
        probe_started=AROSAppleRuntimeSupervisor(AROSCPUProbeStart);
        if (probe_started) hold_puts("[SMP] CPU runtime isolation PASS\n");
        hold_puts(probe_started?"[SMP] secondary execution probe started\n":
            "[SMP] FAIL: secondary execution probe startup\n");
#endif
    }
    /* The desktop is accepted: the widgets the Startup-Sequence started
     * (widgetstart.h) may open their windows now. */
    hold_puts(SetVar(WIDGET_DESKTOP_VAR, "1", 1, GVF_GLOBAL_ONLY)
        ? "[KI] desktop accepted\n" : "[KI] desktop accepted, variable failed\n");
#if defined(__AROSEXEC_SMP__)
    if (probe_started) {
        /* Sample before and after ordinary work so the log records both
         * cores' load, rather than only the idle interval afterwards. */
        report_cpu_probe();
        if (!AROSAppleSMPWorkProof(timer_request))
            hold_puts("[SMP] FAIL: ordinary task execution\n");
        else {
            hold_puts("[SMP] Exec two-core tasks PASS\n");
            report_cpu_probe();
            hold_puts("[SMP] load samples reported\n");
            /* Keep the measured update visible for a bounded host capture. */
            timer_request->tr_node.io_Command=TR_ADDREQUEST;
            timer_request->tr_time.tv_secs=1;
            timer_request->tr_time.tv_micro=0;
            DoIO(&timer_request->tr_node);
        }
    }
#endif
    struct ColdStartServices services = {
        proof_dos_base, timer_request, hold_puts, hold_put_hex,
        hold_put_decimal, hold_zero, hold_counter, hold_frequency,
        set_system_time_from_firmware, rtc_disabled_by_cell,
        boot_assign_exists, publish_system_time
    };
    if (platform != NULL)
        platform->begin(&services);
    else if (!rtc_disabled_by_cell()) {
        uint64_t unix_seconds = set_system_time_from_firmware();
        publish_system_time(unix_seconds, unix_seconds != 0 ? 0 : -1);
    } else {
        hold_puts("[KI] rtc disabled by the cell\n");
        publish_system_time(0, -1);
    }
    input_port = CreateMsgPort();
    if (input_port != NULL)
        input_request = (struct IOStdReq *)CreateIORequest(
            input_port, sizeof(*input_request));
    if (input_request != NULL &&
        OpenDevice("input.device", 0, (struct IORequest *)input_request, 0) == 0)
        input_open = 1;
    inject_mailbox.magic = INJECT_MAGIC;
    inject_mailbox.head = 0;
    inject_mailbox.tail = 0;
    hold_puts("[KI] inject mailbox ");
    hold_put_hex((uint64_t)(uintptr_t)&inject_mailbox);
    hold_puts(input_open ? " input=open\n" : " input=closed\n");
    started = hold_counter();
    deadline = forever ? 0 : started + frequency * seconds;
    hold_puts(forever ? "[KI] hold endless\n" : "[KI] hold bounded\n");
    next_draw = started;
    next_tick = started;
    if (platform != NULL)
        platform->start_polling(started, frequency);
    uint64_t starve_last = 0;
    uint64_t starve_ticks = 0;
    uint64_t starve_next = 0;

    SetTaskPri(FindTask(NULL), -5);
    while (forever || hold_counter() < deadline) {
        uint64_t now = hold_counter();

        /*
         * How long the hold waited for the CPU: each round sleeps 10 ms at
         * priority -5, so time beyond that went to other tasks. Five-second
         * windows with more than 200 ms of it are logged, which shows when
         * the desktop is loaded and by how much.
         */
        if (starve_last != 0 && now > starve_last + frequency / 100U)
            starve_ticks += now - starve_last - frequency / 100U;
        starve_last = now;
        if (now >= starve_next) {
            uint64_t milliseconds = starve_ticks * 1000U / frequency;

            if (starve_next != 0 && milliseconds >= 200U) {
                hold_puts("[KI] hold starved ms ");
                hold_put_hex(milliseconds);
                hold_puts(" at s ");
                hold_put_hex((now - started) / frequency);
                hold_puts("\n");
            }
            starve_ticks = 0;
            starve_next = now + frequency * 5U;
        }

        inject_drain(input_open ? input_request : NULL);
        if (now >= next_tick) {
            /* A trap every two seconds, so the host can act on its queue. */
            hold_puts("[KI] tick\n");
            if (platform != NULL) platform->report_irqs(1);
            next_tick = now + frequency * 2U;
        }
        if (platform != NULL)
            platform->poll(now, started, frequency);
        if (now >= next_draw) {
            uint64_t uptime = (now - started) / frequency;

            AROSCPUProbeTick(uptime);
            /* Let observers capture a completed update instead of mid-redraw. */
            report_cpu_probe();
            if (uptime != 0 && (uptime % 60U) == 0) {
                hold_puts("[KI] hold minute ");
                hold_put_hex(uptime / 60U);
                hold_puts("\n");
            }
            next_draw = now + frequency;
        }
        /*
         * Sleep between rounds rather than yield: a Reschedule() loop at
         * priority -5 kept the CPU busy every moment no other task ran, so
         * it never reached the idle loop and never slowed down. 10 ms keeps
         * the injection mailbox and the once-a-second sampling responsive.
         */
        if (timer_request != NULL && alive_timer_device != NULL) {
            timer_request->tr_node.io_Command = TR_ADDREQUEST;
            timer_request->tr_time.tv_secs = 0;
            timer_request->tr_time.tv_micro = 10000;
            DoIO(&timer_request->tr_node);
        } else
            Reschedule();
    }
#if defined(__AROSEXEC_SMP__)
    if (!AROSAppleRuntimeSupervisor(AROSAppleSMPStop)) {
        hold_puts("[SMP] FAIL: secondary did not stop; retaining kernel\n");
        for (;;) Reschedule();
    }
    if (platform != NULL)
        platform->finish(timer_request, alive_timer_device != NULL, probe_started);
    else if (probe_started)
        hold_puts("[SMP] Exec secondary stopped PASS\n");

#else
    if (probe_started && !AROSCPUProbeDrain())
        hold_puts("[SMP] FAIL: IPI drain timeout\n");
    if (!AROSAppleRuntimeSupervisor(AROSCPUProbeStop)) {
        hold_puts("[SMP] FAIL: secondary did not stop; retaining kernel\n");
        for (;;) Reschedule();
    }
    if (probe_started) {
        hold_puts("[SMP] secondary execution probe stopped\n");
        hold_puts(AROSCPUProbeIPIPassed()?"[SMP] IPI roundtrip PASS\n":
            "[SMP] FAIL: IPI roundtrip\n");
    }
#endif
    hold_puts("[KI] hold complete\n");
    if (platform != NULL) platform->report_irqs(0);
    if (input_open)
        CloseDevice((struct IORequest *)input_request);
    proof->detail = 0;
}

/*
 * Sleep until G4Ready signals the launcher. The observer that created the
 * launcher runs inside DOS's Boot Mount process, which then runs the
 * Initial CLI synchronously and exits when the Startup-Sequence ends; that
 * process is therefore live exactly while the script can still reach
 * G4Ready. Once it is gone without the barrier, the launcher fails closed
 * (0x90b), which is what the disable-g4ready negative expects. A one-second
 * timer request paces the check; without timer.device the launcher only
 * sleeps on the signal.
 */
static int wait_for_initial_cli(struct AROSAppleColdStartProof *proof)
{
    struct MsgPort *port = CreateMsgPort();
    struct timerequest *request = NULL;
    const struct Task *boot_mount = (const struct Task *)proof->dos_boot_process;
    int timer_open = 0;
    int result = 0;

    if (port != NULL)
        request = (struct timerequest *)CreateIORequest(
            port, sizeof(*request));
    if (request != NULL &&
            OpenDevice(TIMERNAME, UNIT_VBLANK, &request->tr_node, 0) == 0)
        timer_open = 1;
    SetSignal(0, SIGBREAKF_CTRL_F);
    for (;;) {
        if (proof->wanderer_initial_cli_seen == 1) {
            result = 1;
            break;
        }
        if (boot_mount != NULL && !task_is_live(boot_mount))
            break;
        if (timer_open) {
            ULONG signals;

            request->tr_node.io_Command = TR_ADDREQUEST;
            request->tr_time.tv_secs = 1;
            request->tr_time.tv_micro = 0;
            SendIO(&request->tr_node);
            signals = Wait(SIGBREAKF_CTRL_F | (1UL << port->mp_SigBit));
            if ((signals & (1UL << port->mp_SigBit)) == 0)
                AbortIO(&request->tr_node);
            WaitIO(&request->tr_node);
        } else {
            Wait(SIGBREAKF_CTRL_F);
        }
    }
    if (timer_open)
        CloseDevice(&request->tr_node);
    if (request != NULL)
        DeleteIORequest(&request->tr_node);
    if (port != NULL)
        DeleteMsgPort(port);
    return result;
}

/* Preserve the foreground evidence before returning to the loader. Copy
 * under Intuition's lock, then print outside it; logging must not retain a
 * window pointer or hold the GUI lock while waiting for serial output. */
static void report_wanderer_windows(void)
{
    struct {
        uintptr_t address;
        uint32_t flags;
        char title[96];
    } windows[16];
    unsigned count = 0, i;
    ULONG lock;
    struct Screen *screen;
    struct Window *window;

    if (proof_intuition_base == NULL)
        return;
    lock = LockIBase(0);
    screen = proof_intuition_base->FirstScreen;
    for (window = screen != NULL ? screen->FirstWindow : NULL;
         window != NULL && count < 16; window = window->NextWindow) {
        const char *title = window->Title != NULL ?
            (const char *)window->Title : "<untitled>";
        unsigned n = 0;
        windows[count].address = (uintptr_t)window;
        windows[count].flags = window->Flags;
        while (n + 1 < sizeof(windows[count].title) && title[n] != 0) {
            char ch = title[n];
            windows[count].title[n++] = ch >= 32 && ch != 127 ? ch : '?';
        }
        windows[count].title[n] = 0;
        ++count;
    }
    UnlockIBase(lock);
    for (i = 0; i < count; ++i) {
        hold_puts(i == 0 ? "[KI] desktop front window=" : "[KI] desktop window=");
        hold_put_hex(windows[i].address);
        hold_puts(" flags="); hold_put_hex(windows[i].flags);
        hold_puts(" title="); hold_puts(windows[i].title); hold_puts("\n");
    }
}

static void wanderer_launcher_entry(void)
{
    struct AROSAppleColdStartProof *proof = active_proof;
    BPTR sys_lock = BNULL;
    BPTR assign_lock = BNULL;
    uint32_t component_detail = 0;

    if (proof == NULL)
        AROSAppleRuntimeReturnEL1(0);
    proof->detail = UINT32_C(0x908);
    /*
     * The launcher exists at priority -1 from the moment the observer
     * created it, so the scheduler runs it the first time every other
     * task sleeps: the Startup-Sequence's first timer wait, long before
     * G4Ready. It must sleep here until G4Ready raises it, never fail:
     * an EL0 process that calls AROSAppleRuntimeReturnEL1() outside the
     * coldstart transition ends in the runtime's halt loop with every
     * interrupt masked, and the machine never wakes the waiting shell.
     */
    if (proof->wanderer_initial_cli_seen != 1 &&
            !wait_for_initial_cli(proof)) {
        proof->detail = UINT32_C(0x90b);
        AROSAppleRuntimeReturnEL1(0);
    }
    proof->detail = UINT32_C(0x909);
    sys_lock = Lock("SYS:", SHARED_LOCK);
    proof->sys_lock = (uintptr_t)BADDR(sys_lock);
    if (sys_lock != BNULL) {
        int existing = boot_assign_exists("WANDERER");

        /* Startup-Sequence may select a complete writable distribution.
         * Keep that choice; SYS: is only the minimal-image fallback. */
        if (existing == 0)
            assign_lock = DupLock(sys_lock);
        if (existing < 0 || (existing == 0 &&
            (assign_lock == BNULL || !AssignLock("WANDERER", assign_lock)))) {
            if (assign_lock != BNULL)
                UnLock(assign_lock);
            proof->detail = UINT32_C(0x907);
            AROSAppleRuntimeReturnEL1(0);
        }
        component_detail = initialize_wanderer_components();
    }
    if (component_detail != 0) {
        proof->detail = component_detail;
        AROSAppleRuntimeReturnEL1(0);
    }
    /* From here the graphics driver and the Workbench screen own the panel. */
    AROSAppleKernelScreenLogStop();
    if (sys_lock != BNULL && run_wanderer(proof, sys_lock)) {
        proof->detail = 0;
        proof->status = 1;
        open_shell_and_hold(proof);
        AROSAppleRuntimeReturnEL1(AROS_APPLE_COLDSTART_STATUS_PASS);
    }
    if (proof != NULL) {
        report_wanderer_windows();
        hold_puts("[KI] Wanderer failed detail="); hold_put_hex(proof->detail);
        hold_puts(" process="); hold_put_hex(proof->wanderer_process);
        hold_puts(" screen="); hold_put_hex(proof->wanderer_screen);
        hold_puts(" window="); hold_put_hex(proof->wanderer_window);
        hold_puts(" cli-closed="); hold_put_hex(proof->wanderer_initial_cli_closed);
        hold_puts("\n");
        proof->detail = UINT32_C(0x529);
    }
    AROSAppleRuntimeReturnEL1(0);
}

static int install_dos_observer(void)
{
    IPTR *old_list = SysBase->ResModules;
    IPTR *new_list;
    uint32_t count = 0;
    uint32_t index;

    if (old_list == NULL)
        return 0;
    while (old_list[count] != 0) {
        if (++count >= AROS_APPLE_STARTUP_MAX_RESIDENTS)
            return 0;
    }
    new_list = AllocMem((count + 2U) * sizeof(*new_list), MEMF_PUBLIC);
    if (new_list == NULL)
        return 0;
    dos_takeover_observer_resident.rt_MatchWord = RTC_MATCHWORD;
    dos_takeover_observer_resident.rt_MatchTag =
        &dos_takeover_observer_resident;
    dos_takeover_observer_resident.rt_EndSkip =
        &dos_takeover_observer_resident + 1;
    dos_takeover_observer_resident.rt_Flags = RTF_AFTERDOS;
    dos_takeover_observer_resident.rt_Version = 1;
    dos_takeover_observer_resident.rt_Type = NT_PROCESS;
    dos_takeover_observer_resident.rt_Pri = 127;
    dos_takeover_observer_resident.rt_Name = "Apple DOS takeover proof";
    dos_takeover_observer_resident.rt_IdString =
        "Apple DOS takeover proof 1.0";
    dos_takeover_observer_resident.rt_Init = dos_takeover_observer;
    for (index = 0; index < count; ++index)
        new_list[index] = old_list[index];
    new_list[count] = (IPTR)&dos_takeover_observer_resident;
    new_list[count + 1U] = 0;
    SysBase->ResModules = new_list;
    return 1;
}

static struct Library *ensure_resident_library(const char *name)
{
    struct Library *library = (struct Library *)FindName(&SysBase->LibList,
        name);
    struct Resident *resident;

    if (library != NULL)
        return library;
    resident = FindResident(name);
    if (resident == NULL)
        return NULL;
    return (struct Library *)InitResident(resident, BNULL);
}

static APTR ensure_resident_component(const char *name)
{
    struct Node *component;
    struct Resident *resident;

    component = FindName(&SysBase->LibList, name);
    if (component == NULL)
        component = FindName(&SysBase->DeviceList, name);
    if (component == NULL)
        component = FindName(&SysBase->ResourceList, name);
    if (component != NULL)
        return component;
    resident = FindResident(name);
    return resident != NULL ? InitResident(resident, BNULL) : NULL;
}

static void log_component(const char *name);
static void log_residents(void);

/*
 * These components may open files, start helper tasks or otherwise use DOS
 * while initializing.  Running them from the Boot Mount AFTERDOS observer
 * would make the filesystem handler wait on work only that same handler can
 * service.  The launcher waits until the real Initial CLI exists, proving
 * AFTERDOS initialization and the boot filesystem are independently live,
 * before it opens any of them.  GadTools is deliberately absent: con-handler
 * needs it before Initial CLI can open CON:, and its resident initializer only
 * opens the already-live Utility, Graphics, Layers and Intuition libraries, so
 * the observer can initialize it safely.
 */
static uint32_t initialize_wanderer_components(void)
{
    static const struct {
        const char *resident_name;
        const char *zune_name;
    } components[] = {
        { "commodities.library", NULL },
        { "rexxsyslib.library", NULL },
        { "datatypes.library", NULL },
        { "diskfont.library", NULL },
        { "icon.library", NULL },
        { "cybergraphics.library", NULL },
        { "workbench.library", NULL },
        { "coolimages.library", NULL },
        { "asl.library", NULL },
        { "muiscreen.library", NULL },
        { "muimaster.library", NULL },
        { "IconListview.mui", "Zune/IconListview.mui" },
        { "IconList.mui", "Zune/IconList.mui" },
        { "IconDrawerList.mui", "Zune/IconDrawerList.mui" },
        { "IconVolumeList.mui", "Zune/IconVolumeList.mui" }
    };
    uint32_t index;

    for (index = 0; index < sizeof(components) / sizeof(components[0]);
        ++index) {
        struct Library *component;

        if (active_proof != NULL)
            active_proof->detail = UINT32_C(0x820) + index;
        component = (struct Library *)
            ensure_resident_component(components[index].resident_name);

/* The desktop profile: see desktop_profile_flag at file scope. */
#define WANDERER_PROFILE(count) (desktop_profile_flag != 0)

        if (component == NULL)
            return UINT32_C(0x810) + index;
        /* MUI's external-class search opens Zune/<class>.  A normal ramlib
         * load resolves that path from disk; this resident-only profile
         * registers the already verified module under the same canonical
         * search name so derived classes can open their superclass. */
        if (components[index].zune_name != NULL)
            component->lib_Node.ln_Name =
                (char *)components[index].zune_name;
    }
    return 0;
}

static int ui_deferred_resident(const struct Resident *resident)
{
    static const char *const names[] = {
        "layers.library",
        "inputclass.hidd",
        "keyboard.hidd",
        "mouse.hidd",
        "keyboard.device",
        "gameport.device",
        "keymap.library",
        "input.device",
        "intuition.library",
        "console.device",
        "asl.library",
        "commodities.library",
        "rexxsyslib.library",
        "gadtools.library",
        "datatypes.library",
        "diskfont.library",
        "cybergraphics.library",
        "coolimages.library",
        "muiscreen.library",
        "muimaster.library",
        "IconList.mui",
        "IconDrawerList.mui",
        "IconVolumeList.mui",
        "IconListview.mui"
    };
    uint32_t index;

    if (resident == NULL)
        return 0;
    if (platform != NULL && name_equal(resident->rt_Name, platform->input_resident))
        return 1;
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (name_equal(resident->rt_Name, names[index]))
            return 1;
    }
    return 0;
}

AROS_UFH3S(APTR, dos_takeover_observer,
    AROS_UFHA(ULONG, dummy, D0),
    AROS_UFHA(BPTR, seg_list, A0),
    AROS_UFHA(struct ExecBase *, sys_base, A6))
{
    AROS_USERFUNC_INIT

    struct AROSAppleColdStartProof *proof = active_proof;
    struct DosLibrary *dos_base;
    struct Library *aros_base;
    struct Library *iffparse_base;
    struct Library *locale_base;
    struct Library *stdc_base;
    struct Library *stdcio_base;
    struct Library *posixc_base;
    struct Library *intuition_base = NULL;
    APTR g3_component = NULL;
    struct Task *process;
    struct FileLock *file_lock;
    struct FileLock *oldsys_file_lock;
    struct MsgPort *boot_owner;
    struct MsgPort *afssys_handler = NULL;
    int pivoted = 0;
    BPTR sys_lock = BNULL;
    BPTR oldsys_lock = BNULL;
    BPTR file = BNULL;
    UBYTE buffer[64];
    char path[32];
    LONG sentinel_read = -1;
    LONG sentinel_eof = -1;
    LONG boot_signature_read = -1;
    LONG abi_probe_return = -1;
    LONG abi_probe_bad_return = -1;
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    int vm_pci_valid = 1;
#endif
    BPTR abi_probe = BNULL;
    BPTR abi_output = BNULL;
    BPTR previous_output = BNULL;
    struct AROSApplePageAllocatorStats abi_before = {0};
    struct AROSApplePageAllocatorStats abi_loaded = {0};
    struct AROSApplePageAllocatorStats abi_unloaded = {0};
    struct AROSApplePageAllocatorStats tls_before = {0};
    struct AROSApplePageAllocatorStats tls_loaded = {0};
    struct AROSApplePageAllocatorStats tls_unloaded = {0};
    LONG tls_probe_return = -1;
    BPTR tls_probe = BNULL;
    BPTR tls_output = BNULL;
    struct AROSAppleFramebufferInfo framebuffer;
    uint32_t assign_count = 0;
    uint32_t index;
    uint32_t intuition_detail = 0;
    int workbench_required = 0;
    int workbench_valid = 1;
    int tls_required = 0;
    int tls_valid = 1;
    int small_required = 0;
    int small_valid = 1;
    LONG small_probe_return = -1;
    LONG small_probe_bad_return = -1;
    BPTR small_probe = BNULL;
    BPTR small_output = BNULL;
    struct AROSApplePageAllocatorStats small_before = {0};
    struct AROSApplePageAllocatorStats small_loaded = {0};
    struct AROSApplePageAllocatorStats small_unloaded = {0};
    int settings_required = 0;
    int settings_valid = 1;
    LONG settings_probe_return = -1;
    BPTR settings_probe = BNULL;
    BPTR settings_output = BNULL;
    struct AROSApplePageAllocatorStats settings_before = {0};
    struct AROSApplePageAllocatorStats settings_loaded = {0};
    struct AROSApplePageAllocatorStats settings_unloaded = {0};
    int power_required = 0;
    int power_valid = 1;
    LONG power_probe_return = -1;
    BPTR power_probe = BNULL;
    BPTR power_output = BNULL;
    int wanderer_requirement = 0;
    int wanderer_valid = 1;
    uint32_t final_detail = 0;
    int valid = proof != NULL && sys_base == SysBase;

    (void)dummy;
    (void)seg_list;
    if (!valid)
        AROSAppleRuntimeReturnEL1(0);
    proof->detail = UINT32_C(0x501);
    dos_base = (struct DosLibrary *)FindName(
        &SysBase->LibList, "dos.library");
    aros_base = ensure_resident_library("aros.library");
    iffparse_base = ensure_resident_library("iffparse.library");
    locale_base = ensure_resident_library("locale.library");
    stdc_base = ensure_resident_library("stdc.library");
    stdcio_base = ensure_resident_library("stdcio.library");
    posixc_base = ensure_resident_library("posixc.library");
    /* Close the deferred UI/input set only after IFFParse and Locale exist. */
    if (proof->resident_count == 34 || WANDERER_PROFILE(proof->resident_count)) {
        static const char *const names[] = {
            "layers.library",
            "inputclass.hidd",
            "keyboard.hidd",
            "mouse.hidd",
            "keyboard.device",
            "gameport.device",
            "keymap.library",
            "input.device",
            "intuition.library",
            "console.device",
            "gadtools.library"
        };
        uint32_t component_count = WANDERER_PROFILE(proof->resident_count) ?
            sizeof(names) / sizeof(names[0]) : 9U;

        log_residents();
        for (index = 0; index < component_count; ++index) {
            /* Name the component being started, for a dump taken mid-way. */
            proof->detail = UINT32_C(0x7a0) + index;
            log_component(names[index]);
            g3_component = ensure_resident_component(names[index]);
            if (g3_component == NULL) {
                intuition_detail = UINT32_C(0x710) + index;
                break;
            }
            /*
             * The built-in input driver registers with the keyboard and
             * mouse hubs, so it follows the two base classes and precedes
             * the devices that consume their events. Residents here are
             * initialised by name, not by flag: a driver left off this
             * list simply never runs.
             */
            if (platform != NULL && index == 3U &&
                FindResident(platform->input_resident) != NULL) {
                proof->detail = UINT32_C(0x7af);
                log_component(platform->input_resident);
                g3_component = ensure_resident_component(platform->input_resident);
                log_component(platform->input_resident);
                if (g3_component == NULL) {
                    intuition_detail = UINT32_C(0x71f);
                    break;
                }
            }
        }
        proof->detail = UINT32_C(0x501);
        if (g3_component != NULL && intuition_detail == 0)
            intuition_base = (struct Library *)FindName(
                &SysBase->LibList, "intuition.library");
    }
    proof->aros_base = (uintptr_t)aros_base;
    proof->iffparse_base = (uintptr_t)iffparse_base;
    proof->locale_base = (uintptr_t)locale_base;
    proof->stdc_base = (uintptr_t)stdc_base;
    proof->stdcio_base = (uintptr_t)stdcio_base;
    proof->posixc_base = (uintptr_t)posixc_base;
    proof->aros_version = aros_base != NULL ? aros_base->lib_Version : 0;
    proof->iffparse_version = iffparse_base != NULL ?
        iffparse_base->lib_Version : 0;
    proof->locale_version = locale_base != NULL ? locale_base->lib_Version : 0;
    proof->stdc_version = stdc_base != NULL ? stdc_base->lib_Version : 0;
    proof->stdcio_version = stdcio_base != NULL ? stdcio_base->lib_Version : 0;
    proof->posixc_version = posixc_base != NULL ? posixc_base->lib_Version : 0;
    proof_dos_base = dos_base;
    proof->detail = UINT32_C(0x50c);
    if (proof->resident_count == 34 || WANDERER_PROFILE(proof->resident_count)) {
        proof_intuition_base = (struct IntuitionBase *)intuition_base;
        if (intuition_base == NULL)
            intuition_detail = UINT32_C(0x713);
        else if (proof->resident_count == 34 &&
            !AROSAppleKernelRetainedFramebuffer(&framebuffer))
            intuition_detail = UINT32_C(0x701);
        else if (proof->resident_count == 34)
            intuition_detail = intuition_render_probe(&framebuffer);
    }
    proof->detail = UINT32_C(0x50d);
    process = GET_THIS_TASK;
    if (dos_base != NULL)
        sys_lock = Lock("SYS:", SHARED_LOCK);
    file_lock = sys_lock != BNULL ? (struct FileLock *)BADDR(sys_lock) : NULL;
    proof->dos_base = (uintptr_t)dos_base;
    proof->dos_boot_process = (uintptr_t)process;
    proof->sys_lock = (uintptr_t)BADDR(sys_lock);
    proof->boot_volume_task = file_lock != NULL ?
        (uintptr_t)file_lock->fl_Task : 0;
    /*
     * BootStartup hands SYS: to the installed AFS+ volume and keeps the RAM
     * boot volume as OLDSYS:. The DOS boot process then owns OLDSYS:, and the
     * RAM markers (B2PASS.TXT, AROS.boot, ABIPROBE) are read from there.
     * Without the hand-over OLDSYS: is SYS: itself, or absent.
     */
    /*
     * The hand-over shows in the DOS root: the boot process is the RAM
     * volume's handler while SYS: now belongs to AFSSYS. The RAM markers are
     * then read through the RAM volume's own device name, SYS0:.
     */
    proof->detail = UINT32_C(0x5d1);
    pivoted = file_lock != NULL && file_lock->fl_Task != NULL &&
        dos_base != NULL && dos_base->dl_Root != NULL &&
        dos_base->dl_Root->rn_BootProc != NULL &&
        dos_base->dl_Root->rn_BootProc != file_lock->fl_Task;
    proof_pivoted = pivoted;
    proof->detail = UINT32_C(0x5d2) + (pivoted ? 1U : 0U);
    if (pivoted) {
        BPTR afssys_lock = Lock("AFSSYS:", SHARED_LOCK);

        proof->detail = UINT32_C(0x5d4);
        if (afssys_lock != BNULL) {
            afssys_handler = ((struct FileLock *)BADDR(afssys_lock))->fl_Task;
            UnLock(afssys_lock);
        }
        proof->detail = UINT32_C(0x5d5);
        oldsys_lock = Lock("SYS0:", SHARED_LOCK);
        proof->detail = UINT32_C(0x5d6);
    }
    oldsys_file_lock = oldsys_lock != BNULL ?
        (struct FileLock *)BADDR(oldsys_lock) : NULL;
    boot_owner = dos_base != NULL && dos_base->dl_Root != NULL ?
        dos_base->dl_Root->rn_BootProc : NULL;
    proof->detail = UINT32_C(0x502);

    if (dos_base != NULL) {
        for (index = 0;
            index < sizeof(standard_assigns) / sizeof(standard_assigns[0]);
            ++index) {
            BPTR lock = Lock(standard_assigns[index], SHARED_LOCK);

            if (lock == BNULL)
                break;
            ++assign_count;
            UnLock(lock);
        }
    }
    proof->standard_assign_count = assign_count;
    proof->detail = UINT32_C(0x503);

    if (dos_base != NULL)
        file = Open(marker_path(path, "B2PASS.TXT"), MODE_OLDFILE);
    if (file != BNULL) {
        sentinel_read = Read(file, buffer, sizeof(buffer));
        if (sentinel_read == (LONG)(sizeof(sentinel_expected) - 1U) &&
            bytes_equal(buffer, sentinel_expected,
                sizeof(sentinel_expected) - 1U))
            sentinel_eof = Read(file, buffer, 1);
        Close(file);
    }
    proof->sentinel_read_actual = (uint64_t)(int64_t)sentinel_read;
    proof->sentinel_eof_actual = (uint64_t)(int64_t)sentinel_eof;
    proof->detail = UINT32_C(0x504);

    file = dos_base != NULL ?
        Open(marker_path(path, "AROS.boot"), MODE_OLDFILE) : BNULL;
    if (file != BNULL) {
        boot_signature_read = Read(file, buffer, sizeof(buffer));
        if (boot_signature_read !=
                (LONG)(sizeof(boot_signature_expected) - 1U) ||
            !bytes_equal(buffer, boot_signature_expected,
                sizeof(boot_signature_expected) - 1U) ||
            Read(file, buffer, 1) != 0)
            boot_signature_read = -1;
        Close(file);
    }
    proof->boot_signature_read_actual =
        (uint64_t)(int64_t)boot_signature_read;
    proof->detail = UINT32_C(0x505);

    if (dos_base != NULL) {
        AROSAppleCodeMemoryGetStats(&abi_before);
        abi_probe = LoadSeg(marker_path(path, "ABIPROBE"));
        AROSAppleCodeMemoryGetStats(&abi_loaded);
    }
    proof->abi_probe_seglist = (uintptr_t)BADDR(abi_probe);
    proof->abi_probe_entry = abi_probe != BNULL ?
        (uintptr_t)BADDR(abi_probe) + sizeof(BPTR) : 0;
    if (abi_probe != BNULL) {
        static const char abi_bad_arguments[] = "wrong\n";
        static const char abi_arguments_sys[] =
            "alpha \"two words\" SYS:B2PASS.TXT\n";
        static const char abi_arguments_oldsys[] =
            "alpha \"two words\" SYS0:B2PASS.TXT\n";
        const char *abi_arguments = pivoted ?
            abi_arguments_oldsys : abi_arguments_sys;
        const LONG abi_arguments_size = pivoted ?
            (LONG)(sizeof(abi_arguments_oldsys) - 1U) :
            (LONG)(sizeof(abi_arguments_sys) - 1U);

        abi_output = Open("NIL:", MODE_NEWFILE);
        if (abi_output != BNULL) {
            previous_output = SelectOutput(abi_output);
            abi_probe_bad_return = RunCommand(abi_probe, AROS_STACKSIZE,
                abi_bad_arguments, sizeof(abi_bad_arguments) - 1U);
            abi_probe_return = RunCommand(abi_probe, AROS_STACKSIZE,
                abi_arguments, abi_arguments_size);
            SelectOutput(previous_output);
            Close(abi_output);
        }
        UnLoadSeg(abi_probe);
        AROSAppleCodeMemoryGetStats(&abi_unloaded);
        proof->abi_probe_argument_size = (uint64_t)abi_arguments_size;
    }
    proof->abi_probe_return = (uint64_t)(int64_t)abi_probe_return;
    proof->abi_probe_bad_return = (uint64_t)(int64_t)abi_probe_bad_return;
    proof->abi_pages_before = abi_before.pages_in_use;
    proof->abi_pages_loaded = abi_loaded.pages_in_use;
    proof->abi_pages_unloaded = abi_unloaded.pages_in_use;
    proof->abi_load_allocations =
        abi_loaded.allocation_count - abi_before.allocation_count;
    proof->abi_unload_frees =
        abi_unloaded.free_count - abi_loaded.free_count;
    proof->abi_load_rx_protections =
        abi_loaded.rx_protection_count - abi_before.rx_protection_count;
    proof->abi_unload_rw_protections =
        abi_unloaded.rw_protection_count - abi_loaded.rw_protection_count;
    proof->detail = UINT32_C(0x506);
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    /* Optional driver qualification runs outside the transient ABI arena
     * accounting: loaded libraries intentionally remain resident. */
    if (dos_base != NULL) {
        BPTR pci_file = Lock("SYS:PCIPROBE", ACCESS_READ);
        if (pci_file != BNULL) {
            UnLock(pci_file);
            BPTR pci_probe = LoadSeg("SYS:PCIPROBE");
            vm_pci_valid = pci_probe != BNULL &&
                RunCommand(pci_probe, AROS_STACKSIZE, "", 0) == 0;
            /* A failed driver probe may have retained registered callbacks.
             * The failure terminates this boot: preserve its code for diagnosis. */
            if (pci_probe != BNULL && vm_pci_valid) UnLoadSeg(pci_probe);
            if (vm_pci_valid) {
                /* Reload a fresh command after unloading its installer.
                 * Resident driver callbacks must belong to vmpci.hidd. */
                pci_probe = LoadSeg("SYS:PCIPROBE");
                vm_pci_valid = pci_probe != BNULL &&
                    RunCommand(pci_probe, AROS_STACKSIZE, "resident", 8) == 0;
                if (pci_probe != BNULL && vm_pci_valid) UnLoadSeg(pci_probe);
            }
        }
    }
#endif


    workbench_required = dos_base != NULL ? workbench_probe_required() : 0;
    if (workbench_required != 0) {
        proof->workbench_probe_requested = 1;
        workbench_valid = workbench_required == 1 &&
            run_workbench_probe(proof, sys_lock);
    }
    proof->detail = UINT32_C(0x508);

    tls_required = dos_base != NULL ? tls_probe_required() : 0;
    if (tls_required != 0) {
        static char tls_arguments[] = "";

        proof->tls_probe_requested = 1;
        AROSAppleCodeMemoryGetStats(&tls_before);
        tls_probe = tls_required == 1 ?
            LoadSeg(marker_path(path, "TLSPROBE")) : BNULL;
        AROSAppleCodeMemoryGetStats(&tls_loaded);
        proof->tls_probe_seglist = (uintptr_t)BADDR(tls_probe);
        if (tls_probe != BNULL) {
            tls_output = Open("NIL:", MODE_NEWFILE);
            if (tls_output != BNULL) {
                previous_output = SelectOutput(tls_output);
                tls_probe_return = RunCommand(tls_probe, AROS_STACKSIZE,
                    tls_arguments, 0);
                SelectOutput(previous_output);
                Close(tls_output);
            }
            UnLoadSeg(tls_probe);
        }
        AROSAppleCodeMemoryGetStats(&tls_unloaded);
        proof->tls_probe_return = (uint64_t)(int64_t)tls_probe_return;
        proof->tls_probe_argument_size = 0;
        proof->tls_pages_before = tls_before.pages_in_use;
        proof->tls_pages_loaded = tls_loaded.pages_in_use;
        proof->tls_pages_unloaded = tls_unloaded.pages_in_use;
        proof->tls_load_allocations =
            tls_loaded.allocation_count - tls_before.allocation_count;
        proof->tls_unload_frees =
            tls_unloaded.free_count - tls_loaded.free_count;
        proof->tls_load_rx_protections =
            tls_loaded.rx_protection_count - tls_before.rx_protection_count;
        proof->tls_unload_rw_protections =
            tls_unloaded.rw_protection_count - tls_loaded.rw_protection_count;
        tls_valid = tls_required == 1 && tls_probe != BNULL &&
            tls_output != BNULL && tls_probe_return == 74 &&
            tls_loaded.pages_in_use > tls_before.pages_in_use &&
            tls_unloaded.pages_in_use == tls_before.pages_in_use &&
            proof->tls_load_allocations != 0 &&
            proof->tls_unload_frees != 0 &&
            proof->tls_load_rx_protections != 0 &&
            proof->tls_unload_rw_protections != 0;
    }
    proof->detail = UINT32_C(0x50a);

    /* The small code model command is the portable CLI command built with
     * -mcmodel=small: the same contract through ADR_PREL_PG_HI21 and the
     * *_ABS_LO12_NC relocations the large-model base set never exercises. */
    small_required = dos_base != NULL ? small_probe_required() : 0;
    if (small_required != 0) {
        static const char small_bad_arguments[] = "wrong\n";
        static const char small_arguments_sys[] =
            "alpha \"two words\" SYS:B2PASS.TXT\n";
        static const char small_arguments_oldsys[] =
            "alpha \"two words\" SYS0:B2PASS.TXT\n";
        const char *small_arguments = pivoted ?
            small_arguments_oldsys : small_arguments_sys;
        const LONG small_arguments_size = pivoted ?
            (LONG)(sizeof(small_arguments_oldsys) - 1U) :
            (LONG)(sizeof(small_arguments_sys) - 1U);

        proof->small_probe_requested = 1;
        AROSAppleCodeMemoryGetStats(&small_before);
        small_probe = small_required == 1 ?
            LoadSeg(marker_path(path, "SMLPROBE")) : BNULL;
        AROSAppleCodeMemoryGetStats(&small_loaded);
        proof->small_probe_seglist = (uintptr_t)BADDR(small_probe);
        if (small_probe != BNULL) {
            small_output = Open("NIL:", MODE_NEWFILE);
            if (small_output != BNULL) {
                previous_output = SelectOutput(small_output);
                small_probe_bad_return = RunCommand(small_probe,
                    AROS_STACKSIZE, small_bad_arguments,
                    sizeof(small_bad_arguments) - 1U);
                small_probe_return = RunCommand(small_probe, AROS_STACKSIZE,
                    small_arguments, small_arguments_size);
                SelectOutput(previous_output);
                Close(small_output);
            }
            UnLoadSeg(small_probe);
        }
        AROSAppleCodeMemoryGetStats(&small_unloaded);
        proof->small_probe_return = (uint64_t)(int64_t)small_probe_return;
        proof->small_probe_bad_return =
            (uint64_t)(int64_t)small_probe_bad_return;
        proof->small_pages_before = small_before.pages_in_use;
        proof->small_pages_loaded = small_loaded.pages_in_use;
        proof->small_pages_unloaded = small_unloaded.pages_in_use;
        proof->small_load_allocations =
            small_loaded.allocation_count - small_before.allocation_count;
        proof->small_unload_frees =
            small_unloaded.free_count - small_loaded.free_count;
        proof->small_load_rx_protections =
            small_loaded.rx_protection_count -
            small_before.rx_protection_count;
        proof->small_unload_rw_protections =
            small_unloaded.rw_protection_count -
            small_loaded.rw_protection_count;
        small_valid = small_required == 1 && small_probe != BNULL &&
            small_output != BNULL && small_probe_bad_return == 30 &&
            small_probe_return == 73 &&
            small_loaded.pages_in_use > small_before.pages_in_use &&
            small_unloaded.pages_in_use == small_before.pages_in_use &&
            proof->small_load_allocations != 0 &&
            proof->small_unload_frees != 0 &&
            proof->small_load_rx_protections != 0 &&
            proof->small_unload_rw_protections != 0;
    }
    proof->detail = UINT32_C(0x50f);

    /* settings.library round trip: the probe copies the shipped font.prefs
     * to ENV:, initialises the library from LIBS: and expects 75. The
     * library stays resident, so the probe's pages come back and the
     * library's do not. */
    settings_required = dos_base != NULL ? settings_probe_required() : 0;
    if (settings_required != 0) {
        static char settings_arguments[] = "";

        proof->settings_probe_requested = 1;
        AROSAppleCodeMemoryGetStats(&settings_before);
        settings_probe = settings_required == 1 ?
            LoadSeg(marker_path(path, "SETPROBE")) : BNULL;
        AROSAppleCodeMemoryGetStats(&settings_loaded);
        proof->settings_probe_seglist = (uintptr_t)BADDR(settings_probe);
        if (settings_probe != BNULL) {
            settings_output = Open("NIL:", MODE_NEWFILE);
            if (settings_output != BNULL) {
                previous_output = SelectOutput(settings_output);
                settings_probe_return = RunCommand(settings_probe, AROS_STACKSIZE,
                    settings_arguments, 0);
                SelectOutput(previous_output);
                Close(settings_output);
            }
            UnLoadSeg(settings_probe);
        }
        AROSAppleCodeMemoryGetStats(&settings_unloaded);
        proof->settings_probe_return = (uint64_t)(int64_t)settings_probe_return;
        proof->settings_pages_before = settings_before.pages_in_use;
        proof->settings_pages_loaded = settings_loaded.pages_in_use;
        proof->settings_pages_unloaded = settings_unloaded.pages_in_use;
        proof->settings_load_allocations =
            settings_loaded.allocation_count - settings_before.allocation_count;
        proof->settings_unload_frees =
            settings_unloaded.free_count - settings_loaded.free_count;
        proof->settings_load_rx_protections =
            settings_loaded.rx_protection_count -
            settings_before.rx_protection_count;
        proof->settings_unload_rw_protections =
            settings_unloaded.rw_protection_count -
            settings_loaded.rw_protection_count;
        /* The probe mounts RAM:, moves ENV: there and leaves the library
         * resident, so the page count after it is not the count before;
         * the step requires the load, its W^X protection and return 75. */
        settings_valid = settings_required == 1 && settings_probe != BNULL &&
            settings_output != BNULL && settings_probe_return == 75 &&
            settings_loaded.pages_in_use > settings_before.pages_in_use &&
            proof->settings_load_allocations != 0 &&
            proof->settings_unload_frees != 0 &&
            proof->settings_load_rx_protections != 0;
    }
    proof->detail = UINT32_C(0x510);

    /* [PWR7] the freeze, the thaw and the two clocks: the probe runs C:Sleep
     * from the image around two child processes and two timer requests, and
     * expects 76. It takes about 25 s of guest time. */
    power_required = dos_base != NULL ? power_probe_required() : 0;
    if (power_required != 0) {
        static char power_arguments[] = "";

        power_probe = power_required == 1 ?
            LoadSeg(marker_path(path, "PWRPROBE")) : BNULL;
        if (power_probe != BNULL) {
            power_output = Open("NIL:", MODE_NEWFILE);
            if (power_output != BNULL) {
                previous_output = SelectOutput(power_output);
                power_probe_return = RunCommand(power_probe, AROS_STACKSIZE,
                    power_arguments, 0);
                SelectOutput(previous_output);
                Close(power_output);
            }
            UnLoadSeg(power_probe);
        }
        power_valid = power_required == 1 && power_probe != BNULL &&
            power_output != BNULL && power_probe_return == 76;
        if (!power_valid) {
            hold_puts("[KI] power probe failed return=");
            hold_put_hex((uint32_t)power_probe_return); hold_puts("\n");
        }
    }
    proof->detail = UINT32_C(0x511);

    wanderer_requirement = dos_base != NULL ? wanderer_required() : 0;
    if (WANDERER_PROFILE(proof->resident_count)) {
        proof->wanderer_requested = 1;
        wanderer_valid = wanderer_requirement == 1;
        if (!wanderer_valid) {
            hold_puts("[KI] Wanderer requirement failed value=");
            hold_put_hex(wanderer_requirement); hold_puts("\n");
        }
    } else if (wanderer_requirement != 0) {
        proof->wanderer_requested = 1;
        wanderer_valid = 0;
    }
    proof->detail = UINT32_C(0x50e);

    if (dos_base == NULL || aros_base == NULL || iffparse_base == NULL ||
        locale_base == NULL || stdc_base == NULL || stdcio_base == NULL ||
        posixc_base == NULL)
        final_detail = UINT32_C(0x520);
    else if (intuition_detail != 0)
        final_detail = intuition_detail;
    else if (process == NULL ||
        !name_equal(process->tc_Node.ln_Name, "Boot Mount"))
        final_detail = UINT32_C(0x521);
    else if (sys_lock == BNULL || file_lock == NULL ||
        file_lock->fl_Task == NULL)
        final_detail = UINT32_C(0x522);
    else if (boot_owner == NULL ||
        (!pivoted && boot_owner != file_lock->fl_Task) ||
        (pivoted && (afssys_handler == NULL ||
            file_lock->fl_Task != afssys_handler ||
            oldsys_file_lock == NULL ||
            oldsys_file_lock->fl_Task != boot_owner)))
        final_detail = UINT32_C(0x523);
    else if (assign_count !=
            sizeof(standard_assigns) / sizeof(standard_assigns[0]) ||
        sentinel_read != (LONG)(sizeof(sentinel_expected) - 1U) ||
        sentinel_eof != 0 || boot_signature_read !=
            (LONG)(sizeof(boot_signature_expected) - 1U))
        final_detail = UINT32_C(0x524);
    else if (abi_probe == BNULL || abi_output == BNULL ||
        abi_probe_bad_return != 30 || abi_probe_return != 73 ||
        proof->abi_probe_entry == 0)
        final_detail = UINT32_C(0x525);
    else if (abi_loaded.pages_in_use <= abi_before.pages_in_use ||
        abi_unloaded.pages_in_use != abi_before.pages_in_use ||
        proof->abi_load_allocations == 0 || proof->abi_unload_frees == 0 ||
        proof->abi_load_rx_protections == 0 ||
        proof->abi_unload_rw_protections == 0)
        final_detail = UINT32_C(0x526);
    else if (!workbench_valid)
        final_detail = UINT32_C(0x527);
    else if (!tls_valid)
        final_detail = UINT32_C(0x528);
    else if (!small_valid)
        final_detail = UINT32_C(0x52d);
    else if (!settings_valid)
        final_detail = UINT32_C(0x52e);
    else if (!power_valid)
        final_detail = UINT32_C(0x52f);
    else if (!wanderer_valid)
        final_detail = UINT32_C(0x529);
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    if (!vm_pci_valid) final_detail = UINT32_C(0x52c);
#endif
    valid = final_detail == 0;
    if (oldsys_lock != BNULL)
        UnLock(oldsys_lock);
    if (sys_lock != BNULL)
        UnLock(sys_lock);
    if (valid) {
        proof->detail = 0;
        if (WANDERER_PROFILE(proof->resident_count)) {
            wanderer_ready_seglist =
                MKBADDR(&AROSAppleG4ReadySegment);
            if (!AddSegment("G4Ready", wanderer_ready_seglist,
                CMD_INTERNAL)) {
                proof->detail = UINT32_C(0x52b);
                AROSAppleRuntimeReturnEL1(0);
            }
            wanderer_launcher_process = CreateNewProcTags(
                NP_Entry, (IPTR)wanderer_launcher_entry,
                NP_Name, (IPTR)"Wanderer G4 launcher",
                NP_Priority, -1,
                NP_StackSize, AROS_STACKSIZE,
                NP_Input, (IPTR)BNULL,
                NP_Output, (IPTR)BNULL,
                NP_Error, (IPTR)BNULL,
                TAG_DONE);

            if (wanderer_launcher_process != NULL) {
                proof->status = 2;
                return NULL;
            }
            proof->detail = UINT32_C(0x52a);
            AROSAppleRuntimeReturnEL1(0);
        }
        proof->status = 1;
        AROSAppleRuntimeReturnEL1(AROS_APPLE_COLDSTART_STATUS_PASS);
    }
    proof->detail = final_detail != 0 ? final_detail :
        (tls_required != 0 && !tls_valid ?
            UINT32_C(0x50b) : UINT32_C(0x509));
    AROSAppleRuntimeReturnEL1(0);

    AROS_USERFUNC_EXIT
}

static int bstr_equal(BPTR value, const char *expected)
{
#ifdef AROS_FAST_BSTR
    return value != BNULL && name_equal((const char *)BADDR(value), expected);
#else
    uint32_t index = 0;
    uint32_t length;

    if (value == BNULL || expected == NULL)
        return 0;
    length = AROS_BSTR_strlen(value);
    while (expected[index] != '\0' && index < length &&
            AROS_BSTR_getchar(value, index) == expected[index])
        ++index;
    return index == length && expected[index] == '\0';
#endif
}

static void clear_proof(struct AROSAppleColdStartProof *proof)
{
    volatile uint64_t *word = (volatile uint64_t *)proof;
    uint32_t index;

    for (index = 0; index < sizeof(*proof) / sizeof(*word); ++index)
        word[index] = 0;
}

/* Preserve an unexpected runtime exception outside the transient vector
 * frame.  Stage two can then emit a deterministic post-mortem instead of
 * leaving the only CPU parked forever in the vector halt loop. */
void AROSAppleColdStartRecordRuntimeException(
    const struct AROSAppleRuntimeFrame *frame)
{
    struct AROSAppleColdStartProof *proof = active_proof;
    const struct Task *task;
    const char *name;
    uint32_t index = 0;

    if (proof == NULL || frame == NULL)
        return;
    proof->runtime_exception_vector = frame->vector;
    proof->runtime_exception_esr = frame->esr;
    proof->runtime_exception_far = frame->far;
    proof->runtime_exception_elr = frame->elr;
    proof->runtime_exception_spsr = frame->spsr;
    proof->runtime_exception_lr = frame->x[30];
    proof->runtime_exception_sp = frame->sp;
    /* x18 is ordinary task state on this target (D-117), never a
     * dereferenceable task: the record names the task the CPU runs. */
    task = SysBase != NULL ? GET_THIS_TASK : NULL;
    proof->runtime_exception_task = (uint64_t)(uintptr_t)task;
    name = task != NULL ? task->tc_Node.ln_Name : NULL;
    if (name != NULL) {
        while (index + 1U < sizeof(proof->runtime_exception_task_name) &&
            name[index] != '\0') {
            proof->runtime_exception_task_name[index] = name[index];
            ++index;
        }
    }
    proof->runtime_exception_task_name[index] = '\0';
}

static uint64_t free_memory(void)
{
    const struct MemHeader *header =
        (const struct MemHeader *)SysBase->MemList.lh_Head;
    uint64_t total = 0;

    while (header != NULL && header->mh_Node.ln_Succ != NULL) {
        if (header->mh_Free < 0 ||
            UINT64_MAX - total < (uint64_t)header->mh_Free)
            return UINT64_MAX;
        total += (uint64_t)header->mh_Free;
        header = (const struct MemHeader *)header->mh_Node.ln_Succ;
    }
    return total;
}

static uint32_t list_count(const struct List *list)
{
    const struct Node *node = list->lh_Head;
    uint32_t count = 0;

    while (node != NULL && node->ln_Succ != NULL) {
        if (++count == UINT32_MAX)
            return UINT32_MAX;
        node = node->ln_Succ;
    }
    return count;
}

static struct Task *find_task(const struct List *list, const char *name)
{
    struct Task *task = (struct Task *)list->lh_Head;

    while (task != NULL && task->tc_Node.ln_Succ != NULL) {
        if (name_equal(task->tc_Node.ln_Name, name))
            return task;
        task = (struct Task *)task->tc_Node.ln_Succ;
    }
    return NULL;
}

static int filesystem_ready(
    const struct FileSysResource *resource,
    uint64_t *segment, uint64_t *entry_count, int expect_afs)
{
    const struct FileSysEntry *entry;
    BPTR common_segment = BNULL;
    uint32_t count = 0;
    uint32_t kinds = 0;
    uint32_t afs_kinds = 0;
    BPTR afs_segment = BNULL;

    if (resource == NULL || resource->fsr_Node.ln_Type != NT_RESOURCE ||
        !name_equal(resource->fsr_Node.ln_Name, "FileSystem.resource"))
        return 0;
    entry = (const struct FileSysEntry *)
        resource->fsr_FileSysEntries.lh_Head;
    while (entry != NULL && entry->fse_Node.ln_Succ != NULL) {
        ULONG required = FSEF_SEGLIST | FSEF_GLOBALVEC | FSEF_PRIORITY;

        if (++count > 16 || (entry->fse_PatchFlags & required) != required ||
            entry->fse_SegList == BNULL ||
            entry->fse_GlobalVec != (BPTR)(SIPTR)-1 ||
            entry->fse_Priority != 10)
            return 0;
        if (entry->fse_DosType == UINT32_C(0x4146532b) ||
            (entry->fse_DosType == 0 &&
             (entry->fse_PatchFlags & FSEF_HANDLER) != 0 &&
             bstr_equal(entry->fse_Handler, "afsplus-handler"))) {
            uint32_t kind = entry->fse_DosType == 0 ? 2U : 1U;
            if (!expect_afs || (afs_kinds & kind) != 0 ||
                (entry->fse_PatchFlags & FSEF_STACKSIZE) == 0 ||
                entry->fse_StackSize != 131072 ||
                (afs_segment != BNULL && afs_segment != entry->fse_SegList))
                return 0;
            afs_kinds |= kind;
            afs_segment = entry->fse_SegList;
            entry = (const struct FileSysEntry *)entry->fse_Node.ln_Succ;
            continue;
        }
        if (common_segment == BNULL)
            common_segment = entry->fse_SegList;
        else if (entry->fse_SegList != common_segment)
            return 0;
        switch (entry->fse_DosType) {
        case 0:
            if ((entry->fse_PatchFlags & FSEF_HANDLER) == 0 ||
                entry->fse_Handler == BNULL)
                return 0;
            kinds |= 1U << 0;
            break;
        case ID_FAT12_DISK:
            kinds |= 1U << 1;
            break;
        case ID_FAT16_DISK:
            kinds |= 1U << 2;
            break;
        case ID_FAT32_DISK:
            kinds |= 1U << 3;
            break;
        default:
            return 0;
        }
        entry = (const struct FileSysEntry *)entry->fse_Node.ln_Succ;
    }
    *segment = (uintptr_t)BADDR(common_segment);
    *entry_count = count;
    return count == 4U + (expect_afs ? 2U : 0U) &&
        afs_kinds == (expect_afs ? 3U : 0U) && kinds == 0x0f && *segment != 0;
}

/* Validate every recognized boot node and its ordering. SYS0 is retained as
 * a fallback; a higher-priority AFSSYS may win normal DOS boot selection. */
static int desktop_storage_nodes_valid(const struct BootNode *first,
    int expect_afs)
{
    const struct BootNode *node = first;
    unsigned seen = 0;
    int previous_priority = 128;
    if (first == NULL || first->bn_Node.ln_Succ == NULL)
        return 0;
    while (node->bn_Node.ln_Succ != NULL) {
        const struct DeviceNode *device = node->bn_DeviceNode;
        const struct FileSysStartupMsg *startup;
        const struct DosEnvec *environment;
        const char *driver = platform != NULL ? platform->storage_device : NULL;
        ULONG dos_type = ID_FAT32_DISK;
        unsigned bit;
        ULONG unit;
        if (device == NULL || node->bn_Node.ln_Type != NT_BOOTNODE ||
            node->bn_Flags != 0 || node->bn_Node.ln_Pri > previous_priority)
            return 0;
        previous_priority = node->bn_Node.ln_Pri;
        if (bstr_equal(device->dn_Name, "ESP")) { bit = 1; unit = 0; }
        else if (bstr_equal(device->dn_Name, "WORK")) { bit = 2; unit = 1; }
        else if (bstr_equal(device->dn_Name, "AFSSYS") && expect_afs) {
            bit = 4; unit = 2; dos_type = UINT32_C(0x4146532b);
        } else if (bstr_equal(device->dn_Name, "DATA") && expect_afs) {
            bit = 16; unit = 3; dos_type = UINT32_C(0x4146532b);
        } else if (bstr_equal(device->dn_Name, "SYS0")) {
            bit = 8; unit = 0; dos_type = ID_FAT12_DISK;
            driver = "appleboot.device";
            if (node->bn_Node.ln_Pri != 10) return 0;
        } else return 0;
        if (seen & bit) return 0;
        if ((bit == 1 || bit == 2 || bit == 16) && node->bn_Node.ln_Pri != -128)
            return 0;
        seen |= bit;
        startup = BADDR(device->dn_Startup);
        if (startup == NULL || startup->fssm_Unit != unit ||
            (driver == NULL || !bstr_equal(startup->fssm_Device, driver)))
            return 0;
        environment = BADDR(startup->fssm_Environ);
        if (environment == NULL || environment->de_DosType != dos_type)
            return 0;
        node = (const struct BootNode *)node->bn_Node.ln_Succ;
    }
    return (seen & 8) != 0;
}

static int resident_at(
    const struct AROSAppleKernelStartupState *state, uint32_t index,
    const char *name, BYTE priority, ULONG flags)
{
    struct Resident **residents =
        (struct Resident **)(uintptr_t)state->resident_list;
    const struct Resident *resident;

    if (index >= state->resident_count)
        return 0;
    resident = residents[index];
    return resident != NULL && resident->rt_MatchWord == RTC_MATCHWORD &&
        resident->rt_MatchTag == resident &&
        name_equal(resident->rt_Name, name) && resident->rt_Pri == priority &&
        (flags == 0 ? resident->rt_Flags == 0 :
            (resident->rt_Flags & flags) == flags);
}

static int find_resident_index(
    const struct AROSAppleKernelStartupState *state, const char *name)
{
    struct Resident **residents =
        (struct Resident **)(uintptr_t)state->resident_list;
    uint32_t index;

    if (residents == NULL)
        return -1;
    for (index = 0; index < state->resident_count &&
            index < AROS_APPLE_STARTUP_MAX_RESIDENTS; index++) {
        const struct Resident *resident = residents[index];

        if (resident != NULL && resident->rt_MatchWord == RTC_MATCHWORD &&
                resident->rt_MatchTag == resident &&
                name_equal(resident->rt_Name, name))
            return (int)index;
    }
    return -1;
}

static int desktop_profile_detect(
    const struct AROSAppleKernelStartupState *state)
{
    return find_resident_index(state, "muimaster.library") >= 0 &&
        find_resident_index(state, "con-handler") >= 0;
}


/*
 * The desktop bundle's residents in the order the loader sorts them: by
 * priority, and by bundle order within a priority. One row per component;
 * the proof checks every position, name, priority and flag against it.
 */
static const struct ColdStartResident desktop_residents[] = {
    { "kernel.resource", 127, RTF_SINGLETASK },
    { "exec.library", 120, RTF_COLDSTART },
    { "expansion.library", 110, RTF_SINGLETASK },
    { "debug.library", 105, RTF_SINGLETASK },
    { "utility.library", 103, RTF_COLDSTART },
    { "aros.library", 101, RTF_COLDSTART },
    { "bootloader.resource", 100, RTF_COLDSTART },
    { "task.resource", 100, RTF_COLDSTART },
    { "processor.resource", 99, RTF_COLDSTART },
    { "oop.library", 94, RTF_COLDSTART },
    { "hiddclass.hidd", 92, RTF_COLDSTART },
    { "FileSystem.resource", 80, RTF_COLDSTART },
    { "gfx.hidd", 66, RTF_COLDSTART },
    { "graphics.library", 65, RTF_COLDSTART },
    { "layers.library", 64, RTF_COLDSTART },
    { "timer.device", 50, RTF_COLDSTART },
    { "entropy.resource", 45, RTF_COLDSTART | RTF_AUTOINIT },
    { "inputclass.hidd", 45, RTF_COLDSTART },
    { "keyboard.hidd", 45, RTF_COLDSTART },
    { "mouse.hidd", 45, RTF_COLDSTART },
    { "keyboard.device", 44, RTF_COLDSTART },
    { "gameport.device", 44, RTF_COLDSTART },
    { "partition.library", 40, RTF_COLDSTART },
    { "keymap.library", 40, RTF_COLDSTART },
    { "input.device", 30, RTF_COLDSTART },
    { "intuition.library", 15, RTF_COLDSTART },
    { "efifbgfx.hidd", 9, RTF_COLDSTART },
    { "cybergraphics.library", 8, RTF_COLDSTART },
    { "console.device", 4, RTF_COLDSTART | RTF_AUTOINIT },
    { "locale.library", 0, RTF_COLDSTART },
    { "stdc.library", 0, RTF_COLDSTART },
    { "stdcio.library", 0, RTF_COLDSTART },
    { "posixc.library", 0, RTF_COLDSTART },
    { "asl.library", 0, RTF_COLDSTART },
    { "commodities.library", 0, RTF_COLDSTART },
    { "rexxsyslib.library", 0, RTF_COLDSTART },
    { "gadtools.library", 0, RTF_COLDSTART },
    { "datatypes.library", 0, RTF_COLDSTART },
    { "diskfont.library", 0, RTF_COLDSTART },
    { "coolimages.library", 0, RTF_COLDSTART },
    { "muiscreen.library", 0, RTF_COLDSTART },
    { "muimaster.library", 0, RTF_COLDSTART },
    { "IconList.mui", 0, RTF_COLDSTART },
    { "IconDrawerList.mui", 0, RTF_COLDSTART },
    { "IconVolumeList.mui", 0, RTF_COLDSTART },
    { "IconListview.mui", 0, RTF_COLDSTART },
    { "fat-handler", -1, RTF_COLDSTART },
    { "appleboot.device", -40, RTF_COLDSTART },
    { "dos.library", -120, 0 },
    { "workbench.library", -120, RTF_AUTOINIT },
    { "iffparse.library", -121, RTF_AFTERDOS },
    { "icon.library", -122, RTF_AFTERDOS },
    { "shell.resource", -123, RTF_AFTERDOS },
    { "shellcommands.resource", -123, RTF_AFTERDOS },
    { "lddemon.resource", -123, RTF_AFTERDOS },
    { "con-handler", -124, RTF_AFTERDOS },
    { "ram-handler", -125, RTF_AFTERDOS },
};
#define DESKTOP_RESIDENT_COUNT \
    ((uint32_t)(sizeof(desktop_residents) / sizeof(desktop_residents[0])))

static const struct ColdStartResident *desktop_resident_at(uint32_t index)
{
    uint32_t common = 0, extra = 0;
    const struct ColdStartResident *row;
    do {
        if (platform != NULL && extra < platform->resident_count &&
            (common == DESKTOP_RESIDENT_COUNT ||
             platform->residents[extra].priority >= desktop_residents[common].priority))
            row = &platform->residents[extra++];
        else
            row = &desktop_residents[common++];
    } while (index-- != 0);
    return row;
}

static void core_coldstart_el0(struct AROSAppleRuntimeProof *unused)
{
    struct AROSAppleColdStartProof *proof = active_proof;
    IPTR *all_resident_modules = SysBase->ResModules;
    IPTR coldstart_modules[AROS_APPLE_STARTUP_MAX_RESIDENTS + 1U];
    struct Library *utility;
    struct ExpansionBase *expansion;
    struct Library *partition;
    struct Task *housekeeper;
    struct Task *guru;
    struct Node *task_resource;
    struct Node *bootloader_resource;
    int platform_resources_valid;
    struct Library *oop_library;
    struct Library *hiddclass_library;
    struct Library *gfx_hidd_library;
    struct Library *graphics_library;
    struct Library *efifbgfx_library;
    struct FileSysResource *filesystem_resource;
    struct VesaInfo *video = NULL;
    struct AROSAppleFramebufferInfo framebuffer;
    struct Resident *dos_resident;
    struct Device *appleboot_device = NULL;
    struct BootNode *boot_node = NULL;
    struct DeviceNode *device_node = NULL;
    struct FileSysStartupMsg *startup = NULL;
    struct DosEnvec *environment = NULL;
    struct Task *bootstrap;
    struct MsgPort *timer_port = NULL;
    struct timerequest *timer_request = NULL;
    struct IOExtTD *boot_request = NULL;
    struct Device *timer_device = NULL;
    struct EClockVal eclock_before = {0, 0};
    struct EClockVal eclock_after = {0, 0};
    struct timeval time_before = {0, 0};
    struct timeval time_after = {0, 0};
    ULONG timer_frequency = 0;
    UBYTE boot_sector[512];
    UBYTE write_probe = 0;
    uintptr_t boot_image_start = 0;
    uint64_t boot_image_size = 0;
    LONG boot_read_error = IOERR_OPENFAIL;
    LONG boot_bounds_error = IOERR_OPENFAIL;
    LONG boot_write_error = IOERR_OPENFAIL;
    uint32_t attempt;
    uint32_t graphics_detail = 0;
    uint32_t validation_detail = 0;
    uint32_t source_index;
    uint32_t coldstart_count = 0;
    uint64_t result = 0;
    int valid;

    (void)unused;
    proof->el0_is_super = (uint64_t)AROSAppleRuntimeIsSuper();
    if (proof->resident_count == 34 || WANDERER_PROFILE(proof->resident_count)) {
        for (source_index = 0;
            source_index < AROS_APPLE_STARTUP_MAX_RESIDENTS &&
                all_resident_modules[source_index] != 0;
            ++source_index) {
            struct Resident *resident =
                (struct Resident *)all_resident_modules[source_index];

            if (!ui_deferred_resident(resident))
                coldstart_modules[coldstart_count++] =
                    all_resident_modules[source_index];
        }
        coldstart_modules[coldstart_count] = 0;
        SysBase->ResModules = coldstart_modules;
    }
    /* A failure inside InitCode must remain distinguishable from later checks. */
    proof->detail = UINT32_C(0x400);
    InitCode(RTF_COLDSTART, 0);
    SysBase->ResModules = all_resident_modules;
    proof->detail = UINT32_C(0x401);

    bootstrap = GET_THIS_TASK;
    utility = (struct Library *)FindName(
        &SysBase->LibList, "utility.library");
    expansion = (struct ExpansionBase *)FindName(
        &SysBase->LibList, "expansion.library");
    partition = (struct Library *)FindName(
        &SysBase->LibList, "partition.library");
    task_resource = FindName(&SysBase->ResourceList, "task.resource");
    proof_task_base = (APTR)task_resource;
    bootloader_resource = FindName(
        &SysBase->ResourceList, "bootloader.resource");
    platform_resources_valid = platform == NULL ||
        platform->resources_valid(proof->resident_count);
    proof_bootloader_base = (APTR)bootloader_resource;
    if (proof_bootloader_base != NULL)
        video = (struct VesaInfo *)GetBootInfo(BL_Video);
    oop_library = (struct Library *)FindName(
        &SysBase->LibList, "oop.library");
    hiddclass_library = (struct Library *)FindName(
        &SysBase->LibList, "hiddclass.hidd");
    gfx_hidd_library = (struct Library *)FindName(
        &SysBase->LibList, "gfx.hidd");
    graphics_library = (struct Library *)FindName(
        &SysBase->LibList, "graphics.library");
    proof_graphics_base = (struct GfxBase *)graphics_library;
    efifbgfx_library = (struct Library *)FindName(
        &SysBase->LibList, "efifbgfx.hidd");
    filesystem_resource = (struct FileSysResource *)
        OpenResource("FileSystem.resource");
    dos_resident = FindResident("dos.library");
    housekeeper = find_task(&SysBase->TaskReady, "Exec housekeeper");
    if (housekeeper == NULL)
        housekeeper = find_task(&SysBase->TaskWait, "Exec housekeeper");
    guru = find_task(&SysBase->TaskReady, "Exec Guru Task");
    if (guru == NULL)
        guru = find_task(&SysBase->TaskWait, "Exec Guru Task");

    timer_port = CreateMsgPort();
    if (timer_port != NULL)
        timer_request = (struct timerequest *)CreateIORequest(
            timer_port, sizeof(*timer_request));
    if (timer_request != NULL &&
        OpenDevice(TIMERNAME, UNIT_VBLANK,
            &timer_request->tr_node, 0) == 0) {
        timer_device = timer_request->tr_node.io_Device;
        timer_frequency = ReadEClock(&eclock_before);
        GetSysTime(&time_before);
        eclock_after = eclock_before;
        for (attempt = 0; attempt < 4096 &&
            eclock_after.ev_hi == eclock_before.ev_hi &&
            eclock_after.ev_lo == eclock_before.ev_lo; ++attempt)
            (void)ReadEClock(&eclock_after);
        GetSysTime(&time_after);
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
        struct KrnPlatformTimer *platform =
            (struct KrnPlatformTimer *)KrnGetSystemAttr(KATTR_PlatformTimer);
        if (platform != NULL && platform != (APTR)-1 &&
            (platform->kpt_Flags & KPTF_DEADLINE_READY)) {
            struct AROSAppleTimerControl control = {0};
            uint64_t before, started = KrnPlatformTimerRead();
            int passed = 1;
            control.version = AROS_APPLE_TIMER_CONTROL_VERSION;
            control.size = sizeof(control);
            control.operation = AROS_APPLE_TIMER_OP_QUERY;
            passed = KrnPlatformTimerControl(&control) == AROS_APPLE_TIMER_OK;
            before = control.fired_count;
            for (unsigned request = 0; passed && request < 3; ++request) {
                timer_request->tr_node.io_Command = TR_ADDREQUEST;
                timer_request->tr_time.tv_secs = 0;
                timer_request->tr_time.tv_micro = 20000;
                passed = DoIO(&timer_request->tr_node) == 0;
            }
            passed = passed && KrnPlatformTimerControl(&control) == AROS_APPLE_TIMER_OK &&
                control.fired_count >= before + 3 &&
                KrnPlatformTimerRead() - started >= (uint64_t)timer_frequency * 60 / 1000;
            hold_puts(passed ? "[VM] timer.device three waits PASS\n" :
                "[VM] FAIL: timer.device waits\n");
            if (!passed) timer_frequency = 0;
            struct TagItem irq_attributes[] = {
                { KERNELTAG_IRQ_TRIGGERLEVEL, 2 },
                { KERNELTAG_IRQ_POLARITY, 1 }, { TAG_DONE, 0 }
            };
            passed = passed && KrnModifyIRQA(32, irq_attributes) != 0 &&
                KrnModifyIRQA(27, irq_attributes) == 0;
            irq_attributes[1].ti_Data = 2;
            passed = passed && KrnModifyIRQA(32, irq_attributes) == 0;
            struct TagItem cycle[] = {{ TAG_MORE, 0 }};
            cycle[0].ti_Data = (IPTR)cycle;
            passed = passed && KrnModifyIRQA(32, cycle) == 0;
            irq_attributes[1].ti_Data = 1;
            struct TagItem redirect[] = {{ TAG_IGNORE, 0 }, { TAG_MORE, (IPTR)irq_attributes }};
            passed = passed && KrnModifyIRQA(32, redirect) != 0;
            passed = passed && !KrnAddIRQHandler(33, irq_probe_callback,
                (void *)&irq_probe_first, (void *)(uintptr_t)0x495251) &&
                AROSAppleRuntimeSupervisor(AROSVMKernelProbePrepare);
            irq_probe_first = irq_probe_second = 0;
            void *first = KrnAddIRQHandler(32, irq_probe_callback,
                (void *)&irq_probe_first, (void *)(uintptr_t)0x495251);
            void *second = KrnAddIRQHandler(32, irq_probe_callback,
                (void *)&irq_probe_second, (void *)(uintptr_t)0x495251);
            passed = passed && first && second &&
                KrnModifyIRQA(32, irq_attributes) == 0 &&
                AROSAppleRuntimeSupervisor(AROSVMKernelProbeIRQ);
            timer_request->tr_node.io_Command = TR_ADDREQUEST;
            timer_request->tr_time.tv_secs = 0;
            timer_request->tr_time.tv_micro = 20000;
            passed = DoIO(&timer_request->tr_node) == 0 && passed &&
                irq_probe_first == 1 && irq_probe_second == 1;
            if (first) KrnRemIRQHandler(first);
            passed = passed && AROSAppleRuntimeSupervisor(AROSVMKernelProbeIRQ);
            timer_request->tr_time.tv_secs = 0;
            timer_request->tr_time.tv_micro = 20000;
            passed = DoIO(&timer_request->tr_node) == 0 && passed &&
                irq_probe_first == 1 && irq_probe_second == 2;
            if (second) KrnRemIRQHandler(second);
            passed = passed && !AROSAppleRuntimeSupervisor(AROSVMKernelProbeIRQ) &&
                AROSAppleRuntimeSupervisor(AROSVMKernelProbeFinish);
            hold_puts(passed ? "[VM] IRQ handler chain PASS\n" :
                "[VM] FAIL: IRQ handler chain\n");
            if (!passed) timer_frequency = 0;
        }
#endif
        if (!prove_dispatch_masks(timer_request, timer_frequency))
            timer_frequency = 0;
        AROSAppleKernelIdleTraceEnable = 1;
        CloseDevice(&timer_request->tr_node);
    }
    proof->detail = UINT32_C(0x402);

    if (AROSAppleKernelChosenDecimal("aros,mp1-proof") == 1 &&
        !AROSAppleGuardProof()) {
        AROSAppleRuntimeReturnEL1(0);
        return;
    }

    if (timer_port != NULL)
        boot_request = (struct IOExtTD *)CreateIORequest(
            timer_port, sizeof(*boot_request));
    proof->detail = UINT32_C(0x403);
    if (boot_request != NULL &&
        OpenDevice("appleboot.device", 0,
            &boot_request->iotd_Req, 0) == 0) {
        proof->detail = UINT32_C(0x404);
        appleboot_device = boot_request->iotd_Req.io_Device;
        boot_request->iotd_Req.io_Command = CMD_READ;
        boot_request->iotd_Req.io_Data = boot_sector;
        boot_request->iotd_Req.io_Length = sizeof(boot_sector);
        boot_request->iotd_Req.io_Offset = 0;
        boot_read_error = DoIO(&boot_request->iotd_Req);
        proof->detail = UINT32_C(0x405);
        proof->boot_read_actual = boot_request->iotd_Req.io_Actual;
        boot_request->iotd_Req.io_Command = CMD_READ;
        boot_request->iotd_Req.io_Data = &write_probe;
        boot_request->iotd_Req.io_Length = sizeof(write_probe);
        boot_request->iotd_Req.io_Offset = UINT32_MAX;
        boot_bounds_error = DoIO(&boot_request->iotd_Req);
        boot_request->iotd_Req.io_Command = CMD_WRITE;
        boot_request->iotd_Req.io_Data = &write_probe;
        boot_request->iotd_Req.io_Length = sizeof(write_probe);
        boot_request->iotd_Req.io_Offset = 0;
        boot_write_error = DoIO(&boot_request->iotd_Req);
        proof->detail = UINT32_C(0x406);
        CloseDevice(&boot_request->iotd_Req);
        proof->detail = UINT32_C(0x407);
    }

    if (expansion != NULL) {
        /* The checks below concern the retained RAM fallback image, not
         * which valid node DOS will select from the unmodified MountList. */
        boot_node = (struct BootNode *)expansion->MountList.lh_Head;
        while (boot_node->bn_Node.ln_Succ != NULL &&
            (boot_node->bn_DeviceNode == NULL ||
             !bstr_equal(((struct DeviceNode *)boot_node->bn_DeviceNode)->dn_Name, "SYS0")))
            boot_node = (struct BootNode *)boot_node->bn_Node.ln_Succ;
        if (boot_node->bn_Node.ln_Succ == NULL)
            boot_node = NULL;
    }
    if (boot_node != NULL && boot_node->bn_Node.ln_Succ != NULL) {
        device_node = boot_node->bn_DeviceNode;
        if (device_node != NULL) {
            startup = BADDR(device_node->dn_Startup);
            if (startup != NULL)
                environment = BADDR(startup->fssm_Environ);
        }
    }
    if (expansion != NULL)
        proof->mount_count = list_count(&expansion->MountList);
    proof->detail = UINT32_C(0x408);
    (void)AROSAppleKernelRetainedBootImage(
        &boot_image_start, &boot_image_size);

    proof->free_after = free_memory();
    proof->bootstrap_task = (uintptr_t)bootstrap;
    if (bootstrap != NULL && (bootstrap->tc_Flags & TF_ETASK) != 0 &&
        bootstrap->tc_UnionETask.tc_ETask != NULL)
        proof->bootstrap_context = (uintptr_t)
            bootstrap->tc_UnionETask.tc_ETask->et_RegFrame;
    proof->kernel_base = (uintptr_t)OpenResource("kernel.resource");
    proof->utility_base = (uintptr_t)utility;
    proof->task_base = (uintptr_t)task_resource;
    proof->expansion_base = (uintptr_t)expansion;
    proof->bootloader_base = (uintptr_t)bootloader_resource;
    proof->partition_base = (uintptr_t)partition;
    proof->housekeeper_task = (uintptr_t)housekeeper;
    proof->guru_task = (uintptr_t)guru;
    proof->ready_count = list_count(&SysBase->TaskReady);
    proof->wait_count = list_count(&SysBase->TaskWait);
    proof->dispatch_count_after = SysBase->DispCount;
    proof->timer_base = (uintptr_t)timer_device;
    proof->timer_frequency = timer_frequency;
    proof->eclock_before = ((uint64_t)eclock_before.ev_hi << 32) |
        eclock_before.ev_lo;
    proof->eclock_after = ((uint64_t)eclock_after.ev_hi << 32) |
        eclock_after.ev_lo;
    proof->time_before_usec = (uint64_t)time_before.tv_secs * 1000000ULL +
        time_before.tv_micro;
    proof->time_after_usec = (uint64_t)time_after.tv_secs * 1000000ULL +
        time_after.tv_micro;
    proof->filesystem_base = (uintptr_t)filesystem_resource;
    proof->dos_resident = (uintptr_t)dos_resident;
    proof->appleboot_base = (uintptr_t)appleboot_device;
    proof->boot_node = (uintptr_t)boot_node;
    proof->boot_image_start = boot_image_start;
    proof->boot_image_size = boot_image_size;
    proof->boot_bounds_error = (uint64_t)(int64_t)boot_bounds_error;
    proof->boot_write_error = (uint64_t)(int64_t)boot_write_error;
    proof->detail = UINT32_C(0x409);

    if (boot_request != NULL)
        DeleteIORequest(&boot_request->iotd_Req);
    proof->detail = UINT32_C(0x40a);
    if (timer_request != NULL)
        DeleteIORequest(&timer_request->tr_node);
    proof->detail = UINT32_C(0x40b);
    if (timer_port != NULL)
        DeleteMsgPort(timer_port);
    proof->detail = UINT32_C(0x40c);

    valid = proof->el0_is_super == 0 && bootstrap != NULL &&
        bootstrap->tc_State == TS_RUN &&
        name_equal(bootstrap->tc_Node.ln_Name,
#if defined(__AROSEXEC_SMP__)
            "Exec BSP Bootstrap Task"
#else
            "Exec Bootstrap Task"
#endif
        ) &&
        proof->bootstrap_context != 0 && proof->kernel_base != 0 &&
        utility != NULL && utility->lib_Node.ln_Type == NT_LIBRARY &&
        expansion != NULL && expansion->LibNode.lib_Node.ln_Type == NT_LIBRARY &&
        partition != NULL && partition->lib_Node.ln_Type == NT_LIBRARY &&
        task_resource != NULL && task_resource->ln_Type == NT_RESOURCE &&
        bootloader_resource != NULL &&
        bootloader_resource->ln_Type == NT_RESOURCE &&
        platform_resources_valid &&
        housekeeper != NULL && guru != NULL &&
        timer_device != NULL && timer_frequency != 0;
    if (!valid)
        validation_detail = UINT32_C(0x40d);
    proof->detail = UINT32_C(0x40d);

    if (valid)
        valid = filesystem_ready(filesystem_resource, &proof->fat_seglist,
            &proof->filesystem_entry_count,
            FindResident("afsplus-handler") != NULL);
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x40e);
    proof->detail = UINT32_C(0x40e);

    if (valid)
        valid = dos_resident != NULL &&
            FindName(&SysBase->LibList, "dos.library") == NULL;
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x40f);
    proof->detail = UINT32_C(0x40f);

    if (valid)
        valid = appleboot_device != NULL &&
            boot_read_error == 0 && proof->boot_read_actual == 512 &&
            boot_sector[510] == 0x55 && boot_sector[511] == 0xaa &&
            boot_bounds_error == IOERR_BADADDRESS &&
            boot_write_error == TDERR_WriteProt &&
            boot_image_start != 0 && boot_image_size != 0 &&
            boot_image_size >= sizeof(boot_sector);
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x410);
    proof->detail = UINT32_C(0x410);

    /* The ordinary RAM-only path stays exact. With desktop storage present,
     * validate the full ordered list, while proving SYS0 as a usable fallback. */
    if (valid)
        valid = (proof->mount_count == 1 ||
                (desktop_profile_flag && proof->mount_count <= AROS_APPLE_DESKTOP_STORAGE_NODE_MAX &&
                 desktop_storage_nodes_valid(
                    (struct BootNode *)expansion->MountList.lh_Head,
                    FindResident("afsplus-handler") != NULL))) &&
            boot_node != NULL &&
            boot_node->bn_Node.ln_Type == NT_BOOTNODE &&
            boot_node->bn_Node.ln_Pri == 10 && boot_node->bn_Flags == 0;
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x411);
    proof->detail = UINT32_C(0x411);

    if (valid)
        valid = device_node != NULL &&
            bstr_equal(device_node->dn_Name, "SYS0");
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x412);
    proof->detail = UINT32_C(0x412);

    if (valid)
        valid = startup != NULL && bstr_equal(startup->fssm_Device,
            "appleboot.device");
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x413);
    proof->detail = UINT32_C(0x413);

    if (valid)
        valid = startup->fssm_Unit == 0 && environment != NULL &&
            environment->de_DosType == ID_FAT12_DISK;
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x414);
    proof->detail = UINT32_C(0x414);

    if (valid)
        valid = proof->eclock_after > proof->eclock_before &&
        proof->time_after_usec >= proof->time_before_usec &&
        proof->free_after < proof->free_before &&
        proof->ready_count != UINT32_MAX &&
        proof->wait_count != UINT32_MAX;
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x415);
    proof->detail = UINT32_C(0x415);

    if (valid && vm_graphics_profile_flag) {
        valid = oop_library != NULL && hiddclass_library != NULL &&
            gfx_hidd_library != NULL && graphics_library != NULL &&
            FindName(&SysBase->LibList, "virtiogfx.hidd") != NULL;
    }
    if (valid && !vm_graphics_profile_flag && (proof->resident_count == 25 ||
        proof->resident_count == 34 || WANDERER_PROFILE(proof->resident_count))) {
        ULONG expected_masks[4];
        ULONG expected_shifts[4];
        uint64_t expected_size_kib;

        valid = AROSAppleKernelRetainedFramebuffer(&framebuffer) &&
            oop_library != NULL && hiddclass_library != NULL &&
            gfx_hidd_library != NULL && graphics_library != NULL &&
            efifbgfx_library != NULL && video != NULL &&
            graphics_probe_expected_masks(framebuffer.format,
                expected_masks, expected_shifts);
        if (valid) {
            expected_size_kib = framebuffer.size / 1024U +
                ((framebuffer.size % 1024U) != 0);
            valid = video->FrameBuffer ==
                    (APTR)(uintptr_t)framebuffer.physical_address &&
                video->FrameBufferSize == expected_size_kib &&
                video->XSize == framebuffer.width &&
                video->YSize == framebuffer.height &&
                video->BytesPerLine == framebuffer.pitch &&
                video->BitsPerPixel == framebuffer.depth &&
                video->Masks[VI_Red] == expected_masks[VI_Red] &&
                video->Masks[VI_Green] == expected_masks[VI_Green] &&
                video->Masks[VI_Blue] == expected_masks[VI_Blue] &&
                video->Masks[VI_Alpha] == expected_masks[VI_Alpha] &&
                video->Shifts[VI_Red] == expected_shifts[VI_Red] &&
                video->Shifts[VI_Green] == expected_shifts[VI_Green] &&
                video->Shifts[VI_Blue] == expected_shifts[VI_Blue] &&
                video->Shifts[VI_Alpha] == expected_shifts[VI_Alpha];
        }
    }
    if (!valid && validation_detail == 0)
        validation_detail = UINT32_C(0x416);
    proof->detail = UINT32_C(0x416);

    /*
     * G2 deliberately retains its full-screen View and bitmap for the final
     * capture.  G3 proves the same graphics path through its Intuition Screen
     * and Window, so it must not consume a competing displayable bitmap here.
     */
    if (valid && proof->resident_count == 25) {
        graphics_detail = graphics_render_probe(&framebuffer);
        valid = graphics_detail == 0;
    }
    proof->detail = graphics_detail != 0 ? graphics_detail : UINT32_C(0x417);

    if (!valid && graphics_detail == 0 && validation_detail == 0)
        validation_detail = UINT32_C(0x418);
    proof->detail = UINT32_C(0x418);

    if (valid) {
        proof->detail = UINT32_C(0x500);
        if (install_dos_observer())
            InitResident(dos_resident, BNULL);
        proof->detail = UINT32_C(0x507);
    } else {
        if (graphics_detail != 0)
            proof->detail = graphics_detail;
        else if (validation_detail != 0)
            proof->detail = validation_detail;
        else
            proof->detail = 2;
    }
    AROSAppleRuntimeReturnEL1(result);
}

int AROSAppleKernelProveCoreColdStart(
    uintptr_t user_stack, uint64_t user_stack_size,
    const struct AROSAppleKernelStartupState *state,
    struct AROSAppleColdStartProof *proof)
{
    uintptr_t stack_top;
    struct AROSApplePayloadIdentity retained_identity;

    if (state == NULL || proof == NULL || ((uintptr_t)proof & 15U) != 0)
        return 0;
    clear_proof(proof);
    proof->version = AROS_APPLE_COLDSTART_PROOF_VERSION;
    proof->size = sizeof(*proof);
#define REQUIRE_ENTRY(condition, code) \
    do { \
        if (!(condition)) { \
            proof->detail = (code); \
            return 0; \
        } \
    } while (0)
    REQUIRE_ENTRY(state->version == AROS_APPLE_STARTUP_STATE_VERSION,
        UINT32_C(0x101));
    REQUIRE_ENTRY(state->size == sizeof(*state), UINT32_C(0x102));
    REQUIRE_ENTRY(state->phase == AROS_APPLE_KERNEL_PHASE_MEMORY_READY,
        UINT32_C(0x103));
    REQUIRE_ENTRY(state->status == AROS_APPLE_KERNEL_STARTUP_OK,
        UINT32_C(0x104));
    proof->resident_count = state->resident_count;
    desktop_profile_flag = state->resident_list != 0 &&
        state->resident_count <= AROS_APPLE_STARTUP_MAX_RESIDENTS &&
        desktop_profile_detect(state);
#if defined(AROS_VM_GUEST) && AROS_VM_GUEST
    vm_pci_profile_flag = desktop_profile_flag &&
        find_resident_index(state, "pci.hidd") >= 0;
    vm_graphics_profile_flag = desktop_profile_flag &&
        find_resident_index(state, "virtiogfx.hidd") >= 0;
#endif
    REQUIRE_ENTRY(state->resident_count == 20 || state->resident_count == 21 ||
        state->resident_count == 22 || state->resident_count == 25 ||
        state->resident_count == 34 || WANDERER_PROFILE(state->resident_count),
        UINT32_C(0x105));
    REQUIRE_ENTRY(state->resident_list != 0, UINT32_C(0x106));
    REQUIRE_ENTRY(SysBase != NULL, UINT32_C(0x107));
    REQUIRE_ENTRY(GET_THIS_TASK == NULL, UINT32_C(0x108));
    REQUIRE_ENTRY(IsListEmpty(&SysBase->TaskReady), UINT32_C(0x109));
    REQUIRE_ENTRY(user_stack != 0 && (user_stack & 15U) == 0,
        UINT32_C(0x10a));
    REQUIRE_ENTRY(user_stack_size >= AROS_APPLE_RUNTIME_STACK_MIN,
        UINT32_C(0x10b));
    REQUIRE_ENTRY(user_stack_size <= UINTPTR_MAX - user_stack,
        UINT32_C(0x10c));
    REQUIRE_ENTRY(AROSAppleRuntimeLocal()->active != 0, UINT32_C(0x10d));
    REQUIRE_ENTRY(AROSAppleRuntimeLocal()->transition_active == 0,
        UINT32_C(0x10e));
    REQUIRE_ENTRY(state->payload_identity.size != 0 &&
        payload_digest_nonzero(state->payload_identity.sha256) &&
        state->payload_identity.reserved == 0, UINT32_C(0x10f));
    REQUIRE_ENTRY(
        (state->payload_identity.producer == AROS_BOOT_PRODUCER_UEFI &&
         state->payload_identity.generation_sequence != 0 &&
         state->payload_identity.slot <= 1 &&
         (state->payload_identity.flags & AROS_BOOT_PAYLOAD_DIRECT) == 0) ||
        ((state->payload_identity.producer == AROS_BOOT_PRODUCER_M1N1 ||
          state->payload_identity.producer == AROS_BOOT_PRODUCER_UEFI) &&
         state->payload_identity.generation_sequence == 0 &&
         state->payload_identity.slot == AROS_BOOT_PAYLOAD_SLOT_DIRECT &&
         state->payload_identity.flags == AROS_BOOT_PAYLOAD_DIRECT),
        UINT32_C(0x110));
    REQUIRE_ENTRY(AROSAppleKernelRetainedPayloadIdentity(&retained_identity) &&
        payload_identity_equal(&retained_identity, &state->payload_identity),
        UINT32_C(0x111));
#undef REQUIRE_ENTRY

    proof->kernel_index = 0;
    proof->exec_index = 1;
    proof->expansion_index = 2;
    proof->pmgr_index = UINT32_MAX;
    proof->rtkit_index = UINT32_MAX;
    proof->utility_index = (uint32_t)find_resident_index(state, "utility.library");
    proof->aros_index = (uint32_t)find_resident_index(state, "aros.library");
    proof->bootloader_index = (uint32_t)find_resident_index(state, "bootloader.resource");
    proof->task_index = (uint32_t)find_resident_index(state, "task.resource");
    proof->filesystem_index = (uint32_t)find_resident_index(state, "FileSystem.resource");
    proof->timer_index = (uint32_t)find_resident_index(state, "timer.device");
    proof->partition_index = (uint32_t)find_resident_index(state, "partition.library");
    proof->locale_index = (uint32_t)find_resident_index(state, "locale.library");
    proof->stdc_index = (uint32_t)find_resident_index(state, "stdc.library");
    proof->stdcio_index = (uint32_t)find_resident_index(state, "stdcio.library");
    proof->posixc_index = (uint32_t)find_resident_index(state, "posixc.library");
    proof->fat_index = (uint32_t)find_resident_index(state, "fat-handler");
    proof->appleboot_index = (uint32_t)find_resident_index(state, "appleboot.device");
    proof->dos_index = (uint32_t)find_resident_index(state, "dos.library");
    proof->iffparse_index = (uint32_t)find_resident_index(state, "iffparse.library");
    if (platform != NULL && !platform->prepare_proof(state, proof,
            desktop_profile_flag, resident_at, find_resident_index))
        return 0;
#define REQUIRE_RESIDENT(index, name, priority, flags) \
    do { \
        if (!resident_at(state, (index), (name), (priority), (flags))) { \
            proof->detail = UINT32_C(0x300) + (index); \
            return 0; \
        } \
    } while (0)

    REQUIRE_RESIDENT(proof->kernel_index, "kernel.resource", 127,
        RTF_SINGLETASK);
    REQUIRE_RESIDENT(proof->exec_index, "exec.library", 120,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->expansion_index, "expansion.library", 110,
        RTF_SINGLETASK);
    REQUIRE_RESIDENT(proof->utility_index, "utility.library", 103,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->aros_index, "aros.library", 101,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->bootloader_index, "bootloader.resource", 100,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->task_index, "task.resource", 100,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->filesystem_index, "FileSystem.resource", 80,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->timer_index, "timer.device", 50,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->partition_index, "partition.library", 40,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->locale_index, "locale.library", 0,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->stdc_index, "stdc.library", 0,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->stdcio_index, "stdcio.library", 0,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->posixc_index, "posixc.library", 0,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->fat_index, "fat-handler", -1, RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->appleboot_index, "appleboot.device", -40,
        RTF_COLDSTART);
    REQUIRE_RESIDENT(proof->dos_index, "dos.library", -120, 0);
    REQUIRE_RESIDENT(proof->iffparse_index, "iffparse.library", -121,
        RTF_AFTERDOS);
    if (WANDERER_PROFILE(state->resident_count)) {
        uint32_t index;
        uint32_t position = 0;
        const uint32_t has_usb =
            platform != NULL &&
            find_resident_index(state, platform->optional_usb_resident) >= 0 ? 1U : 0U;
        const uint32_t has_afs =
            find_resident_index(state, "afsplus-handler") >= 0 ? 1U : 0U;

        const uint32_t has_of =
            find_resident_index(state, "openfirmware.resource") >= 0 ? 1U : 0U;

        /* Optional residents have fixed insertion points, so an extra module
         * cannot weaken the exact order, priority and flags proof. */
        if ((vm_graphics_profile_flag && !vm_pci_profile_flag) ||
            (vm_pci_profile_flag && !has_of) ||
            state->resident_count != DESKTOP_RESIDENT_COUNT +
            (platform != NULL ? platform->resident_count : 0) + has_usb + has_afs +
            has_of + (vm_pci_profile_flag ? 3U : 0U)) {
            proof->detail = UINT32_C(0x3ff);
            return 0;
        }
        for (index = 0; index < DESKTOP_RESIDENT_COUNT +
                (platform != NULL ? platform->resident_count : 0); index++) {
            const struct ColdStartResident *expected = desktop_resident_at(index);
            if (has_usb && name_equal(expected->name,
                    platform->usb_before_resident)) {
                REQUIRE_RESIDENT(position, platform->optional_usb_resident, 99, RTF_COLDSTART);
                position++;
            }
            REQUIRE_RESIDENT(position,
                vm_graphics_profile_flag && name_equal(expected->name, "efifbgfx.hidd") ?
                    "virtiogfx.hidd" : expected->name,
                expected->priority,
                expected->flags);
            position++;
            /* Firmware follows the product's last early resource. */
            if (has_of && name_equal(expected->name, platform != NULL ?
                    platform->firmware_after_resident : "utility.library")) {
                REQUIRE_RESIDENT(position, "openfirmware.resource", 102, RTF_COLDSTART);
                position++;
            }
            if (vm_pci_profile_flag && name_equal(expected->name, "hiddclass.hidd")) {
                REQUIRE_RESIDENT(position, "pci.hidd", 88, RTF_COLDSTART); position++;
                REQUIRE_RESIDENT(position, "pciecam.hidd", 87, RTF_COLDSTART); position++;
                REQUIRE_RESIDENT(position, "vmpci.hidd", 86, RTF_COLDSTART); position++;
            }
            if (has_afs && name_equal(expected->name, "fat-handler")) {
                REQUIRE_RESIDENT(position, "afsplus-handler", -1, RTF_COLDSTART);
                position++;
            }
        }
    } else if (state->resident_count == 25 || state->resident_count == 34) {
        REQUIRE_RESIDENT(9, "oop.library", 94, RTF_COLDSTART);
        REQUIRE_RESIDENT(10, "hiddclass.hidd", 92, RTF_COLDSTART);
        REQUIRE_RESIDENT(12, "gfx.hidd", 66, RTF_COLDSTART);
        REQUIRE_RESIDENT(13, "graphics.library", 65, RTF_COLDSTART);
        REQUIRE_RESIDENT(state->resident_count == 34 ? 25 : 16,
            "efifbgfx.hidd", 9, RTF_COLDSTART);
        if (state->resident_count == 34) {
            REQUIRE_RESIDENT(14, "layers.library", 64, RTF_COLDSTART);
            REQUIRE_RESIDENT(16, "inputclass.hidd", 45, RTF_COLDSTART);
            REQUIRE_RESIDENT(17, "keyboard.hidd", 45, RTF_COLDSTART);
            REQUIRE_RESIDENT(18, "mouse.hidd", 45, RTF_COLDSTART);
            REQUIRE_RESIDENT(19, "keyboard.device", 44, RTF_COLDSTART);
            REQUIRE_RESIDENT(20, "gameport.device", 44, RTF_COLDSTART);
            REQUIRE_RESIDENT(22, "keymap.library", 40, RTF_COLDSTART);
            REQUIRE_RESIDENT(23, "input.device", 30, RTF_COLDSTART);
            REQUIRE_RESIDENT(24, "intuition.library", 15, RTF_COLDSTART);
        }
    }
#undef REQUIRE_RESIDENT

    stack_top = user_stack + (uintptr_t)user_stack_size;
    if ((stack_top & 15U) != 0) {
        proof->detail = 4;
        return 0;
    }
    proof->free_before = free_memory();
    proof->dispatch_count_before = SysBase->DispCount;
    if (proof->free_before == UINT64_MAX) {
        proof->detail = 5;
        return 0;
    }

    active_proof = proof;
    proof->return_status = AROSAppleRuntimeEnterEL0(
        core_coldstart_el0, NULL, stack_top);
    active_proof = NULL;
    return proof->status == 1 &&
        proof->return_status == AROS_APPLE_COLDSTART_STATUS_PASS &&
        AROSAppleRuntimeLocal()->transition_active == 0;
}
