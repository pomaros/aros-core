/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include <stddef.h>
#include <exec/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/keymap.h>

static void wipe(void *memory, size_t count)
{
    volatile unsigned char *p = memory;
    while (count--) *p++ = 0;
}

/* No IDCMP password data, clipboard, ARexx, environment or command-line input.
 * input.device captures keyboard completions before the commodity chain.
 * This is containment: a task that writes shared memory can bypass it. */
int kr_prompt_passphrase(unsigned char *buffer, size_t capacity, size_t *length)
{
    struct Library *IntuitionBase = NULL, *GfxBase = NULL, *KeymapBase = NULL;
    struct MsgPort *port = NULL;
    struct IOStdReq *request = NULL;
    struct Window *window = NULL;
    struct InputEvent event;
    char translated[16];
    size_t used = 0;
    unsigned ticks;
    int opened = 0, captured = 0, result = -1;
    if (!buffer || !length || capacity < 2) return -1;
    *length = 0;
    wipe(buffer, capacity);
    wipe(&event, sizeof(event));
    wipe(translated, sizeof(translated));
    IntuitionBase = OpenLibrary("intuition.library", 39);
    GfxBase = OpenLibrary("graphics.library", 39);
    KeymapBase = OpenLibrary("keymap.library", 0);
    port = CreateMsgPort();
    if (!IntuitionBase || !GfxBase || !KeymapBase || !port) goto done;
    request = (struct IOStdReq *)CreateIORequest(port, sizeof(*request));
    if (!request || OpenDevice("input.device", 0, (struct IORequest *)request, 0)) goto done;
    opened = 1;
    request->io_Command = IND_SECRETBEGIN;
    if (DoIO((struct IORequest *)request)) goto done;
    captured = 1;
    window = OpenWindowTags(NULL,
        WA_Title, (IPTR)"Unlock keyring (containment)",
        WA_Width, 480, WA_Height, 116,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_DepthGadget, FALSE,
        WA_CloseGadget, FALSE, WA_IDCMP, 0,
        WA_Flags, WFLG_SMART_REFRESH | WFLG_RMBTRAP,
        TAG_DONE);
    if (!window) goto done;
    SetAPen(window->RPort, 1);
    Move(window->RPort, 14, window->BorderTop + 22);
    Text(window->RPort, "Type passphrase on the physical keyboard.",
        sizeof("Type passphrase on the physical keyboard.") - 1);
    Move(window->RPort, 14, window->BorderTop + 42);
    Text(window->RPort, "Enter unlocks; Esc cancels. Input is hidden.",
        sizeof("Enter unlocks; Esc cancels. Input is hidden.") - 1);
    {
        const char *status = "[KEYRING] physical-prompt-ready=1\n";
        while (*status) RawPutChar(*status++);
    }
    for (ticks = 0; ticks < 6000; ticks++)
    {
        LONG count;
        request->io_Command = IND_SECRETREAD;
        request->io_Data = &event;
        request->io_Length = sizeof(event);
        if (DoIO((struct IORequest *)request)) goto done;
        if (request->io_Actual)
        {
            if (event.ie_Class != IECLASS_RAWKEY || (event.ie_Code & IECODE_UP_PREFIX))
                goto next;
            if (event.ie_Code == 0x45) goto done; /* Escape */
            if (event.ie_Code == 0x44 || event.ie_Code == 0x43) /* Return/Enter */
            {
                if (!used) goto done;
                *length = used;
                result = 0;
                goto done;
            }
            if (event.ie_Code == 0x41) /* Backspace */
            {
                if (used) buffer[--used] = 0;
                goto next;
            }
            if (event.ie_Qualifier & (IEQUALIFIER_CONTROL | IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
                goto next;
            count = MapRawKey(&event, translated, sizeof(translated), NULL);
            if (count < 0 || (size_t)count > sizeof(translated)) goto done;
            if ((size_t)count > capacity - 1 - used) goto done;
            if (count)
            {
                LONG i;
                for (i = 0; i < count; i++)
                    if ((unsigned char)translated[i] < 32 || translated[i] == 127) goto done;
                CopyMem(translated, buffer + used, count);
                used += count;
            }
        }
next:
        wipe(&event, sizeof(event));
        wipe(translated, sizeof(translated));
        Delay(1);
    }
done:
    wipe(&event, sizeof(event));
    wipe(translated, sizeof(translated));
    if (window) CloseWindow(window);
    if (captured)
    {
        request->io_Command = IND_SECRETEND;
        request->io_Data = NULL;
        request->io_Length = 0;
        if (DoIO((struct IORequest *)request)) result = -1;
    }
    if (opened) CloseDevice((struct IORequest *)request);
    if (request) DeleteIORequest((struct IORequest *)request);
    if (port) DeleteMsgPort(port);
    if (KeymapBase) CloseLibrary(KeymapBase);
    if (GfxBase) CloseLibrary(GfxBase);
    if (IntuitionBase) CloseLibrary(IntuitionBase);
    if (result) { wipe(buffer, capacity); *length = 0; }
    return result;
}
