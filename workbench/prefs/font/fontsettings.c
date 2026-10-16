/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.
*/
#include <aros/asmcall.h>
#include <exec/types.h>
#include <libraries/iffparse.h>
#include <libraries/settings.h>
#include <prefs/prefhdr.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#include <proto/settings.h>

#include <string.h>

#include "fontsettings.h"

#define ID_PREF_HEADER MAKE_ID('P', 'R', 'H', 'D')

static void put16(UBYTE *p, UWORD v) { p[0] = (UBYTE)(v >> 8); p[1] = (UBYTE)v; }
static UWORD get16(const UBYTE *p) { return (UWORD)((p[0] << 8) | p[1]); }
static void put32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16); p[2] = (UBYTE)(v >> 8); p[3] = (UBYTE)v;
}
static ULONG get32(const UBYTE *p)
{
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) | ((ULONG)p[2] << 8) | p[3];
}

/* Offsets of struct FileFontPrefs (workbench/prefs/font/prefs.h), the
 * same the font schema names: fp_Type@14, ta_YSize@24, fp_Name@28. */
void FontSettings_Encode(const struct FontPrefs *fp, UBYTE chunk[FONTSETTINGS_CHUNK])
{
    int i;

    memset(chunk, 0, FONTSETTINGS_CHUNK);
    for (i = 0; i < 3; i++)
        put32(chunk + 4 * i, (ULONG)fp->fp_Reserved[i]);
    put16(chunk + 12, fp->fp_Reserved2);
    put16(chunk + 14, fp->fp_Type);
    chunk[16] = fp->fp_FrontPen;
    chunk[17] = fp->fp_BackPen;
    chunk[18] = fp->fp_DrawMode;
    /* 20..23: ta_Name, a pointer in memory, zero on disk */
    put16(chunk + 24, fp->fp_TextAttr.ta_YSize);
    chunk[26] = fp->fp_TextAttr.ta_Style;
    chunk[27] = fp->fp_TextAttr.ta_Flags;
    strncpy((char *)chunk + 28, (const char *)fp->fp_Name, FONTNAMESIZE - 1);
}

void FontSettings_Decode(const UBYTE chunk[FONTSETTINGS_CHUNK], struct FontPrefs *fp)
{
    int i;

    memset(fp, 0, sizeof(*fp));
    for (i = 0; i < 3; i++)
        fp->fp_Reserved[i] = (LONG)get32(chunk + 4 * i);
    fp->fp_Reserved2 = get16(chunk + 12);
    fp->fp_Type = get16(chunk + 14);
    fp->fp_FrontPen = chunk[16];
    fp->fp_BackPen = chunk[17];
    fp->fp_DrawMode = chunk[18];
    fp->fp_TextAttr.ta_YSize = get16(chunk + 24);
    fp->fp_TextAttr.ta_Style = chunk[26];
    fp->fp_TextAttr.ta_Flags = chunk[27];
    memcpy(fp->fp_Name, chunk + 28, FONTNAMESIZE);
    fp->fp_Name[FONTNAMESIZE - 1] = '\0';
    fp->fp_TextAttr.ta_Name = (STRPTR)fp->fp_Name;
}

struct load_context
{
    struct FontPrefs *fp;
    ULONG found;              /* bit per fp_Type seen */
};

struct store_context
{
    UBYTE header[6];
    UBYTE fonts[FONTSETTINGS_COUNT][FONTSETTINGS_CHUNK];
};

AROS_UFH3(IPTR, fontsettings_load_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    struct SettingReplaceMessage *message = object;
    struct load_context *context = hook->h_Data;
    ULONG i, positional = 0;
    (void)unused;

    for (i = 0; i < message->srm_Count; i++) {
        const struct SettingChunk *chunk = &message->srm_Chunks[i];
        struct FontPrefs decoded;
        ULONG slot;

        if (chunk->sc_ID != ID_FONT || chunk->sc_Size != FONTSETTINGS_CHUNK)
            continue;
        FontSettings_Decode(chunk->sc_Data, &decoded);
        /* The entry's own type places it; a foreign type keeps its order. */
        slot = decoded.fp_Type < FONTSETTINGS_COUNT ? decoded.fp_Type : positional;
        positional++;
        if (slot >= FONTSETTINGS_COUNT)
            continue;
        context->fp[slot] = decoded;
        context->fp[slot].fp_TextAttr.ta_Name = (STRPTR)context->fp[slot].fp_Name;
        context->found |= 1UL << slot;
    }
    return 0;
    AROS_USERFUNC_EXIT
}

AROS_UFH3(IPTR, fontsettings_store_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    struct SettingReplaceMessage *message = object;
    struct store_context *context = hook->h_Data;
    struct SettingChunk others[SETTING_REPLACE_CHUNKS];
    struct SettingChunk header = { ID_PREF_HEADER, sizeof(context->header), context->header };
    ULONG i, n = 0, other_count = 0;
    (void)unused;

    /* Keep the file's header and every chunk that is not a FONT entry. */
    for (i = 0; i < message->srm_Count; i++) {
        if (message->srm_Chunks[i].sc_ID == ID_PREF_HEADER)
            header = message->srm_Chunks[i];
        else if (message->srm_Chunks[i].sc_ID != ID_FONT)
            others[other_count++] = message->srm_Chunks[i];
    }
    if (1 + FONTSETTINGS_COUNT + other_count > message->srm_Capacity)
        return 0;
    message->srm_Chunks[n++] = header;
    for (i = 0; i < FONTSETTINGS_COUNT; i++) {
        message->srm_Chunks[n].sc_ID = ID_FONT;
        message->srm_Chunks[n].sc_Size = FONTSETTINGS_CHUNK;
        message->srm_Chunks[n].sc_Data = context->fonts[i];
        n++;
    }
    for (i = 0; i < other_count; i++)
        message->srm_Chunks[n++] = others[i];
    message->srm_Count = n;
    return 1;
    AROS_USERFUNC_EXIT
}

LONG FontSettings_Load(struct FontPrefs fp[FONTSETTINGS_COUNT], ULONG layer, ULONG *source)
{
    struct load_context context = { fp, 0 };
    struct Hook hook;
    ULONG answered = layer;
    struct TagItem tags[] = {
        { SETTING_Layer, layer },
        { SETTING_ReadOnly, TRUE },
        { SETTING_SourceLayer, (IPTR)&answered },
        { TAG_DONE, 0 }
    };
    LONG result;

    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (HOOKFUNC)fontsettings_load_hook;
    hook.h_Data = &context;
    result = ReplaceSettingsA((CONST_STRPTR)"font", &hook, tags);
    if (result == SETTING_OK && context.found == 0)
        result = SETTING_ERROR_NO_FILE;
    if (source != NULL)
        *source = answered;
    return result;
}

LONG FontSettings_Store(const struct FontPrefs fp[FONTSETTINGS_COUNT], ULONG layer)
{
    struct store_context context;
    struct Hook hook;
    struct TagItem tags[] = { { SETTING_Layer, layer }, { TAG_DONE, 0 } };
    ULONG i;

    memset(&context, 0, sizeof(context));
    context.header[0] = PHV_CURRENT;
    for (i = 0; i < FONTSETTINGS_COUNT; i++) {
        struct FontPrefs entry = fp[i];
        entry.fp_Type = (UWORD)i;
        FontSettings_Encode(&entry, context.fonts[i]);
    }
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (HOOKFUNC)fontsettings_store_hook;
    hook.h_Data = &context;
    return ReplaceSettingsA((CONST_STRPTR)"font", &hook, tags);
}
