/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The settings window: a PrefsWindow whose contents are a sidebar of
    groups (search field, back and forward above it) and one content area
    showing either a group page (header and rows) or an editor page (header
    and one PrefsEditor created from the row's class). Save, Use, Test,
    Revert and Cancel go to every editor page opened so far.
*/
#define MUIMASTER_YES_INLINE_STDARG

#include <exec/types.h>
#include <intuition/intuition.h>
#include <libraries/mui.h>
#include <zune/customclasses.h>
#include <zune/prefseditor.h>
#include <zune/prefswindow.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>
#include <aros/debug.h>

#include <string.h>
#include <stdio.h>

#include <libraries/settings.h>
#include <proto/settings.h>

#include "settingswindow.h"
#include "settingsprefs.h"

#define HISTORY 32
#define INDEX_MAX 512

/* One gadget of an editor, from MUIM_PrefsEditor_Describe: what search
 * matches besides the catalogue's row text. */
struct index_record
{
    UWORD row;
    ULONG id;
    char  name[96];
    char  help[96];
    char  section[48];
    char  choices[512];
};

/* A page is a group (kind 0) or a row (kind 1), by index in the catalogue. */
struct page_ref { UBYTE kind; UWORD index; };

/* What a sidebar line stands for. */
struct side_entry { UBYTE kind; UWORD index; };

struct SettingsWindow_DATA
{
    struct catalogue  *swd_Catalogue;
    Object            *swd_Sidebar, *swd_Search, *swd_Back, *swd_Forward;
    Object            *swd_Title, *swd_Description, *swd_Content, *swd_Shown;
    Object            *swd_GroupPage[CAT_GROUPS];
    Object            *swd_RowPage[CAT_ROWS];    /* editor page, created once */
    Object            *swd_Editor[CAT_ROWS];
    struct side_entry  swd_Side[CAT_GROUPS + CAT_ROWS];
    ULONG              swd_SideCount;
    struct page_ref    swd_History[HISTORY];
    ULONG              swd_HistoryCount, swd_HistoryAt;
    BOOL               swd_Navigating;
    Object            *swd_Results, *swd_ResultBody;
    Object            *swd_Card[CAT_ROWS], *swd_NoResults;
    struct MUI_EventHandlerNode swd_Keys;
    LONG               swd_ResultAt;
    char               swd_Line[CAT_GROUPS + CAT_ROWS][CAT_NAME + 4];
    struct index_record *swd_Index;          /* built at the first search */
    ULONG              swd_IndexCount;
    BOOL               swd_Indexed;
    char               swd_Query[CAT_NAME];
    Object            *swd_LevelCycle, *swd_ShowHidden, *swd_BootUndo;
    ULONG              swd_Level;                /* CAT_NOVICE..CAT_EXPERT */
    BOOL               swd_Hidden;               /* show settings above it */
};

static CONST_STRPTR level_names[] = { "Novice", "Average", "Expert", NULL };

/*** The level is a setting (settings/window/level) ***************************/
struct prefs_context { UBYTE chunk[4]; };

AROS_UFH3(static IPTR, prefs_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct SettingReplaceMessage *, message, A2),
    AROS_UFHA(APTR, unused, A1))
{
    AROS_USERFUNC_INIT
    struct prefs_context *context = hook->h_Data;
    ULONG i, n = 0;
    /* Keep every other chunk; the SETW chunk is ours, written whole. */
    for (i = 0; i < message->srm_Count; i++)
        if (message->srm_Chunks[i].sc_ID != ID_SETW)
            message->srm_Chunks[n++] = message->srm_Chunks[i];
    if (n >= message->srm_Capacity)
        return 0;
    message->srm_Chunks[n].sc_ID = ID_SETW;
    message->srm_Chunks[n].sc_Size = sizeof(context->chunk);
    message->srm_Chunks[n].sc_Data = context->chunk;
    message->srm_Count = n + 1;
    return 1;
    AROS_USERFUNC_EXIT
}

static void load_level(struct SettingsWindow_DATA *data)
{
    char value[16];
    ULONG i;
    data->swd_Level = CAT_AVERAGE;
    data->swd_Hidden = FALSE;
    if (SettingsBase == NULL)
        return;
    if (GetSettingA((CONST_STRPTR)"settings/window/level", (STRPTR)value, sizeof(value), NULL) == SETTING_OK)
        for (i = 0; level_names[i] != NULL; i++)
            if (Stricmp((CONST_STRPTR)value, level_names[i]) == 0)
                data->swd_Level = i;
    if (GetSettingA((CONST_STRPTR)"settings/window/show-hidden", (STRPTR)value, sizeof(value), NULL) == SETTING_OK)
        data->swd_Hidden = value[0] == '1';
}

/* Written whole through ReplaceSettingsA, so the first change creates the
 * file; ENV: now, ENVARC: with the window's Save. */
static void store_level(struct SettingsWindow_DATA *data, ULONG layer)
{
    struct prefs_context context;
    struct Hook hook;
    struct TagItem tags[] = { { SETTING_Layer, layer }, { TAG_DONE, 0 } };
    if (SettingsBase == NULL)
        return;
    memset(&context, 0, sizeof(context));
    context.chunk[0] = (UBYTE)data->swd_Level;
    context.chunk[1] = data->swd_Hidden ? 1 : 0;
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (HOOKFUNC)prefs_hook;
    hook.h_Data = &context;
    ReplaceSettingsA((CONST_STRPTR)"settings", &hook, tags);
}

#define SETUP_INST_DATA struct SettingsWindow_DATA *data = INST_DATA(CLASS, self)

/*** Pages ******************************************************************/
static ULONG row_matches(struct SettingsWindow_DATA *data, ULONG row, const char *query,
    ULONG *ids, ULONG capacity);

static Object *make_group_page(Class *CLASS, Object *self, struct SettingsWindow_DATA *data,
    ULONG group)
{
    const struct catalogue *c = data->swd_Catalogue;
    Object *page = VGroup, End;
    ULONG r;

    if (page == NULL)
        return NULL;
    for (r = 0; r < c->row_count; r++)
    {
        Object *row, *button;
        if (strcmp(c->rows[r].group, c->groups[group].id) != 0)
            continue;
        row = HGroup,
            Child, (IPTR)(button = SimpleButton(c->rows[r].name)),
            Child, (IPTR)TextObject,
                MUIA_Text_Contents, (IPTR)c->rows[r].description,
            End,
        End;
        if (row == NULL)
            continue;
        DoMethod(page, OM_ADDMEMBER, (IPTR)row);
        DoMethod(button, MUIM_Notify, MUIA_Pressed, FALSE,
            (IPTR)self, 2, MUIM_SettingsWindow_OpenRow, r);
    }
    DoMethod(page, OM_ADDMEMBER, (IPTR)HVSpace);
    return page;
}

static Object *make_row_page(Class *CLASS, Object *self, struct SettingsWindow_DATA *data,
    ULONG row)
{
    const struct cat_row *r = &data->swd_Catalogue->rows[row];
    Object *editor = MUI_NewObject((STRPTR)r->class_name, TAG_DONE);

    data->swd_Editor[row] = editor;
    if (editor == NULL)
        return VGroup,
            Child, (IPTR)TextObject,
                MUIA_Text_Contents, (IPTR)"This editor's class is not installed.",
            End,
            Child, (IPTR)HVSpace,
        End;
    DoMethod(editor, MUIM_Notify, MUIA_PrefsEditor_Changed, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_SettingsWindow_UpdateButtons);
    DoMethod(editor, MUIM_Notify, MUIA_PrefsEditor_Testing, MUIV_EveryTime,
        (IPTR)self, 1, MUIM_SettingsWindow_UpdateButtons);
    return editor;
}

/* An editor page under a query shows only its matching sections, with
 * the matches marked and the first one focused; no query shows it all. */
static void filter_row(struct SettingsWindow_DATA *data, ULONG row)
{
    ULONG ids[64], n, i, j;
    struct PrefsEditorMatch matches[64];
    struct MUI_TextHighlightRange (*ranges)[96];

    if (data->swd_Editor[row] == NULL)
        return;
    n = row_matches(data, row, data->swd_Query, ids, 64);
    ranges = n ? AllocVec(n * sizeof(*ranges), MEMF_ANY) : NULL;
    memset(matches, 0, sizeof(matches));
    for (i = 0; i < n; i++)
    {
        matches[i].id = ids[i];
        if (!ranges) continue;
        matches[i].highlight.ranges = ranges[i];
        for (j = 0; j < data->swd_IndexCount; j++)
        {
            const struct index_record *r = &data->swd_Index[j];
            ULONG start, len = strlen(data->swd_Query);
            if (r->row != row || r->id != ids[i] || !len) continue;
            for (start = 0; r->name[start]; start++)
            {
                ULONG k;
                for (k = 0; k < len && r->name[start + k] &&
                    ToLower((UBYTE)r->name[start + k]) == ToLower((UBYTE)data->swd_Query[k]); k++);
                if (k == len && matches[i].highlight.count < 96)
                {
                    ULONG at = matches[i].highlight.count++;
                    ranges[i][at].start = start;
                    ranges[i][at].length = len;
                    start += len - 1;
                }
            }
            break;
        }
    }
    DoMethod(data->swd_Editor[row], MUIM_PrefsEditor_Filter, (IPTR)matches, n);
    FreeVec(ranges);
    if (n > 0)
        DoMethod(data->swd_Editor[row], MUIM_PrefsEditor_Reveal, ids[0]);
}

static void clear_results(struct SettingsWindow_DATA *data);

static void show(Class *CLASS, Object *self, struct SettingsWindow_DATA *data,
    struct page_ref ref, BOOL record)
{
    const struct catalogue *c = data->swd_Catalogue;
    Object *page;

    if (data->swd_Query[0] && data->swd_Shown == data->swd_Results)
    {
        if (ref.kind == 1 && data->swd_Card[ref.index])
        {
            filter_row(data, ref.index);
            return;
        }
        SET(data->swd_Search, MUIA_String_Contents, (IPTR)"");
    }
    if (ref.kind == 0)
    {
        if (data->swd_GroupPage[ref.index] == NULL)
            data->swd_GroupPage[ref.index] = make_group_page(CLASS, self, data, ref.index);
        page = data->swd_GroupPage[ref.index];
        SET(data->swd_Title, MUIA_Text_Contents, c->groups[ref.index].name);
        SET(data->swd_Description, MUIA_Text_Contents, c->groups[ref.index].description);
    }
    else
    {
        if (data->swd_RowPage[ref.index] == NULL)
            data->swd_RowPage[ref.index] = make_row_page(CLASS, self, data, ref.index);
        page = data->swd_RowPage[ref.index];
        SET(data->swd_Title, MUIA_Text_Contents, c->rows[ref.index].name);
        SET(data->swd_Description, MUIA_Text_Contents, c->rows[ref.index].description);
    }
    if (page != NULL && page != data->swd_Shown)
    {
        DoMethod(data->swd_Content, MUIM_Group_InitChange);
        if (data->swd_Shown != NULL)
            DoMethod(data->swd_Content, OM_REMMEMBER, (IPTR)data->swd_Shown);
        DoMethod(data->swd_Content, OM_ADDMEMBER, (IPTR)page);
        DoMethod(data->swd_Content, MUIM_Group_ExitChange);
        data->swd_Shown = page;
    }
    /* After the page is attached, so Reveal finds its window. */
    if (ref.kind == 1)
        filter_row(data, ref.index);
    if (record)
    {
        /* A new visit drops the forward history, as a browser does. */
        if (data->swd_HistoryCount > 0 && data->swd_HistoryAt + 1 < data->swd_HistoryCount)
            data->swd_HistoryCount = data->swd_HistoryAt + 1;
        if (data->swd_HistoryCount == HISTORY)
        {
            memmove(&data->swd_History[0], &data->swd_History[1],
                sizeof(data->swd_History[0]) * (HISTORY - 1));
            data->swd_HistoryCount--;
        }
        data->swd_History[data->swd_HistoryCount++] = ref;
        data->swd_HistoryAt = data->swd_HistoryCount - 1;
    }
    SET(data->swd_Back, MUIA_Disabled, data->swd_HistoryAt == 0);
    SET(data->swd_Forward, MUIA_Disabled, data->swd_HistoryAt + 1 >= data->swd_HistoryCount);
}

/*** Index ******************************************************************/
struct describe_context { struct SettingsWindow_DATA *data; UWORD row; };

static void copy_text(char *dst, size_t size, CONST_STRPTR src)
{
    size_t n = 0;
    while (src != NULL && src[n] != '\0' && n + 1 < size) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

AROS_UFH3(static IPTR, describe_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, editor, A2),
    AROS_UFHA(struct PrefsEditorRecord *, record, A1))
{
    AROS_USERFUNC_INIT
    struct describe_context *context = hook->h_Data;
    struct SettingsWindow_DATA *data = context->data;
    struct index_record *r;

    if (data->swd_IndexCount >= INDEX_MAX)
        return 0;
    r = &data->swd_Index[data->swd_IndexCount++];
    r->row = context->row;
    r->id = record->per_ID;
    copy_text(r->name, sizeof(r->name), record->per_Name);
    copy_text(r->help, sizeof(r->help), record->per_Help);
    copy_text(r->section, sizeof(r->section), record->per_Section);
    copy_text(r->choices, sizeof(r->choices), record->per_Choices);
    return 0;
    AROS_USERFUNC_EXIT
}

static Object *make_row_page(Class *CLASS, Object *self, struct SettingsWindow_DATA *data, ULONG row);

/* The disk cache contains descriptions only: no object or process pointers.
 * Its key hashes the class binary (therefore its version), catalogue fields
 * and locale preferences. Invalid/missing files take the headless Describe
 * path once; only opened pages survive that operation. */
#define INDEX_FORMAT 0x53495804UL
struct index_header
{
    ULONG format, record_size, class_hash, catalogue_hash, language_hash, count;
};

static ULONG index_hash(ULONG hash, const void *buffer, ULONG length)
{
    const UBYTE *p = buffer;
    while (length--) hash = (hash ^ *p++) * 16777619UL;
    return hash;
}

static BOOL file_hash(const char *path, ULONG *hash)
{
    UBYTE buffer[1024];
    LONG got;
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!file) return FALSE;
    *hash = 2166136261UL;
    while ((got = Read(file, buffer, sizeof(buffer))) > 0)
        *hash = index_hash(*hash, buffer, got);
    Close(file);
    return got == 0;
}

static void cache_directory(const char *path)
{
    BPTR lock = CreateDir((CONST_STRPTR)path);
    if (lock) UnLock(lock);
}

static BOOL cache_read(const char *path, struct index_header *key,
    struct SettingsWindow_DATA *data, ULONG row)
{
    struct index_header header;
    ULONG i, base = data->swd_IndexCount;
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    BOOL valid = FALSE;
    if (!file) return FALSE;
    if (Read(file, &header, sizeof(header)) != sizeof(header) ||
        header.format != key->format || header.record_size != key->record_size ||
        header.class_hash != key->class_hash || header.catalogue_hash != key->catalogue_hash ||
        header.language_hash != key->language_hash || header.count > INDEX_MAX - base)
        goto out;
    if (Read(file, &data->swd_Index[base], header.count * sizeof(struct index_record)) !=
        header.count * sizeof(struct index_record))
        goto out;
    for (i = base; i < base + header.count; i++)
    {
        struct index_record *r = &data->swd_Index[i];
        if (!memchr(r->name, 0, sizeof(r->name)) || !memchr(r->help, 0, sizeof(r->help)) ||
            !memchr(r->section, 0, sizeof(r->section)) || !memchr(r->choices, 0, sizeof(r->choices))) goto out;
        r->row = row;
    }
    data->swd_IndexCount += header.count;
    valid = TRUE;
out:
    Close(file);
    return valid;
}

static void cache_write(const char *path, struct index_header *key,
    const struct index_record *records)
{
    char temporary[256];
    BPTR file;
    BOOL okay;
    snprintf(temporary, sizeof(temporary), "%s.%lx.tmp", path, (unsigned long)FindTask(NULL));
    file = Open((CONST_STRPTR)temporary, MODE_NEWFILE);
    if (!file) return;
    okay = Write(file, key, sizeof(*key)) == sizeof(*key) &&
        Write(file, (APTR)records, key->count * sizeof(*records)) == key->count * sizeof(*records);
    if (!Close(file)) okay = FALSE;
    if (okay)
    {
        /* A failed replace is harmless: the next process rebuilds the index. */
        DeleteFile((CONST_STRPTR)path);
        Rename((CONST_STRPTR)temporary, (CONST_STRPTR)path);
    }
    DeleteFile((CONST_STRPTR)temporary);
}

static void build_index(Class *CLASS, Object *self, struct SettingsWindow_DATA *data)
{
    struct describe_context context;
    struct Hook hook;
    ULONG r, language = 0, api_hash = 0;
    char directory[96];

    data->swd_Index = AllocVec(sizeof(struct index_record) * INDEX_MAX, MEMF_ANY | MEMF_CLEAR);
    if (data->swd_Index == NULL)
        return;
    data->swd_Indexed = TRUE;
    file_hash("ENV:SYS/locale.prefs", &language);
    file_hash("SYS:Classes/Zune/PrefsEditor.mcc", &api_hash);
    snprintf(directory, sizeof(directory), "SYS:Prefs/Index/%08lx", (unsigned long)language);
    cache_directory("SYS:Prefs/Index");
    cache_directory(directory);
    memset(&hook, 0, sizeof(hook));
    hook.h_Entry = (HOOKFUNC)describe_hook;
    hook.h_Data = &context;
    context.data = data;
    for (r = 0; r < data->swd_Catalogue->row_count; r++)
    {
        const struct cat_row *row = &data->swd_Catalogue->rows[r];
        struct index_header key;
        char classpath[160], cachepath[192];
        ULONG start = data->swd_IndexCount;
        Object *editor;
        BOOL cacheable;
        memset(&key, 0, sizeof(key));
        key.format = INDEX_FORMAT;
        key.record_size = sizeof(struct index_record);
        key.language_hash = language;
        key.catalogue_hash = index_hash(api_hash, row, sizeof(*row));
        snprintf(classpath, sizeof(classpath), "SYS:Classes/Zune/%s", row->class_name);
        snprintf(cachepath, sizeof(cachepath), "%s/%s.idx", directory, row->class_name);
        cacheable = file_hash(classpath, &key.class_hash);
        if (cacheable && cache_read(cachepath, &key, data, r)) continue;
        /* Reuse a live page to respect editors with a single-instance guard. */
        editor = data->swd_Editor[r];
        if (!editor) editor = MUI_NewObject((STRPTR)row->class_name, TAG_DONE);
        if (!editor) continue;
        context.row = (UWORD)r;
        DoMethod(editor, MUIM_PrefsEditor_Describe, (IPTR)&hook);
        if (editor != data->swd_Editor[r]) MUI_DisposeObject(editor);
        key.count = data->swd_IndexCount - start;
        if (cacheable && data->swd_IndexCount < INDEX_MAX)
            cache_write(cachepath, &key, &data->swd_Index[start]);
    }
}

static int contains(const char *haystack, const char *needle)
{
    size_t i, j;
    if (needle[0] == '\0')
        return 1;
    for (i = 0; haystack[i] != '\0'; i++)
    {
        for (j = 0; needle[j] != '\0' && haystack[i + j] != '\0' &&
             ToLower((UBYTE)haystack[i + j]) == ToLower((UBYTE)needle[j]); j++)
            ;
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

static BOOL record_matches(const struct index_record *r, const char *query)
{
    return contains(r->name, query) || contains(r->help, query) || contains(r->section, query) || contains(r->choices, query);
}

/* The ids of a row's gadgets that match the query; returns the count. */
static ULONG row_matches(struct SettingsWindow_DATA *data, ULONG row, const char *query,
    ULONG *ids, ULONG capacity)
{
    ULONG i, n = 0;
    if (query[0] == '\0' || data->swd_Index == NULL)
        return 0;
    for (i = 0; i < data->swd_IndexCount && n < capacity; i++)
        if (data->swd_Index[i].row == row && record_matches(&data->swd_Index[i], query))
            ids[n++] = data->swd_Index[i].id;
    return n;
}

/* The row matches by its catalogue text or by one of its gadgets, and its
 * level is at or below the window's unless hidden rows are shown. */
static BOOL row_visible(struct SettingsWindow_DATA *data, ULONG row, const char *query)
{
    ULONG id;
    if ((ULONG)data->swd_Catalogue->rows[row].level > data->swd_Level && !data->swd_Hidden)
        return FALSE;
    return cat_row_matches(data->swd_Catalogue, &data->swd_Catalogue->rows[row], query) ||
           row_matches(data, row, query, &id, 1) != 0;
}

/* A row above the level, shown because hidden rows are shown, is in
 * italics with its level after it. */
static void row_line(struct SettingsWindow_DATA *data, char *line, size_t size, ULONG row)
{
    const struct cat_row *r = &data->swd_Catalogue->rows[row];
    if ((ULONG)r->level > data->swd_Level)
        snprintf(line, size, "   \33i%s (%s)", r->name, level_names[r->level]);
    else
        snprintf(line, size, "   %s", r->name);
}

/*** Sidebar ****************************************************************/
static void fill_sidebar(struct SettingsWindow_DATA *data, CONST_STRPTR query)
{
    const struct catalogue *c = data->swd_Catalogue;
    ULONG g, r, n = 0;

    SET(data->swd_Sidebar, MUIA_List_Quiet, TRUE);
    DoMethod(data->swd_Sidebar, MUIM_List_Clear);
    for (g = 0; g < c->group_count; g++)
    {
        BOOL any = query[0] == '\0';
        for (r = 0; r < c->row_count && !any; r++)
            any = strcmp(c->rows[r].group, c->groups[g].id) == 0 &&
                  row_visible(data, r, (const char *)query);
        if (!any)
            continue;
        snprintf(data->swd_Line[n], sizeof(data->swd_Line[n]), "\33b%s", c->groups[g].name);
        data->swd_Side[n].kind = 0;
        data->swd_Side[n].index = (UWORD)g;
        DoMethod(data->swd_Sidebar, MUIM_List_InsertSingle, (IPTR)data->swd_Line[n], MUIV_List_Insert_Bottom);
        n++;
        for (r = 0; r < c->row_count; r++)
        {
            if (strcmp(c->rows[r].group, c->groups[g].id) != 0 ||
                !row_visible(data, r, (const char *)query))
                continue;
            row_line(data, data->swd_Line[n], sizeof(data->swd_Line[n]), r);
            data->swd_Side[n].kind = 1;
            data->swd_Side[n].index = (UWORD)r;
            DoMethod(data->swd_Sidebar, MUIM_List_InsertSingle, (IPTR)data->swd_Line[n], MUIV_List_Insert_Bottom);
            n++;
        }
    }
    /* Rows whose group is missing stay reachable through search. */
    if (query[0] != '\0')
        for (r = 0; r < c->row_count; r++)
            if (cat_group_index(c, c->rows[r].group) < 0 &&
                row_visible(data, r, (const char *)query))
            {
                row_line(data, data->swd_Line[n], sizeof(data->swd_Line[n]), r);
                data->swd_Side[n].kind = 1;
                data->swd_Side[n].index = (UWORD)r;
                DoMethod(data->swd_Sidebar, MUIM_List_InsertSingle, (IPTR)data->swd_Line[n], MUIV_List_Insert_Bottom);
                n++;
            }
    data->swd_SideCount = n;
    SET(data->swd_Sidebar, MUIA_List_Quiet, FALSE);
}

/* Cards borrow their editors. Detach every editor before disposing a card,
 * so switching query/page never destroys edits and disposal has one owner. */
static void clear_results(struct SettingsWindow_DATA *data)
{
    ULONG r;
    if (!data->swd_ResultBody) return;
    DoMethod(data->swd_ResultBody, MUIM_Group_InitChange);
    for (r = 0; r < CAT_ROWS; r++)
        if (data->swd_Card[r])
        {
            DoMethod(data->swd_Card[r], OM_REMMEMBER, (IPTR)data->swd_RowPage[r]);
            DoMethod(data->swd_ResultBody, OM_REMMEMBER, (IPTR)data->swd_Card[r]);
            MUI_DisposeObject(data->swd_Card[r]);
            data->swd_Card[r] = NULL;
        }
    if (data->swd_NoResults)
    {
        DoMethod(data->swd_ResultBody, OM_REMMEMBER, (IPTR)data->swd_NoResults);
        MUI_DisposeObject(data->swd_NoResults);
        data->swd_NoResults = NULL;
    }
    DoMethod(data->swd_ResultBody, MUIM_Group_ExitChange);
}

static void show_results(Class *CLASS, Object *self, struct SettingsWindow_DATA *data)
{
    ULONG r, n = 0;
    BOOL attaching = data->swd_Shown != data->swd_Results || !data->swd_Results;
    if (!data->swd_Results)
        data->swd_Results = ScrollgroupObject,
            MUIA_Scrollgroup_Contents, (IPTR)(data->swd_ResultBody = VirtgroupObject, End),
        End;
    if (!data->swd_Results) { data->swd_ResultBody = NULL; return; }
    DoMethod(data->swd_Content, MUIM_Group_InitChange);
    if (attaching && data->swd_Shown)
        DoMethod(data->swd_Content, OM_REMMEMBER, (IPTR)data->swd_Shown);
    DoMethod(data->swd_ResultBody, MUIM_Group_InitChange);
    for (r = 0; r < data->swd_Catalogue->row_count; r++)
    {
        BOOL visible = row_visible(data, r, data->swd_Query);
        if (!visible)
        {
            if (data->swd_Card[r]) SET(data->swd_Card[r], MUIA_ShowMe, FALSE);
            continue;
        }
        if (!data->swd_RowPage[r])
            data->swd_RowPage[r] = make_row_page(CLASS, self, data, r);
        if (!data->swd_RowPage[r]) continue;
        if (!data->swd_Card[r])
        {
            data->swd_Card[r] = VGroup,
                GroupFrame,
                MUIA_FrameTitle, (IPTR)data->swd_Catalogue->rows[r].name,
            End;
            if (!data->swd_Card[r]) continue;
            DoMethod(data->swd_Card[r], OM_ADDMEMBER, (IPTR)data->swd_RowPage[r]);
            DoMethod(data->swd_ResultBody, OM_ADDMEMBER, (IPTR)data->swd_Card[r]);
        }
        /* Keep the same attachment while typing. Reparenting a live editor
         * runs Cleanup/Setup and would restart its service workers. */
        SET(data->swd_Card[r], MUIA_ShowMe, TRUE);
        n++;
    }
    if (!data->swd_NoResults)
    {
        data->swd_NoResults = TextObject, End;
        if (data->swd_NoResults)
            DoMethod(data->swd_ResultBody, OM_ADDMEMBER, (IPTR)data->swd_NoResults);
    }
    if (data->swd_NoResults)
    {
        char text[CAT_NAME + 96];
        snprintf(text, sizeof(text), "No settings match '%s'. Try another word or Help search.", data->swd_Query);
        SET(data->swd_NoResults, MUIA_Text_Contents, (IPTR)text);
        SET(data->swd_NoResults, MUIA_ShowMe, n == 0);
    }
    DoMethod(data->swd_ResultBody, MUIM_Group_ExitChange);
    if (attaching) DoMethod(data->swd_Content, OM_ADDMEMBER, (IPTR)data->swd_Results);
    data->swd_Shown = data->swd_Results;
    DoMethod(data->swd_Content, MUIM_Group_ExitChange);
    SET(data->swd_Title, MUIA_Text_Contents, (IPTR)"Search results");
    SET(data->swd_Description, MUIA_Text_Contents, (IPTR)data->swd_Query);
    for (r = 0; r < data->swd_Catalogue->row_count; r++)
        if (data->swd_Card[r] && XGET(data->swd_Card[r], MUIA_ShowMe)) filter_row(data, r);
    SET(self, MUIA_Window_ActiveObject, (IPTR)data->swd_Search);
    data->swd_ResultAt = -1;
}

/*** Methods ****************************************************************/
static IPTR SettingsWindow__OM_NEW(Class *CLASS, Object *self, struct opSet *message)
{
    struct catalogue *catalogue = (struct catalogue *)GetTagData(MUIA_SettingsWindow_Catalogue, 0,
        message->ops_AttrList);
    CONST_STRPTR initial = (CONST_STRPTR)GetTagData(MUIA_SettingsWindow_Initial, 0,
        message->ops_AttrList);
    Object *sidebar, *search, *back, *forward, *title, *description, *content, *level, *hidden, *bootundo;

    if (catalogue == NULL)
        return 0;
    self = (Object *)DoSuperNewTags
    (
        CLASS, self, NULL,
        MUIA_Window_Title, (IPTR)"Settings",
        MUIA_Window_ID, MAKE_ID('S','E','T','W'),
        WindowContents, (IPTR)HGroup,
            Child, (IPTR)VGroup,
                MUIA_Weight, 30,
                Child, (IPTR)HGroup,
                    Child, (IPTR)(back = SimpleButton("<")),
                    Child, (IPTR)(forward = SimpleButton(">")),
                    Child, (IPTR)HSpace(0),
                End,
                Child, (IPTR)(search = StringObject,
                    StringFrame,
                    MUIA_String_MaxLen, CAT_NAME,
                    MUIA_CycleChain, 1,
                    MUIA_ObjectID, MAKE_ID('S','W','S','R'),
                End),
                Child, (IPTR)HGroup,
                    Child, (IPTR)(level = CycleObject,
                        MUIA_Cycle_Entries, (IPTR)level_names,
                        MUIA_ObjectID, MAKE_ID('S','W','L','V'),
                    End),
                    Child, (IPTR)(hidden = MUI_MakeObject(MUIO_Checkmark, NULL)),
                    Child, (IPTR)Label1("Show the rest"),
                End,
                Child, (IPTR)(bootundo = SimpleButton("Settings as at boot")),
                Child, (IPTR)ListviewObject,
                    MUIA_Listview_List, (IPTR)(sidebar = ListObject,
                        InputListFrame,
                        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                        MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                    End),
                End,
            End,
            Child, (IPTR)VGroup,
                MUIA_Weight, 70,
                Child, (IPTR)(title = TextObject,
                    MUIA_Font, MUIV_Font_Big,
                    MUIA_Text_Contents, (IPTR)"",
                End),
                Child, (IPTR)(description = TextObject,
                    MUIA_Text_Contents, (IPTR)"",
                End),
                Child, (IPTR)RectangleObject, MUIA_Rectangle_HBar, TRUE, MUIA_FixHeight, 2, End,
                Child, (IPTR)(content = VGroup, End),
            End,
        End,
        TAG_MORE, (IPTR)message->ops_AttrList
    );
    if (self != NULL)
    {
        SETUP_INST_DATA;
        struct page_ref first = { 0, 0 };
        ULONG r;

        memset(data, 0, sizeof(*data));
        data->swd_Keys.ehn_Class = CLASS;
        data->swd_Keys.ehn_Object = self;
        data->swd_Keys.ehn_Events = IDCMP_RAWKEY;
        data->swd_Keys.ehn_Priority = 10;
        DoMethod(self, MUIM_Window_AddEventHandler, (IPTR)&data->swd_Keys);
        data->swd_Catalogue = catalogue;
        data->swd_Sidebar = sidebar;
        data->swd_Search = search;
        data->swd_Back = back;
        data->swd_Forward = forward;
        data->swd_Title = title;
        data->swd_Description = description;
        data->swd_Content = content;
        data->swd_LevelCycle = level;
        data->swd_ShowHidden = hidden;
        data->swd_BootUndo = bootundo;
        DoMethod(bootundo, MUIM_Notify, MUIA_Pressed, FALSE,
            (IPTR)self, 1, MUIM_SettingsWindow_BootUndo);

        load_level(data);
        NNSET(level, MUIA_Cycle_Active, data->swd_Level);
        NNSET(hidden, MUIA_Selected, data->swd_Hidden);
        fill_sidebar(data, (CONST_STRPTR)"");
        SET(self, MUIA_PrefsWindow_Test_Disabled, TRUE);
        SET(self, MUIA_PrefsWindow_Revert_Disabled, TRUE);
        SET(self, MUIA_PrefsWindow_Save_Disabled, TRUE);
        SET(self, MUIA_PrefsWindow_Use_Disabled, TRUE);

        DoMethod(sidebar, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime,
            (IPTR)self, 1, MUIM_SettingsWindow_Select);
        DoMethod(search, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
            (IPTR)self, 1, MUIM_SettingsWindow_Search);
        DoMethod(level, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
            (IPTR)self, 1, MUIM_SettingsWindow_Level);
        DoMethod(hidden, MUIM_Notify, MUIA_Selected, MUIV_EveryTime,
            (IPTR)self, 1, MUIM_SettingsWindow_Level);
        DoMethod(back, MUIM_Notify, MUIA_Pressed, FALSE,
            (IPTR)self, 1, MUIM_SettingsWindow_Back);
        DoMethod(forward, MUIM_Notify, MUIA_Pressed, FALSE,
            (IPTR)self, 1, MUIM_SettingsWindow_Forward);

        if (initial != NULL)
        {
            for (r = 0; r < catalogue->row_count; r++)
                if (Stricmp((CONST_STRPTR)catalogue->rows[r].class_name, initial) == 0)
                {
                    first.kind = 1;
                    first.index = (UWORD)r;
                    break;
                }
            if (first.kind == 0)
                Printf("Settings: no page for class %s\n", (IPTR)initial);
        }
        if (first.kind == 1 || catalogue->group_count > 0)
            show(CLASS, self, data, first, TRUE);
    }
    return (IPTR)self;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Select(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    LONG active = XGET(data->swd_Sidebar, MUIA_List_Active);
    struct page_ref ref;

    if (active < 0 || (ULONG)active >= data->swd_SideCount || data->swd_Navigating)
        return 0;
    ref.kind = data->swd_Side[active].kind;
    ref.index = data->swd_Side[active].index;
    show(CLASS, self, data, ref, TRUE);
    return 0;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_OpenRow(Class *CLASS, Object *self,
    struct MUIP_SettingsWindow_OpenRow *message)
{
    SETUP_INST_DATA;
    struct page_ref ref = { 1, (UWORD)message->row };

    if (message->row >= data->swd_Catalogue->row_count)
        return 0;
    show(CLASS, self, data, ref, TRUE);
    return 0;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Back(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (data->swd_HistoryAt == 0)
        return 0;
    data->swd_HistoryAt--;
    show(CLASS, self, data, data->swd_History[data->swd_HistoryAt], FALSE);
    return 0;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Forward(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (data->swd_HistoryAt + 1 >= data->swd_HistoryCount)
        return 0;
    data->swd_HistoryAt++;
    show(CLASS, self, data, data->swd_History[data->swd_HistoryAt], FALSE);
    return 0;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Search(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    STRPTR query = (STRPTR)XGET(data->swd_Search, MUIA_String_Contents);
    ULONG r;

    copy_text(data->swd_Query, sizeof(data->swd_Query), query);
    if (data->swd_Query[0] != '\0' && !data->swd_Indexed)
        build_index(CLASS, self, data);
    if (data->swd_Query[0] == '\0')
        for (r = 0; r < data->swd_Catalogue->row_count; r++)
            if (data->swd_Editor[r] != NULL)
                DoMethod(data->swd_Editor[r], MUIM_PrefsEditor_Filter, (IPTR)NULL, 0);
    data->swd_Navigating = TRUE;
    fill_sidebar(data, (CONST_STRPTR)data->swd_Query);
    data->swd_Navigating = FALSE;
    if (data->swd_Query[0])
        show_results(CLASS, self, data);
    else if (data->swd_Results && data->swd_Shown == data->swd_Results)
    {
        clear_results(data);
        if (data->swd_HistoryCount)
            show(CLASS, self, data, data->swd_History[data->swd_HistoryAt], FALSE);
    }
    return 0;
}

static void append(STRPTR buffer, ULONG size, ULONG *used, CONST_STRPTR a, CONST_STRPTR b)
{
    ULONG la = strlen((const char *)a), lb = b != NULL ? strlen((const char *)b) : 0;
    if (*used + la + lb + 2 >= size)
        return;
    memcpy(buffer + *used, a, la);
    *used += la;
    if (b != NULL)
    {
        buffer[(*used)++] = '/';
        memcpy(buffer + *used, b, lb);
        *used += lb;
    }
    buffer[(*used)++] = '\n';
    buffer[*used] = '\0';
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Find(Class *CLASS, Object *self,
    struct MUIP_SettingsWindow_Find *message)
{
    SETUP_INST_DATA;
    const struct catalogue *c = data->swd_Catalogue;
    ULONG used = 0, r, i, found = 0;

    if (message->size == 0)
        return 0;
    message->buffer[0] = '\0';
    if (!data->swd_Indexed)
        build_index(CLASS, self, data);
    for (r = 0; r < c->row_count; r++)
        if (cat_row_matches(c, &c->rows[r], (const char *)message->text))
        {
            append(message->buffer, message->size, &used, (CONST_STRPTR)c->rows[r].name, NULL);
            found++;
        }
    for (i = 0; data->swd_Index != NULL && i < data->swd_IndexCount; i++)
        if (record_matches(&data->swd_Index[i], (const char *)message->text))
        {
            append(message->buffer, message->size, &used,
                (CONST_STRPTR)c->rows[data->swd_Index[i].row].name,
                (CONST_STRPTR)data->swd_Index[i].name);
            found++;
        }
    return found;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Level(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    data->swd_Level = XGET(data->swd_LevelCycle, MUIA_Cycle_Active);
    data->swd_Hidden = XGET(data->swd_ShowHidden, MUIA_Selected) != 0;
    store_level(data, SETTING_LAYER_APPLIED);
    data->swd_Navigating = TRUE;
    fill_sidebar(data, (CONST_STRPTR)data->swd_Query);
    data->swd_Navigating = FALSE;
    if (data->swd_Query[0]) show_results(CLASS, self, data);
    return 0;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_SetLevel(Class *CLASS, Object *self,
    struct MUIP_SettingsWindow_SetLevel *message)
{
    SETUP_INST_DATA;
    ULONG i;
    for (i = 0; level_names[i] != NULL; i++)
        if (Stricmp(message->name, level_names[i]) == 0)
        {
            SET(data->swd_LevelCycle, MUIA_Cycle_Active, i);   /* notifies Level */
            return TRUE;
        }
    return FALSE;
}

/*** Self-check *************************************************************/
/* Every finding goes to the debug console and to the program's output, so
 * a run can be judged from a serial log or from a redirected file. */
/* Flushed per line: a run that hangs on a page leaves its last line in a
 * file on a disk. */
#define CHECKLINE(...) do { bug(__VA_ARGS__); Printf(__VA_ARGS__); Flush(Output()); } while (0)

static ULONG visible_rows(struct SettingsWindow_DATA *data)
{
    ULONG i, n = 0;
    for (i = 0; i < data->swd_SideCount; i++)
        if (data->swd_Side[i].kind == 1)
            n++;
    return n;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_Check(Class *CLASS, Object *self,
    struct MUIP_SettingsWindow_Check *message)
{
    SETUP_INST_DATA;
    const struct catalogue *c = data->swd_Catalogue;
    ULONG r, i, failures = 0, rows[3], first = c->row_count, saved = data->swd_Level;
    char found[1024];

    for (r = 0; r < c->row_count; r++)
    {
        struct page_ref ref = { 1, (UWORD)r };
        CHECKLINE("[SETCHECK] opening %s\n", c->rows[r].class_name);
        show(CLASS, self, data, ref, TRUE);
        CHECKLINE("[SETCHECK] page %s class=%s editor=%s shown=%s\n", c->rows[r].name,
            c->rows[r].class_name, data->swd_Editor[r] != NULL ? "yes" : "no",
            data->swd_Shown == data->swd_RowPage[r] ? "yes" : "no");
        if (data->swd_Editor[r] == NULL)
        {
            /* Tell a class that does not load from one whose object fails. */
            char path[CAT_NAME + 8];
            struct Library *base;
            snprintf(path, sizeof(path), "Zune/%s", c->rows[r].class_name);
            base = OpenLibrary((CONST_STRPTR)path, 0);
            CHECKLINE("[SETCHECK] class %s library=%s ioerr=%ld\n", c->rows[r].class_name,
                base != NULL ? "opens" : "fails", (long)IoErr());
            if (base != NULL)
                CloseLibrary(base);
        }
        if (data->swd_Editor[r] == NULL || data->swd_Shown != data->swd_RowPage[r])
            failures++;
    }
    CHECKLINE("[SETCHECK] indexing\n");
    if (!data->swd_Indexed)
        build_index(CLASS, self, data);
    CHECKLINE("[SETCHECK] index records=%lu\n", (unsigned long)data->swd_IndexCount);
    for (r = 0; r < c->row_count; r++)
    {
        ULONG records = 0, explicit = 0;
        for (i = 0; i < data->swd_IndexCount; i++)
            if (data->swd_Index[i].row == r)
            {
                records++;
                if ((data->swd_Index[i].id & 0x80000000) == 0)
                    explicit++;
            }
        CHECKLINE("[SETCHECK] row %s records=%lu explicit-ids=%lu\n", c->rows[r].name,
            (unsigned long)records, (unsigned long)explicit);
        /* An editor page that the index cannot see is not searchable. */
        if (data->swd_Editor[r] != NULL && records == 0)
            failures++;
    }
    if (data->swd_IndexCount == 0)
        failures++;

    /* The levels without the cycle's notification, which would store
     * each one as the user's choice. */
    for (i = 0; i < 3; i++)
    {
        NNSET(data->swd_LevelCycle, MUIA_Cycle_Active, i);
        data->swd_Level = i;
        data->swd_Navigating = TRUE;
        fill_sidebar(data, (CONST_STRPTR)data->swd_Query);
        data->swd_Navigating = FALSE;
        rows[i] = visible_rows(data);
        CHECKLINE("[SETCHECK] level %s rows=%lu\n", level_names[i], (unsigned long)rows[i]);
    }
    if (!(rows[0] <= rows[1] && rows[1] <= rows[2] && rows[2] == c->row_count))
        failures++;
    NNSET(data->swd_LevelCycle, MUIA_Cycle_Active, saved);
    data->swd_Level = saved;
    data->swd_Navigating = TRUE;
    fill_sidebar(data, (CONST_STRPTR)data->swd_Query);
    data->swd_Navigating = FALSE;

    if (message->text != NULL && message->text[0] != '\0')
    {
        ULONG n = DoMethod(self, MUIM_SettingsWindow_Find, (IPTR)message->text,
            (IPTR)found, sizeof(found));
        CHECKLINE("[SETCHECK] find %s matches=%lu\n%s", message->text, (unsigned long)n, found);
        SET(data->swd_Search, MUIA_String_Contents, message->text);
        for (i = 0; i < data->swd_SideCount; i++)
            if (data->swd_Side[i].kind == 1) { first = data->swd_Side[i].index; break; }
        CHECKLINE("[SETCHECK] search %s sidebar=%lu rows=%lu\n", message->text,
            (unsigned long)data->swd_SideCount, (unsigned long)visible_rows(data));
        if (n == 0 || first >= c->row_count)
            failures++;
        else
        {
            struct page_ref ref = { 1, (UWORD)first };
            show(CLASS, self, data, ref, TRUE);
            CHECKLINE("[SETCHECK] search opens %s\n", c->rows[first].name);
        }
    }
    CHECKLINE("[SETCHECK] result=%s failures=%lu\n", failures == 0 ? "PASS" : "FAIL",
        (unsigned long)failures);
    Flush(Output());
    return failures;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_SetSearch(Class *CLASS, Object *self,
    struct MUIP_SettingsWindow_SetSearch *message)
{
    SETUP_INST_DATA;
    SET(data->swd_Search, MUIA_String_Contents, message->text);
    return 0;
}

/*** Settings as at boot ***************************************************/
/* Copies one file over its ENV: counterpart when they differ; write to a
 * temporary name and take the old one, like settings.library. Returns 1
 * when the file was restored. */
static ULONG restore_file(CONST_STRPTR from, CONST_STRPTR to)
{
    BPTR in, out;
    UBYTE *a = NULL, *b = NULL;
    LONG la = -1, lb = -1;
    ULONG restored = 0;
    char temporary[300];

    if ((in = Open(from, MODE_OLDFILE)) == BNULL)
        return 0;
    Seek(in, 0, OFFSET_END);
    la = Seek(in, 0, OFFSET_BEGINNING);
    if (la >= 0 && la < 262144 && (a = AllocVec(la + 1, MEMF_ANY)) != NULL && Read(in, a, la) == la)
    {
        if ((out = Open(to, MODE_OLDFILE)) != BNULL)
        {
            Seek(out, 0, OFFSET_END);
            lb = Seek(out, 0, OFFSET_BEGINNING);
            if (lb == la && (b = AllocVec(lb + 1, MEMF_ANY)) != NULL && Read(out, b, lb) == lb &&
                memcmp(a, b, la) == 0)
                la = -2;            /* identical: nothing to do */
            Close(out);
        }
        if (la >= 0)
        {
            snprintf(temporary, sizeof(temporary), "%s.new", (const char *)to);
            if ((out = Open((CONST_STRPTR)temporary, MODE_NEWFILE)) != BNULL)
            {
                BOOL ok = Write(out, a, la) == la;
                Close(out);
                if (ok && (Rename((CONST_STRPTR)temporary, to) ||
                    (IoErr() == ERROR_OBJECT_EXISTS && DeleteFile(to) && Rename((CONST_STRPTR)temporary, to))))
                    restored = 1;
                else
                    DeleteFile((CONST_STRPTR)temporary);
            }
        }
    }
    if (a != NULL) FreeVec(a);
    if (b != NULL) FreeVec(b);
    Close(in);
    return restored;
}

/* Walks the snapshot; dir is relative to it ("" at the top). */
static ULONG restore_dir(CONST_STRPTR dir)
{
    char path[300], target[300];
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR lock;
    ULONG restored = 0;

    snprintf(path, sizeof(path), "%s%s%s", SETTINGS_BOOT_SNAPSHOT, dir[0] ? "/" : "", (const char *)dir);
    if (fib == NULL || (lock = Lock((CONST_STRPTR)path, SHARED_LOCK)) == BNULL)
    {
        if (fib != NULL) FreeDosObject(DOS_FIB, fib);
        return 0;
    }
    if (Examine(lock, fib))
        while (ExNext(lock, fib))
        {
            char child[300];
            snprintf(child, sizeof(child), "%s%s%s", (const char *)dir, dir[0] ? "/" : "",
                (const char *)fib->fib_FileName);
            if (fib->fib_DirEntryType > 0)
            {
                BPTR made;
                snprintf(target, sizeof(target), "ENV:%s", child);
                if ((made = Lock((CONST_STRPTR)target, SHARED_LOCK)) == BNULL)
                    made = CreateDir((CONST_STRPTR)target);
                if (made != BNULL)
                    UnLock(made);
                restored += restore_dir((CONST_STRPTR)child);
            }
            else
            {
                snprintf(path, sizeof(path), "%s/%s", SETTINGS_BOOT_SNAPSHOT, child);
                snprintf(target, sizeof(target), "ENV:%s", child);
                restored += restore_file((CONST_STRPTR)path, (CONST_STRPTR)target);
            }
        }
    UnLock(lock);
    FreeDosObject(DOS_FIB, fib);
    return restored;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_BootUndo(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    static char text[96];
    BPTR lock = Lock((CONST_STRPTR)SETTINGS_BOOT_SNAPSHOT, SHARED_LOCK);
    ULONG restored, r;

    if (lock == BNULL)
    {
        /* Refused, with the reason: without the snapshot there is no boot
         * state to go back to. */
        SET(data->swd_Description, MUIA_Text_Contents,
            (IPTR)"No settings snapshot from this boot (" SETTINGS_BOOT_SNAPSHOT "): nothing to restore.");
        return FALSE;
    }
    UnLock(lock);
    restored = restore_dir((CONST_STRPTR)"");
    /* Open pages show the restored values; IPrefs and the applications
     * follow ENV: through their file notifications. */
    for (r = 0; r < data->swd_Catalogue->row_count; r++)
        if (data->swd_Editor[r] != NULL)
            DoMethod(data->swd_Editor[r], MUIM_PrefsEditor_ImportFromDirectory, (IPTR)"ENV:");
    snprintf(text, sizeof(text), "Settings as at boot: %lu file(s) restored.", (unsigned long)restored);
    SET(data->swd_Description, MUIA_Text_Contents, (IPTR)text);
    return restored;
}

static IPTR SettingsWindow__MUIM_SettingsWindow_UpdateButtons(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    BOOL changed = FALSE, testing = FALSE;
    ULONG r;

    for (r = 0; r < data->swd_Catalogue->row_count; r++)
        if (data->swd_Editor[r] != NULL)
        {
            changed |= XGET(data->swd_Editor[r], MUIA_PrefsEditor_Changed) != 0;
            testing |= XGET(data->swd_Editor[r], MUIA_PrefsEditor_Testing) != 0;
        }
    SET(self, MUIA_PrefsWindow_Test_Disabled, !changed);
    SET(self, MUIA_PrefsWindow_Revert_Disabled, !(changed || testing));
    SET(self, MUIA_PrefsWindow_Save_Disabled, !(changed || testing));
    SET(self, MUIA_PrefsWindow_Use_Disabled, !(changed || testing));
    return 0;
}

/* One method to every opened editor that is changed or testing (only to
 * the changed ones when testing is FALSE); FALSE if any refused. An editor
 * the window merely created, for the index or for a look, is left alone,
 * so Use and Save never rewrite a file nobody touched. */
static BOOL each_editor(struct SettingsWindow_DATA *data, ULONG method, BOOL testing)
{
    BOOL all = TRUE;
    ULONG r;
    for (r = 0; r < data->swd_Catalogue->row_count; r++)
    {
        Object *editor = data->swd_Editor[r];
        if (editor == NULL)
            continue;
        if (!XGET(editor, MUIA_PrefsEditor_Changed) &&
            !(testing && XGET(editor, MUIA_PrefsEditor_Testing)))
            continue;
        if (!DoMethod(editor, method))
            all = FALSE;
    }
    return all;
}

static IPTR SettingsWindow__MUIM_PrefsWindow_Save(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (!each_editor(data, MUIM_PrefsEditor_Save, TRUE))
        return FALSE;
    store_level(data, SETTING_LAYER_MACHINE);
    DoMethod(_app(self), MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    return TRUE;
}

static IPTR SettingsWindow__MUIM_PrefsWindow_Use(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    if (!each_editor(data, MUIM_PrefsEditor_Use, TRUE))
        return FALSE;
    DoMethod(_app(self), MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    return TRUE;
}

static IPTR SettingsWindow__MUIM_PrefsWindow_Cancel(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    each_editor(data, MUIM_PrefsEditor_Cancel, TRUE);
    DoMethod(_app(self), MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    return TRUE;
}

static IPTR SettingsWindow__MUIM_PrefsWindow_Test(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    return each_editor(data, MUIM_PrefsEditor_Test, FALSE);
}

static IPTR SettingsWindow__MUIM_PrefsWindow_Revert(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    return each_editor(data, MUIM_PrefsEditor_Revert, TRUE);
}

/* Pages not on screen are detached from the content group; dispose them
 * with the window so their editors' resources are released. */
/* Search owns arrows only while the field is focused, leaving value-gadget
 * arrow keys intact. Escape from any result restores the pre-search page. */
static IPTR SettingsWindow__MUIM_HandleEvent(Class *CLASS, Object *self,
    struct MUIP_HandleEvent *message)
{
    SETUP_INST_DATA;
    ULONG i, code;
    LONG first = -1, last = -1, chosen = -1;
    if (!message->imsg || message->imsg->Class != IDCMP_RAWKEY || !data->swd_Query[0]) return 0;
    code = message->imsg->Code;
    if (code == 0x45)
    {
        SET(data->swd_Search, MUIA_String_Contents, (IPTR)"");
        return MUI_EventHandlerRC_Eat;
    }
    if ((Object *)XGET(self, MUIA_Window_ActiveObject) != data->swd_Search ||
        (code != 0x44 && code != 0x4c && code != 0x4d)) return 0;
    for (i = 0; i < data->swd_IndexCount; i++)
    {
        const struct index_record *r = &data->swd_Index[i];
        if (!data->swd_Card[r->row] || !XGET(data->swd_Card[r->row], MUIA_ShowMe) ||
            !record_matches(r, data->swd_Query)) continue;
        if (first < 0) first = i;
        last = i;
        if (code == 0x4c && (LONG)i < data->swd_ResultAt) chosen = i;
        if (code != 0x4c && chosen < 0 && (LONG)i > data->swd_ResultAt) chosen = i;
    }
    if (code == 0x44) chosen = data->swd_ResultAt >= 0 ? data->swd_ResultAt : first;
    if (chosen < 0) chosen = code == 0x4c ? last : first;
    if (chosen >= 0)
    {
        const struct index_record *r = &data->swd_Index[chosen];
        data->swd_ResultAt = chosen;
        DoMethod(data->swd_Editor[r->row], MUIM_PrefsEditor_Reveal, r->id);
        if (code != 0x44) SET(self, MUIA_Window_ActiveObject, (IPTR)data->swd_Search);
    }
    return MUI_EventHandlerRC_Eat;
}

static IPTR SettingsWindow__OM_DISPOSE(Class *CLASS, Object *self, Msg message)
{
    SETUP_INST_DATA;
    ULONG i;

    DoMethod(self, MUIM_Window_RemEventHandler, (IPTR)&data->swd_Keys);
    clear_results(data);
    if (data->swd_Results && data->swd_Results != data->swd_Shown)
        MUI_DisposeObject(data->swd_Results);
    for (i = 0; i < CAT_GROUPS; i++)
        if (data->swd_GroupPage[i] != NULL && data->swd_GroupPage[i] != data->swd_Shown)
            MUI_DisposeObject(data->swd_GroupPage[i]);
    for (i = 0; i < CAT_ROWS; i++)
        if (data->swd_RowPage[i] != NULL && data->swd_RowPage[i] != data->swd_Shown)
            MUI_DisposeObject(data->swd_RowPage[i]);
    if (data->swd_Index != NULL)
        FreeVec(data->swd_Index);
    return DoSuperMethodA(CLASS, self, message);
}

ZUNE_CUSTOMCLASS_20
(
    SettingsWindow, NULL, MUIC_PrefsWindow, NULL,
    OM_NEW,                              struct opSet *,
    OM_DISPOSE,                          Msg,
    MUIM_HandleEvent,                    struct MUIP_HandleEvent *,
    MUIM_SettingsWindow_Select,          Msg,
    MUIM_SettingsWindow_OpenRow,         struct MUIP_SettingsWindow_OpenRow *,
    MUIM_SettingsWindow_Back,            Msg,
    MUIM_SettingsWindow_Forward,         Msg,
    MUIM_SettingsWindow_Search,          Msg,
    MUIM_SettingsWindow_UpdateButtons,   Msg,
    MUIM_SettingsWindow_Find,            struct MUIP_SettingsWindow_Find *,
    MUIM_SettingsWindow_Level,           Msg,
    MUIM_SettingsWindow_SetLevel,        struct MUIP_SettingsWindow_SetLevel *,
    MUIM_SettingsWindow_BootUndo,        Msg,
    MUIM_SettingsWindow_Check,           struct MUIP_SettingsWindow_Check *,
    MUIM_SettingsWindow_SetSearch,       struct MUIP_SettingsWindow_SetSearch *,
    MUIM_PrefsWindow_Save,               Msg,
    MUIM_PrefsWindow_Use,                Msg,
    MUIM_PrefsWindow_Cancel,             Msg,
    MUIM_PrefsWindow_Test,               Msg,
    MUIM_PrefsWindow_Revert,             Msg
);
