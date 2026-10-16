/*
    Copyright (C) 1995-2011, The AROS Development Team. All rights reserved.
    Copyright (C) 2026 John KNIPPER

    Bootloader information initialisation.
*/

#define DEBUG 0

#include <aros/debug.h>
#include <aros/kernel.h>
#include <aros/multiboot.h>
#include <aros/symbolsets.h>
#include <aros/bootloader.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <utility/utility.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/bootloader.h>
#include <proto/kernel.h>
#include <proto/utility.h>

#include <ctype.h>
#include <string.h>

#include "bootloader_intern.h"

#include LC_LIBDEFS_FILE

static void GetCmdLine(char *Kernel_Args, struct BootLoaderBase *BootLoaderBase)
{
    STRPTR buff;
    ULONG len;

    /* Sanity check against broken bootstraps */
    if (!Kernel_Args)
        return;

    D(bug("[BootLdr] Kernel arguments: %s\n", Kernel_Args));

    /* First make a copy of the command line */
    len = strlen(Kernel_Args) + 1;
    buff = AllocMem(len, MEMF_ANY);
    if (buff)
    {
        CopyMem(Kernel_Args, buff, len);

        while (1)
        {
            struct Node *node;

            /* remove any leading spaces */
            buff = stpblk(buff);

            /* Hit end of line (trailing spaces) ? Exit if so. */
            if (!*buff)
                break;

            /* Allocate node and insert into list */
            node = AllocMem(sizeof(struct Node),MEMF_ANY);
            if (!node)
                break;

            node->ln_Name = buff;
            AddTail(&(BootLoaderBase->Args), node);

            /* We now have the command line */
            BootLoaderBase->Flags |= BL_FLAGS_CMDLINE;

            /* Skip up to a next space or EOL */
            while (*buff != '\0' && !isspace(*buff))
                buff++;

            /* End of line ? If yes, we are done. */
            if (!*buff)
            {
                D(bug("[BootLdr] Init: Last argument %s\n", node->ln_Name));
                break;
            }

            /* Split the line and repeat */
            *buff++ = 0;
            D(bug("[BootLdr] Init: Argument %s\n", node->ln_Name));
        }
    }
}

static ULONG mask_for_bits(UBYTE bits)
{
    if (bits == 0)
        return 0;
    if (bits >= 32)
        return 0xFFFFFFFFUL;
    return ((1UL << bits) - 1UL);
}

static int GM_UNIQUENAME(Init)(LIBBASETYPEPTR BootLoaderBase)
{
    struct TagItem *tag, *bootinfo;
    APTR KernelBase;
    struct Library *UtilityBase;
    struct vbe_mode *vmi = NULL;
    struct vbe_controller *vci = NULL;
    UWORD vmode = 0x00FF;       /* Dummy mode number by default           */
    UBYTE palette = 0;          /* By default we don't know palette width */
    IPTR fb_addr64 = 0;         /* Full 64-bit framebuffer address from KRN_FBAddr */
    IPTR fb_width = 0;
    IPTR fb_height = 0;
    IPTR fb_depth = 0;
    IPTR fb_pitch = 0;
    IPTR fb_format = KRN_FB_FORMAT_NONE;
    IPTR fb_size = 0;

    D(bug("[BootLdr] Init\n"));

    UtilityBase = TaggedOpenLibrary(TAGGEDOPEN_UTILITY);
    if (!UtilityBase)
        return FALSE;

    NEWLIST(&(BootLoaderBase->Args));
    NEWLIST(&(BootLoaderBase->DriveInfo));

    KernelBase = OpenResource("kernel.resource");
    bootinfo = (struct TagItem *)KrnGetBootInfo();
    
    while ((tag = NextTagItem(&bootinfo)))
    {
        switch (tag->ti_Tag)
        {
        case KRN_BootLoader:
            BootLoaderBase->LdrName = (STRPTR)tag->ti_Data;
            break;

        case KRN_CmdLine:
            GetCmdLine((char *)tag->ti_Data, BootLoaderBase);
            break;

        case KRN_VBEModeInfo:
            vmi = (struct vbe_mode *)tag->ti_Data;
            break;

        case KRN_VBEControllerInfo:
            vci = (struct vbe_controller *)tag->ti_Data;
            break;

        case KRN_VBEMode:
            vmode = tag->ti_Data;
            break;

        case KRN_VBEPaletteWidth:
            palette = tag->ti_Data;
            break;

        case KRN_FBAddr:
            fb_addr64 = tag->ti_Data;
            break;

        case KRN_FrameBufferWidth:
            fb_width = tag->ti_Data;
            break;

        case KRN_FrameBufferHeight:
            fb_height = tag->ti_Data;
            break;

        case KRN_FrameBufferDepth:
            fb_depth = tag->ti_Data;
            break;

        case KRN_FrameBufferPitch:
            fb_pitch = tag->ti_Data;
            break;

        case KRN_FrameBufferFormat:
            fb_format = tag->ti_Data;
            break;

        case KRN_FrameBufferSize:
            fb_size = tag->ti_Data;
            break;
        }
    }

    /* Get VESA mode information */
    D(bug("[BootLdr] VESA mode info 0x%p, controller info 0x%p\n", vmi, vci));

    /* We support only graphical framebuffers here. */
    if (vmi && (vmi->mode_attributes & VM_GRAPHICS))
    {
        BootLoaderBase->Vesa = AllocMem(sizeof(struct VesaInfo), MEMF_CLEAR);

        if (BootLoaderBase->Vesa)
        {
            /* If we don't have VBEControllerInfo, this is VBE v2 mode structure */
            unsigned short version = 0x0200;

            if (vci)
            {
                D(bug("[BootLdr] Init: Vesa card capability flags: 0x%08X\n", vci->capabilities));

                version = vci->version;
                /* FrameBufferSize is in KBytes! */
                BootLoaderBase->Vesa->FrameBufferSize = vci->total_memory << 6;
            }

            BootLoaderBase->Vesa->XSize            = vmi->x_resolution;
            BootLoaderBase->Vesa->YSize            = vmi->y_resolution;
            BootLoaderBase->Vesa->BitsPerPixel     = vmi->bits_per_pixel;
            BootLoaderBase->Vesa->ModeNumber       = vmode;
            BootLoaderBase->Vesa->PaletteWidth     = palette;

            /* Framebuffer pointer is only valid for VBE v2 and better */
            if (version >= 0x0200)
                BootLoaderBase->Vesa->FrameBuffer = (APTR)(IPTR)vmi->phys_base;

            /* Override with full 64-bit address if bootstrap provided KRN_FBAddr.
             * VBEModeInfo.phys_base is 32-bit and truncates on >4GB systems. */
            if (fb_addr64)
                BootLoaderBase->Vesa->FrameBuffer = (APTR)fb_addr64;

            if (version >= 0x0300)
            {
                /* VBE v3 structures specify linear framebuffer parameters in separate fields */
                BootLoaderBase->Vesa->BytesPerLine     = vmi->linear_bytes_per_scanline;
                BootLoaderBase->Vesa->Masks [VI_Red]   = mask_for_bits(vmi->linear_red_mask_size) << vmi->linear_red_field_position;
                BootLoaderBase->Vesa->Masks [VI_Blue]  = mask_for_bits(vmi->linear_blue_mask_size) << vmi->linear_blue_field_position;
                BootLoaderBase->Vesa->Masks [VI_Green] = mask_for_bits(vmi->linear_green_mask_size) << vmi->linear_green_field_position;
                BootLoaderBase->Vesa->Masks [VI_Alpha] = mask_for_bits(vmi->linear_reserved_mask_size) << vmi->linear_reserved_field_position;
                BootLoaderBase->Vesa->Shifts[VI_Red]   = 32 - vmi->linear_red_field_position - vmi->linear_red_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Blue]  = 32 - vmi->linear_blue_field_position - vmi->linear_blue_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Green] = 32 - vmi->linear_green_field_position - vmi->linear_green_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Alpha] = 32 - vmi->linear_reserved_field_position - vmi->linear_reserved_mask_size;
            }
            else
            {
                BootLoaderBase->Vesa->BytesPerLine     = vmi->bytes_per_scanline;
                BootLoaderBase->Vesa->Masks [VI_Red]   = mask_for_bits(vmi->red_mask_size) << vmi->red_field_position;
                BootLoaderBase->Vesa->Masks [VI_Blue]  = mask_for_bits(vmi->blue_mask_size) << vmi->blue_field_position;
                BootLoaderBase->Vesa->Masks [VI_Green] = mask_for_bits(vmi->green_mask_size) << vmi->green_field_position;
                BootLoaderBase->Vesa->Masks [VI_Alpha] = mask_for_bits(vmi->reserved_mask_size) << vmi->reserved_field_position;
                BootLoaderBase->Vesa->Shifts[VI_Red]   = 32 - vmi->red_field_position - vmi->red_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Blue]  = 32 - vmi->blue_field_position - vmi->blue_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Green] = 32 - vmi->green_field_position - vmi->green_mask_size;
                BootLoaderBase->Vesa->Shifts[VI_Alpha] = 32 - vmi->reserved_field_position - vmi->reserved_mask_size;
            }

            D(bug("[BootLdr] Init: Vesa mode %x type (%dx%dx%d)\n", BootLoaderBase->Vesa->ModeNumber,
                  BootLoaderBase->Vesa->XSize, BootLoaderBase->Vesa->YSize, BootLoaderBase->Vesa->BitsPerPixel));
            D(bug("[BootLdr] Init: Vesa FB at 0x%p size %d kB\n", BootLoaderBase->Vesa->FrameBuffer,
                  BootLoaderBase->Vesa->FrameBufferSize));
            D(bug("[BootLdr] Init: Vesa mode palette width: %d\n", BootLoaderBase->Vesa->PaletteWidth));
            D(bug("[BootLdr] Init: Vesa mode direct color flags %02X\n", vmi->direct_color_mode_info));
        }
    }
    else if (fb_addr64 && fb_width && fb_height &&
             fb_pitch && fb_width <= fb_pitch / 4 &&
             fb_height <= fb_size / fb_pitch &&
             fb_width <= (IPTR)(ULONG)~0UL &&
             fb_height <= (IPTR)(ULONG)~0UL &&
             fb_pitch <= (IPTR)(ULONG)~0UL &&
             ((fb_depth == 32 &&
               (fb_format == KRN_FB_FORMAT_RGBX8888 ||
                fb_format == KRN_FB_FORMAT_BGRX8888)) ||
              (fb_depth == 30 &&
               fb_format == KRN_FB_FORMAT_X2R10G10B10)))
    {
        ULONG size_kib;

        BootLoaderBase->Vesa = AllocMem(sizeof(struct VesaInfo), MEMF_CLEAR);
        if (BootLoaderBase->Vesa)
        {
            IPTR kib = fb_size / 1024 + ((fb_size % 1024) != 0);

            size_kib = kib > (IPTR)(ULONG)~0UL ? (ULONG)~0UL : (ULONG)kib;
            BootLoaderBase->Vesa->FrameBuffer = (APTR)fb_addr64;
            BootLoaderBase->Vesa->FrameBufferSize = size_kib;
            BootLoaderBase->Vesa->XSize = (ULONG)fb_width;
            BootLoaderBase->Vesa->YSize = (ULONG)fb_height;
            BootLoaderBase->Vesa->BytesPerLine = (ULONG)fb_pitch;
            BootLoaderBase->Vesa->BitsPerPixel = (UBYTE)fb_depth;
            BootLoaderBase->Vesa->ModeNumber = vmode;
            BootLoaderBase->Vesa->PaletteWidth = palette;
            /* Shifts follow the VBE v3 convention this file already uses for
               a real mode structure: 32 - field position - field size. */
            if (fb_format == KRN_FB_FORMAT_X2R10G10B10)
            {
                BootLoaderBase->Vesa->Masks[VI_Red] = 0x3ff00000UL;
                BootLoaderBase->Vesa->Shifts[VI_Red] = 2;
                BootLoaderBase->Vesa->Masks[VI_Green] = 0x000ffc00UL;
                BootLoaderBase->Vesa->Shifts[VI_Green] = 12;
                BootLoaderBase->Vesa->Masks[VI_Blue] = 0x000003ffUL;
                BootLoaderBase->Vesa->Shifts[VI_Blue] = 22;
                BootLoaderBase->Vesa->Masks[VI_Alpha] = 0xc0000000UL;
                BootLoaderBase->Vesa->Shifts[VI_Alpha] = 0;
            }
            else
            {
                BootLoaderBase->Vesa->Masks[VI_Green] = 0x0000ff00UL;
                BootLoaderBase->Vesa->Shifts[VI_Green] = 16;
                BootLoaderBase->Vesa->Masks[VI_Alpha] = 0xff000000UL;
                BootLoaderBase->Vesa->Shifts[VI_Alpha] = 0;
                if (fb_format == KRN_FB_FORMAT_RGBX8888)
                {
                    BootLoaderBase->Vesa->Masks[VI_Red] = 0x000000ffUL;
                    BootLoaderBase->Vesa->Shifts[VI_Red] = 24;
                    BootLoaderBase->Vesa->Masks[VI_Blue] = 0x00ff0000UL;
                    BootLoaderBase->Vesa->Shifts[VI_Blue] = 8;
                }
                else
                {
                    BootLoaderBase->Vesa->Masks[VI_Red] = 0x00ff0000UL;
                    BootLoaderBase->Vesa->Shifts[VI_Red] = 8;
                    BootLoaderBase->Vesa->Masks[VI_Blue] = 0x000000ffUL;
                    BootLoaderBase->Vesa->Shifts[VI_Blue] = 24;
                }
            }
        }
    }

    CloseLibrary(UtilityBase);

    return TRUE;
}

ADD2INITLIB(GM_UNIQUENAME(Init), 0)
