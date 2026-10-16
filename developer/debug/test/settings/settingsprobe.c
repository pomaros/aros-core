/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    SETPROBE: the [SET10] round trip in the native guest. Started by the
    cold-start proof when SETTEST.REQ is on SYS:, it copies the shipped
    font.prefs to ENV:SYS/, opens settings.library from LIBS: (the bundle
    has no library loader, so it initialises the resident itself), and
    exercises GetSettingA, SetSettingA, DescribeSettingA and EnumSettingsA.
    Returns 75 on success, a distinct code for each failing step.
*/
#include <stdint.h>
#include <string.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <workbench/startup.h>
#include <libraries/iffparse.h>
#include <libraries/settings.h>
#include <proto/settings.h>
#include "fontsettings.h"
#include <aros/debug.h>

struct Library *SettingsBase;

/* A CLI command: defining WBenchMsg keeps the Workbench startup object out
 * of the link, which would otherwise wait for a WBStartup message that the
 * cold-start proof never sends. */
int __nowbsupport = 1;
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

/* Every step and its DOS error go to the kernel debug console, which the
 * QEMU run keeps in semihost.log; the probe's own output goes to NIL:. */
#define TRACE(step) kprintf("[SETPROBE] %s ioerr=%ld\n", step, (long)IoErr())

static struct Library *init_from_libs(void)
{
    BPTR seglist = LoadSeg("LIBS:settings.library");
    BPTR seg;
    const int resident_size = offsetof(struct Resident, rt_Init) + sizeof(APTR);

    if (seglist == BNULL)
        return NULL;
    for (seg = seglist; seg != BNULL; seg = *(BPTR *)BADDR(seg)) {
        STRPTR addr = (STRPTR)((IPTR)BADDR(seg) - sizeof(ULONG));
        ULONG size = *(ULONG *)addr;
        for (addr += sizeof(BPTR) + sizeof(ULONG), size -= sizeof(BPTR) + sizeof(ULONG);
             size >= (ULONG)resident_size; size -= 2, addr += 2) {
            struct Resident *res = (struct Resident *)addr;
            if (res->rt_MatchWord == RTC_MATCHWORD && res->rt_MatchTag == res) {
                struct Node *node;
                Forbid();
                InitResident(res, seglist);
                node = FindName(&SysBase->LibList, res->rt_Name);
                Permit();
                return (struct Library *)node;
            }
        }
    }
    return NULL;
}

/* 0 on success; 1: source absent, 2: destination refused, 3: short write. */
static int copy_file(CONST_STRPTR from, CONST_STRPTR to)
{
    static UBYTE buffer[4096];
    BPTR in = Open(from, MODE_OLDFILE), out;
    LONG n;
    if (in == BNULL)
        return 1;
    out = Open(to, MODE_NEWFILE);
    if (out == BNULL) {
        Close(in);
        return 2;
    }
    while ((n = Read(in, buffer, sizeof(buffer))) > 0)
        if (Write(out, buffer, n) != n)
            n = -1;
    Close(out);
    Close(in);
    return n == 0 ? 0 : 3;
}

static ULONG enumerated;

AROS_UFH3(IPTR, count_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, message, A1))
{
    AROS_USERFUNC_INIT
    struct SettingDescription *description = object;
    (void)hook; (void)message;
    if (description->sd_Size == sizeof(*description) &&
        strncmp((const char *)description->sd_Key, "font/", 5) == 0)
        enumerated++;
    return 0;
    AROS_USERFUNC_EXIT
}

/* ReplaceSettingsA hooks: h_Data selects the behaviour. 0 edits the
 * system FONT chunk's size to 10 in place; 1 cancels; 2 shrinks a FONT
 * chunk so the library must refuse the result. */
AROS_UFH3(IPTR, replace_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, message, A1))
{
    AROS_USERFUNC_INIT
    struct SettingReplaceMessage *replace = object;
    IPTR mode = (IPTR)hook->h_Data;
    ULONG i;
    (void)message;
    if (mode == 1)
        return 0;
    for (i = 0; i < replace->srm_Count; i++) {
        struct SettingChunk *chunk = &replace->srm_Chunks[i];
        UBYTE *data = chunk->sc_Data;
        if (chunk->sc_ID != MAKE_ID('F', 'O', 'N', 'T') || chunk->sc_Size != 156)
            continue;
        if (mode == 2) {
            chunk->sc_Size = 155;
            return 1;
        }
        if (data[14] == 0 && data[15] == 1) {   /* fp_Type == 1: system */
            data[24] = 0;
            data[25] = 10;
        }
    }
    return 1;
    AROS_USERFUNC_EXIT
}

static int read_env_size_field(void)
{
    static UBYTE image[1024];
    BPTR file = Open("ENV:SYS/font.prefs", MODE_OLDFILE);
    LONG length;
    LONG position = 12;
    if (file == BNULL)
        return -1;
    length = Read(file, image, sizeof(image));
    Close(file);
    if (length != 518 || memcmp(image, "FORM", 4) != 0 || memcmp(image + 8, "PREF", 4) != 0)
        return -2;
    while (position + 8 <= length) {
        ULONG size = ((ULONG)image[position + 4] << 24) | ((ULONG)image[position + 5] << 16) |
                     ((ULONG)image[position + 6] << 8) | image[position + 7];
        if (memcmp(image + position, "FONT", 4) == 0 && size == 156 &&
            image[position + 8 + 14] == 0 && image[position + 8 + 15] == 1)
            return (image[position + 8 + 24] << 8) | image[position + 8 + 25];
        position += 8 + size + (size & 1);
    }
    return -3;
}

/* The boot image is read-only and the bundle has no RAM:, so the probe
 * mounts one from the ram-handler the image carries in L:, the way Mount
 * would: a device node whose handler name DOS loads on first access. The
 * target stores BSTRs as C strings (AROS_FAST_BSTR). */
/* 0 when RAM: is usable; 71 node allocation, 72 AddDosEntry, 73 the
 * handler did not start (the lock after mounting failed). */
static int mount_ram(void)
{
    struct DeviceNode *node;
    BPTR lock = Lock("RAM:", SHARED_LOCK);
    if (lock != BNULL) {
        UnLock(lock);
        return 0;
    }
    TRACE("RAM: absent, mounting");
    node = (struct DeviceNode *)MakeDosEntry("RAM", DLT_DEVICE);
    if (node == NULL) {
        TRACE("MakeDosEntry failed");
        return 71;
    }
    node->dn_Handler = (BSTR)MKBADDR((APTR)"L:ram-handler");
    node->dn_StackSize = 65536;
    node->dn_Priority = 5;
    node->dn_GlobalVec = (BPTR)(SIPTR)-1;
    node->dn_Startup = BNULL;
    node->dn_SegList = BNULL;
    if (!AddDosEntry((struct DosList *)node)) {
        TRACE("AddDosEntry failed");
        FreeDosEntry((struct DosList *)node);
        return 72;
    }
    lock = Lock("RAM:", SHARED_LOCK);
    if (lock == BNULL) {
        TRACE("Lock RAM: failed after mount");
        return 73;
    }
    UnLock(lock);
    TRACE("RAM: mounted");
    return 0;
}

/* The guest has no Intuition, so a missing volume must fail instead of
 * raising a requester. ENV: is moved to RAM:Env. 0 on success, else the
 * mount code or 61 (home directory), 62 (assign), 63 (ENV:SYS). */
static int ensure_env(void)
{
    BPTR lock;
    int mounted;

    /* The guest's DOS takeover already assigns ENV: onto the read-only boot
     * image; the probe needs a writable one, so it is replaced. */
    mounted = mount_ram();
    if (mounted != 0)
        return mounted;
    lock = CreateDir("RAM:Env");
    TRACE(lock != BNULL ? "CreateDir RAM:Env ok" : "CreateDir RAM:Env failed");
    if (lock != BNULL)
        UnLock(lock);
    lock = Lock("RAM:Env", SHARED_LOCK);
    if (lock == BNULL) {
        TRACE("Lock RAM:Env failed");
        return 61;
    }
    if (!AssignLock("ENV", lock)) {
        TRACE("AssignLock ENV failed");
        return 62;
    }
    TRACE("ENV: assigned to RAM:Env");
    lock = CreateDir("ENV:SYS");
    if (lock == BNULL && IoErr() != ERROR_OBJECT_EXISTS) {
        TRACE("CreateDir ENV:SYS failed");
        return 63;
    }
    if (lock != BNULL)
        UnLock(lock);
    TRACE("ENV:SYS ready");
    return 0;
}

int main(void)
{
    char value[64];
    struct SettingDescription description;
    struct Hook hook;
    BPTR lock;
    struct Process *self = (struct Process *)FindTask(NULL);

    self->pr_WindowPtr = (APTR)-1;
    {
        int env = ensure_env();
        if (env != 0)
            return env;
    }
    {
        int copied = copy_file("SYS:Prefs/Env-Archive/SYS/font.prefs", "ENV:SYS/font.prefs");
        if (copied != 0) {
            kprintf("[SETPROBE] copy failed code=%ld ioerr=%ld\n", (long)copied, (long)IoErr());
            return 90 + copied;
        }
        TRACE("font.prefs copied to ENV:SYS");
    }

    SettingsBase = OpenLibrary("settings.library", 1);
    if (SettingsBase == NULL)
        SettingsBase = init_from_libs();
    if (SettingsBase == NULL) {
        TRACE("settings.library did not open");
        return 10;
    }
    TRACE("settings.library open");

    if (GetSettingA("font/system/size", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "14") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)11, (long)IoErr()); return 11; }
    if (GetSettingA("font/system/name", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "ttcourier.font") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)12, (long)IoErr()); return 12; }
    if (SetSettingA("font/system/size", "11", NULL) != SETTING_OK)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)13, (long)IoErr()); return 13; }
    if (GetSettingA("font/system/size", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "11") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)14, (long)IoErr()); return 14; }
    if (SetSettingA("font/system/size", "99", NULL) != SETTING_ERROR_RANGE)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)15, (long)IoErr()); return 15; }
    if (GetSettingA("font/system/size", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "11") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)16, (long)IoErr()); return 16; }
    if (SetSettingA("font/workbench/style", "bold,italic", NULL) != SETTING_OK ||
        GetSettingA("font/workbench/style", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "bold,italic") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)17, (long)IoErr()); return 17; }
    if (GetSettingA("font/system/colour", value, sizeof(value), NULL) != SETTING_ERROR_UNKNOWN)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)18, (long)IoErr()); return 18; }
    if (GetSettingA("nosuch/x/y", value, sizeof(value), NULL) != SETTING_ERROR_NO_SCHEMA)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)19, (long)IoErr()); return 19; }
    if (DescribeSettingA("font/system/size", &description, NULL) != SETTING_OK ||
        description.sd_Type != SETTING_TYPE_INT || description.sd_Min != 4 ||
        description.sd_Max != 64 || description.sd_Level != SETTING_LEVEL_AVERAGE ||
        description.sd_Unit == NULL || strcmp((const char *)description.sd_Unit, "points") != 0)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)20, (long)IoErr()); return 20; }
    hook.h_Entry = (HOOKFUNC)count_hook;
    hook.h_SubEntry = NULL;
    hook.h_Data = NULL;
    enumerated = 0;
    if (EnumSettingsA("font/", &hook, NULL) != SETTING_OK || enumerated != 12)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)21, (long)IoErr()); return 21; }
    if (read_env_size_field() != 11)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)22, (long)IoErr()); return 22; }
    lock = Lock("ENV:SYS/font.prefs.new", SHARED_LOCK);
    if (lock != BNULL) {
        UnLock(lock);
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)23, (long)IoErr()); return 23; }
    }
    if (NotifySettingA("font/", NULL, NULL) != SETTING_ERROR_RANGE)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)24, (long)IoErr()); return 24; }
    /* Whole-file rewrite through the hook: edit, cancel, refused result. */
    hook.h_Entry = (HOOKFUNC)replace_hook;
    hook.h_Data = (APTR)0;
    if (ReplaceSettingsA("font", &hook, NULL) != SETTING_OK ||
        GetSettingA("font/system/size", value, sizeof(value), NULL) != SETTING_OK ||
        strcmp(value, "10") != 0 || read_env_size_field() != 10)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)40, (long)IoErr()); return 40; }
    hook.h_Data = (APTR)1;
    if (ReplaceSettingsA("font/", &hook, NULL) != SETTING_ERROR_CANCELLED || read_env_size_field() != 10)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)41, (long)IoErr()); return 41; }
    hook.h_Data = (APTR)2;
    if (ReplaceSettingsA("font", &hook, NULL) != SETTING_ERROR_FORMAT || read_env_size_field() != 10)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)42, (long)IoErr()); return 42; }
    if (ReplaceSettingsA("nosuch", &hook, NULL) != SETTING_ERROR_NO_SCHEMA)
        { kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)43, (long)IoErr()); return 43; }
    TRACE("replace: edit, cancel, refused and unknown stem");
    {
        /* The Font editor's store (workbench/prefs/font/fontsettings.c):
         * write, read back, a first write with no file, and a first
         * write whose directory is missing. */
        static struct FontPrefs written[FONTSETTINGS_COUNT], read[FONTSETTINGS_COUNT];
        static const char *const names[FONTSETTINGS_COUNT] = { "arial.font", "ttcourier.font", "dejavusans.font" };
        static const UWORD sizes[FONTSETTINGS_COUNT] = { 13, 9, 15 };
        ULONG source = 99, i;
        int step = 0;

        for (i = 0; i < FONTSETTINGS_COUNT; i++) {
            memset(&written[i], 0, sizeof(written[i]));
            written[i].fp_Type = (UWORD)i;
            written[i].fp_FrontPen = (UBYTE)(i + 1);
            written[i].fp_TextAttr.ta_YSize = sizes[i];
            written[i].fp_TextAttr.ta_Flags = FPF_DISKFONT;
            strcpy((char *)written[i].fp_Name, names[i]);
            written[i].fp_TextAttr.ta_Name = (STRPTR)written[i].fp_Name;
        }
        if (FontSettings_Store(written, SETTING_LAYER_APPLIED) != SETTING_OK ||
            GetSettingA("font/system/size", value, sizeof(value), NULL) != SETTING_OK ||
            strcmp(value, "9") != 0 || read_env_size_field() != 9)
            step = 44;
        else if (FontSettings_Load(read, SETTING_LAYER_APPLIED, &source) != SETTING_OK ||
                 source != SETTING_LAYER_APPLIED)
            step = 45;
        else {
            for (i = 0; i < FONTSETTINGS_COUNT && step == 0; i++)
                if (strcmp((const char *)read[i].fp_Name, names[i]) != 0 ||
                    read[i].fp_TextAttr.ta_YSize != sizes[i] ||
                    read[i].fp_FrontPen != i + 1 || read[i].fp_Type != i ||
                    read[i].fp_TextAttr.ta_Name != (STRPTR)read[i].fp_Name)
                    step = 45;
        }
        if (step == 0) {
            if (!DeleteFile("ENV:SYS/font.prefs") ||
                FontSettings_Load(read, SETTING_LAYER_APPLIED, NULL) != SETTING_ERROR_NO_FILE ||
                FontSettings_Store(written, SETTING_LAYER_APPLIED) != SETTING_OK ||
                read_env_size_field() != 9)
                step = 46;
        }
        if (step == 0) {
            if (!DeleteFile("ENV:SYS/font.prefs") || !DeleteFile("ENV:SYS") ||
                FontSettings_Store(written, SETTING_LAYER_APPLIED) != SETTING_OK ||
                read_env_size_field() != 9 ||
                GetSettingA("font/screen/name", value, sizeof(value), NULL) != SETTING_OK ||
                strcmp(value, "dejavusans.font") != 0)
                step = 47;
        }
        if (step != 0) {
            kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)step, (long)IoErr());
            return step;
        }
    }
    TRACE("font editor store: write, read back, no file, no directory");
    {
        /* Zune's store: items chunk MUIC in ENV:zune/global.prefs, written
         * here the way muimaster's Configdata writes it. */
        static const UBYTE zune_prefs[] = {
            'F','O','R','M', 0,0,0,38, 'P','R','E','F',
            'M','U','I','C', 0,0,0,26,
            0,0,0,0x01, 0,0,0,4, 0,0,0,5,
            0,0,0,0x1e, 0,0,0,6, 'X','E','N','/','8',0
        };
        BPTR dir = CreateDir("ENV:zune"), file;
        int step = 0;

        if (dir != BNULL)
            UnLock(dir);
        file = Open("ENV:zune/global.prefs", MODE_NEWFILE);
        if (file == BNULL || Write(file, (APTR)zune_prefs, sizeof(zune_prefs)) != (LONG)sizeof(zune_prefs))
            step = 48;
        if (file != BNULL)
            Close(file);
        if (step == 0) {
            static const char *const keys[3] = { "zune/window/spacing-left", "zune/fonts/normal",
                "zune/window/spacing-top" };
            static const char *const want[3] = { "5", "XEN/8", "3" };
            ULONG k;
            for (k = 0; k < 3 && step == 0; k++) {
                LONG code;
                value[0] = '\0';
                code = GetSettingA((CONST_STRPTR)keys[k], value, sizeof(value), NULL);
                if (code != SETTING_OK || strcmp(value, want[k]) != 0) {
                    kprintf("[SETPROBE] zune get %s code=%ld value='%s'\n", keys[k], (long)code, value);
                    step = 49;
                }
            }
        }
        if (step == 0 &&
            (SetSettingA("zune/fonts/normal", "DejaVuSans/13", NULL) != SETTING_OK ||
             GetSettingA("zune/fonts/normal", value, sizeof(value), NULL) != SETTING_OK ||
             strcmp(value, "DejaVuSans/13") != 0 ||
             GetSettingA("zune/window/spacing-left", value, sizeof(value), NULL) != SETTING_OK ||
             strcmp(value, "5") != 0))
            step = 50;
        if (step == 0 &&
            (SetSettingA("zune/listview/font-leading", "2", NULL) != SETTING_OK ||
             GetSettingA("zune/listview/font-leading", value, sizeof(value), NULL) != SETTING_OK ||
             strcmp(value, "2") != 0 ||
             SetSettingA("zune/window/spacing-left", "99", NULL) != SETTING_ERROR_RANGE))
            step = 51;
        if (step != 0) {
            kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)step, (long)IoErr());
            return step;
        }
    }
    TRACE("zune store: items read, default, text grown, item appended, range");
    {
        /* Per-setting notification: one message for the key that changed,
         * none for an unrelated prefix, none after unsubscribing. */
        struct MsgPort *font_port = CreateMsgPort();
        struct MsgPort *palette_port = CreateMsgPort();
        struct TagItem unsubscribe[] = { { SETTING_Unsubscribe, TRUE }, { TAG_DONE, 0 } };
        struct SettingNotifyMessage *message;
        int step = 0, received = 0;

        TRACE("notify: ports created");
        /* The guest delivers no timer interrupts, so nothing here waits on
         * time: the expected message is awaited on its port, and "nothing
         * arrives" is proven by order, since one watcher process handles the
         * file's notifications in sequence. */
        if (font_port == NULL || palette_port == NULL)
            step = 25;
        else if (NotifySettingA("font/system/size", font_port, NULL) != SETTING_OK)
            step = 26;
        else if (NotifySettingA("palette/", palette_port, NULL) != SETTING_OK)
            step = 27;
        else if (NotifySettingA("font/nothing/", font_port, NULL) != SETTING_ERROR_UNKNOWN)
            step = 28;
        else if (SetSettingA("font/system/size", "12", NULL) != SETTING_OK)
            step = 29;
        if (step == 0) {
            TRACE("notify: size set to 12, waiting on the port");
            WaitPort(font_port);
            while ((message = (struct SettingNotifyMessage *)GetMsg(font_port)) != NULL) {
                if (strcmp(message->snm_Key, "font/system/size") == 0 &&
                    message->snm_Layer == SETTING_LAYER_APPLIED)
                    received++;
                else
                    received = -100;
                ReplyMsg(&message->snm_Message);
            }
            kprintf("[SETPROBE] notify: messages after the first write=%ld\n", (long)received);
            /* The replace is a delete and a rename on RAM:, two notifications
             * for one value change; the second finds nothing changed. */
            if (received != 1)
                step = 30;
            else if (NotifySettingA("font/system/size", font_port, unsubscribe) != SETTING_OK)
                step = 32;
            else if (SetSettingA("font/system/size", "13", NULL) != SETTING_OK)
                step = 33;
            else if (NotifySettingA("font/system/size", font_port, NULL) != SETTING_OK)
                step = 36;
            else if (SetSettingA("font/system/size", "14", NULL) != SETTING_OK)
                step = 37;
            else {
                /* One message for 14; a message for 13 would mean the
                 * unsubscribe did not take effect, and it would precede it. */
                received = 0;
                WaitPort(font_port);
                while ((message = (struct SettingNotifyMessage *)GetMsg(font_port)) != NULL) {
                    received++;
                    ReplyMsg(&message->snm_Message);
                }
                kprintf("[SETPROBE] notify: messages after unsubscribe and resubscribe=%ld\n",
                    (long)received);
                if (received != 1)
                    step = 34;
                else if ((message = (struct SettingNotifyMessage *)GetMsg(palette_port)) != NULL) {
                    ReplyMsg(&message->snm_Message);
                    step = 31;
                } else if (NotifySettingA("font/system/size", font_port, unsubscribe) != SETTING_OK ||
                           NotifySettingA("palette/", palette_port, unsubscribe) != SETTING_OK)
                    step = 35;
            }
        }
        if (font_port != NULL)
            DeleteMsgPort(font_port);
        if (palette_port != NULL)
            DeleteMsgPort(palette_port);
        if (step != 0) {
            kprintf("[SETPROBE] step %ld failed ioerr=%ld\n", (long)step, (long)IoErr());
            return step;
        }
        TRACE("notification round trip passed");
    }
    TRACE("all steps passed");
    CloseLibrary(SettingsBase);
    return 75;
}
