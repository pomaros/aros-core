/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    This file is part of the PrefsEditor class, which is distributed under
    the terms of version 2.1 of the GNU Lesser General Public License.

    Default Describe, Filter and Reveal: a walk over the editor's gadget
    tree. A label is the Text object that precedes a gadget in its group
    (the Label2/gadget pairs of the editors' column groups); a section is
    the nearest enclosing group with a frame title.

    Filter keeps the objects it hid and the labels it marked until the next
    Filter or the editor's disposal. An editor that removes or disposes its
    own children at run time calls Filter with no ids first.
*/
#include <exec/types.h>
#include <utility/hooks.h>
#include <libraries/mui.h>
#include <zune/prefseditor.h>

#include <proto/alib.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/utility.h>

#include <string.h>

#include "prefseditor_private.h"

#define NAME_MAX 96

struct walk
{
    Object      *editor;
    struct Hook *hook;            /* Describe: called per record          */
    const struct PrefsEditorMatch *matches;             /* Filter: matched ids, count            */
    ULONG        count;
    ULONG        reveal;          /* Reveal: the id to find               */
    Object      *found;
    Object      *found_section;
    Object      *found_register;  /* Reveal: the register and page that  */
    LONG         found_page;      /* hold the found gadget, or NULL/-1    */
    ULONG        index;           /* walk order, for path ids             */
    ULONG        outer_id;        /* a Listview's or Popstring's own id   */
    Object      *label;           /* pending label object                 */
    char         name[NAME_MAX];  /* its cleaned text                     */
    struct PrefsEditor_DATA *data;
    BOOL         filtering;
};

static BOOL is_a(Object *object, CONST_STRPTR name)
{
    Class *cl = OCLASS(object);
    while (cl != NULL)
    {
        if (cl->cl_ID != NULL && strcmp((const char *)cl->cl_ID, (const char *)name) == 0)
            return TRUE;
        cl = cl->cl_Super;
    }
    return FALSE;
}

static ULONG role_of(Object *object)
{
    if (is_a(object, MUIC_Text) && XGET(object, MUIA_ObjectID))
        return XGET(object, MUIA_InputMode) == MUIV_InputMode_None ?
            PREFSEDITOR_ROLE_TEXT : PREFSEDITOR_ROLE_ACTION;
    if (is_a(object, MUIC_String))  return PREFSEDITOR_ROLE_STRING;
    if (is_a(object, MUIC_Cycle) || is_a(object, MUIC_Radio)) return PREFSEDITOR_ROLE_CHOICE;
    if (is_a(object, MUIC_Numeric)) return PREFSEDITOR_ROLE_NUMBER;
    if (is_a(object, MUIC_Floattext)) return 0;   /* read-only text */
    if (is_a(object, MUIC_List))    return PREFSEDITOR_ROLE_LIST;
    if (is_a(object, MUIC_Image) && XGET(object, MUIA_InputMode) == MUIV_InputMode_Toggle)
        return PREFSEDITOR_ROLE_TOGGLE;
    return 0;
}

/* Text's parsed stream has the same offsets as its Highlight attribute. */
static void clean(char *out, CONST_STRPTR text)
{
    size_t n = text ? strlen((const char *)text) : 0;
    if (n >= NAME_MAX) n = NAME_MAX - 1;
    if (n) memcpy(out, text, n);
    while (n && (out[n - 1] == ':' || out[n - 1] == ' ')) n--;
    out[n] = 0;
}

static const struct PrefsEditorMatch *matched(const struct walk *w, ULONG id)
{
    ULONG i;
    for (i = 0; i < w->count; i++)
        if (w->matches[i].id == id)
            return &w->matches[i];
    return NULL;
}

static void mark(struct walk *w, Object *label, Object *gadget,
    const struct PrefsEditorMatch *match)
{
    struct PrefsEditor_DATA *data = w->data;
    ULONG n = data->ped_MarkedCount;
    if (n >= 64) return;
    data->ped_Marked[n] = label;
    data->ped_MarkedGadget[n] = gadget;
    data->ped_PreviousMark[n] = XGET(gadget, MUIA_Area_SearchMark);
    data->ped_MarkedCount++;
    SET(gadget, MUIA_Area_SearchMark, TRUE);
    if (label)
        SET(label, MUIA_Text_Highlight, (IPTR)&match->highlight);
}

/* Cycle and Radio own Text children for every entry, including inactive
 * pages; reading these avoids borrowing editor-specific entry structs. */
static void choice_text(Object *group, char *out, ULONG capacity)
{
    struct List *children = NULL;
    Object *state, *child;
    if (!GetAttr(MUIA_Group_ChildList, group, (IPTR *)&children) || !children) return;
    state = (Object *)children->lh_Head;
    while ((child = NextObject(&state)))
    {
        if (is_a(child, MUIC_Text))
        {
            CONST_STRPTR text = (CONST_STRPTR)XGET(child, MUIA_Text_DisplayText);
            ULONG used = strlen(out), n = text ? strlen((const char *)text) : 0;
            if (used + n + 2 < capacity)
            {
                if (used) out[used++] = ' ';
                memcpy(out + used, text, n);
                out[used + n] = 0;
            }
        }
        else if (is_a(child, MUIC_Group)) choice_text(child, out, capacity);
    }
}

/* Returns TRUE when something under group matched (Filter) or was found. */
static BOOL walk_group(struct walk *w, Object *group, CONST_STRPTR section, Object *section_group)
{
    struct List *children = NULL;
    Object *child, *state;
    BOOL any = FALSE;

    if (!GetAttr(MUIA_Group_ChildList, group, (IPTR *)&children) || children == NULL)
        return FALSE;
    state = (Object *)children->lh_Head;
    while ((child = NextObject(&state)) != NULL)
    {
        /* A value gadget first: Zune's Cycle and Radio are groups. */
        ULONG role = role_of(child);

        if (role == 0 && is_a(child, MUIC_Group) && !is_a(child, MUIC_Listview) &&
            !is_a(child, MUIC_Popstring) && !is_a(child, MUIC_Register))
        {
            CONST_STRPTR title = (CONST_STRPTR)XGET(child, MUIA_FrameTitle);
            Object *own = title != NULL ? child : section_group;
            BOOL inner;

            /* A label belongs to its own section: none crosses a frame. */
            if (title != NULL)
            {
                w->label = NULL;
                w->name[0] = '\0';
            }
            inner = walk_group(w, child, title != NULL ? title : section, own);
            if (title != NULL)
            {
                w->label = NULL;
                w->name[0] = '\0';
            }
            if (w->filtering && title != NULL && w->count > 0 && !inner &&
                w->data->ped_HiddenCount < 32)
            {
                if (XGET(child, MUIA_ShowMe))
                {
                    SET(child, MUIA_ShowMe, FALSE);
                    w->data->ped_Hidden[w->data->ped_HiddenCount++] = child;
                }
            }
            any |= inner;
            if (w->found != NULL)
                return TRUE;
            continue;
        }
        if (role == 0 && is_a(child, MUIC_Register))
        {
            struct List *pages = NULL;
            Object *page, *page_state;
            LONG index = 0;

            if (GetAttr(MUIA_Group_ChildList, child, (IPTR *)&pages) && pages != NULL)
            {
                page_state = (Object *)pages->lh_Head;
                while ((page = NextObject(&page_state)) != NULL)
                {
                    if (is_a(page, MUIC_Group))
                        any |= walk_group(w, page, section, section_group);
                    if (w->found != NULL)
                    {
                        if (w->found_register == NULL)
                        {
                            w->found_register = child;
                            w->found_page = index;
                        }
                        return TRUE;
                    }
                    index++;
                }
            }
            continue;
        }
        if (role == 0 && (is_a(child, MUIC_Listview) || is_a(child, MUIC_Popstring)))
        {
            /* The value gadget is inside: its list or its string, which
             * takes the container's id when it has none of its own. */
            ULONG outer = w->outer_id;
            w->outer_id = XGET(child, MUIA_ObjectID);
            any |= walk_group(w, child, section, section_group);
            w->outer_id = outer;
            if (w->found != NULL)
                return TRUE;
            continue;
        }
        if (role == 0)
        {
            if (is_a(child, MUIC_Text))
            {
                w->label = child;
                clean(w->name, (CONST_STRPTR)XGET(child, MUIA_Text_DisplayText));
            }
            continue;
        }
        {
            struct PrefsEditorRecord record;
            char choices[512] = {0};
            ULONG id = XGET(child, MUIA_ObjectID);

            /* A checkmark is often followed by its label instead. */
            if (w->name[0] == '\0')
            {
                Object *peek = state, *next = NextObject(&peek);
                Object *after_state = peek, *after = NULL;

                if (next != NULL)
                    after = NextObject(&after_state);
                /* Not when that text labels the gadget after it. */
                if (next != NULL && role_of(next) == 0 && is_a(next, MUIC_Text) &&
                    !is_a(next, MUIC_Group) && (after == NULL || role_of(after) == 0))
                {
                    w->label = next;
                    clean(w->name, (CONST_STRPTR)XGET(next, MUIA_Text_DisplayText));
                    state = peek;   /* the label is consumed */
                }
            }

            if (id == 0)
                id = w->outer_id;

            if (role == PREFSEDITOR_ROLE_ACTION ||
                (role == PREFSEDITOR_ROLE_TEXT && w->name[0] == '\0'))
            {
                w->label = child;
                clean(w->name, (CONST_STRPTR)XGET(child, MUIA_Text_DisplayText));
            }
            if (role == PREFSEDITOR_ROLE_CHOICE) choice_text(child, choices, sizeof(choices));
            record.per_Choices = (CONST_STRPTR)choices;
            record.per_ID = id != 0 ? id : (PREFSEDITOR_PATH_ID | w->index);
            w->index++;
            record.per_Name = (CONST_STRPTR)w->name;
            record.per_Help = (CONST_STRPTR)XGET(child, MUIA_ShortHelp);
            record.per_Section = section;
            record.per_Object = child;
            record.per_Role = role;
            if (w->hook != NULL)
                CallHookPkt(w->hook, w->editor, &record);
            if (w->filtering && matched(w, record.per_ID))
            {
                mark(w, w->label, child, matched(w, record.per_ID));
                any = TRUE;
            }
            if (w->reveal != 0 && record.per_ID == w->reveal)
            {
                w->found = child;
                w->found_section = section_group;
                return TRUE;
            }
            w->label = NULL;
            w->name[0] = '\0';
        }
    }
    return any;
}

static void restore(struct PrefsEditor_DATA *data)
{
    ULONG i;
    for (i = 0; i < data->ped_HiddenCount; i++)
        SET(data->ped_Hidden[i], MUIA_ShowMe, TRUE);
    data->ped_HiddenCount = 0;
    for (i = 0; i < data->ped_MarkedCount; i++)
    {
        if (data->ped_Marked[i])
            SET(data->ped_Marked[i], MUIA_Text_Highlight, 0);
        SET(data->ped_MarkedGadget[i], MUIA_Area_SearchMark, data->ped_PreviousMark[i]);
    }
    data->ped_MarkedCount = 0;
}

IPTR PrefsEditor__MUIM_PrefsEditor_Describe(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_Describe *message)
{
    struct walk w;
    memset(&w, 0, sizeof(w));
    w.editor = self;
    w.hook = message->hook;
    w.data = INST_DATA(CLASS, self);
    walk_group(&w, self, NULL, NULL);
    return w.index;
}

IPTR PrefsEditor__MUIM_PrefsEditor_Filter(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_Filter *message)
{
    struct PrefsEditor_DATA *data = INST_DATA(CLASS, self);
    struct walk w;

    restore(data);
    if (message->count == 0)
        return 0;
    memset(&w, 0, sizeof(w));
    w.editor = self;
    w.matches = message->matches;
    w.count = message->count;
    w.data = data;
    w.filtering = TRUE;
    return walk_group(&w, self, NULL, NULL);
}

IPTR PrefsEditor__MUIM_PrefsEditor_Reveal(Class *CLASS, Object *self,
    struct MUIP_PrefsEditor_Reveal *message)
{
    struct walk w;
    Object *window;

    memset(&w, 0, sizeof(w));
    w.editor = self;
    w.reveal = message->id;
    w.found_page = -1;
    w.data = INST_DATA(CLASS, self);
    walk_group(&w, self, NULL, NULL);
    if (w.found == NULL)
        return FALSE;
    if (w.found_section != NULL)
        SET(w.found_section, MUIA_ShowMe, TRUE);
    if (w.found_register != NULL)
        SET(w.found_register, MUIA_Group_ActivePage, w.found_page);
    /* The window is known only between Setup and Cleanup. */
    window = muiRenderInfo(self) != NULL ? _win(self) : NULL;
    if (window != NULL)
        SET(window, MUIA_Window_ActiveObject, (IPTR)w.found);
    return TRUE;
}
