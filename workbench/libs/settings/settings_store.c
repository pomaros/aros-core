/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The store side of settings.library: schema cache, layer paths, file
    images and the atomic replace.
*/
#include <string.h>
#include "settings_private.h"

void *settings_lib_alloc(void *ctx, size_t size)
{
    (void)ctx;
    return AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
}

void settings_lib_free(void *ctx, void *block, size_t size)
{
    (void)ctx; (void)size;
    FreeVec(block);
}

LONG settings_code_from_dos(LONG dos_error)
{
    switch (dos_error) {
    case ERROR_OBJECT_NOT_FOUND:
    case ERROR_DIR_NOT_FOUND:
    case ERROR_DEVICE_NOT_MOUNTED:
        return SETTING_ERROR_NO_FILE;
    case ERROR_NO_FREE_STORE:
        return SETTING_ERROR_MEMORY;
    default:
        return SETTING_ERROR_IO;
    }
}

LONG settings_image_read(struct SettingsBase *SettingsBase, CONST_STRPTR path,
    struct SettingsImage *image)
{
    BPTR file;
    struct FileInfoBlock *fib;
    LONG result = SETTING_OK;

    image->si_Data = NULL;
    image->si_Length = 0;
    file = Open(path, MODE_OLDFILE);
    if (file == BNULL)
        return settings_code_from_dos(IoErr());
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib == NULL) {
        Close(file);
        return SETTING_ERROR_MEMORY;
    }
    if (!ExamineFH(file, fib) || fib->fib_Size <= 0 || fib->fib_Size > 1024 * 1024) {
        result = fib->fib_Size <= 0 ? SETTING_ERROR_FORMAT : settings_code_from_dos(IoErr());
    } else {
        image->si_Length = (ULONG)fib->fib_Size;
        image->si_Data = AllocVec(image->si_Length, MEMF_PUBLIC);
        if (image->si_Data == NULL)
            result = SETTING_ERROR_MEMORY;
        else if (Read(file, image->si_Data, image->si_Length) != (LONG)image->si_Length) {
            result = SETTING_ERROR_IO;
            FreeVec(image->si_Data);
            image->si_Data = NULL;
        }
    }
    FreeDosObject(DOS_FIB, fib);
    Close(file);
    return result;
}

void settings_image_free(struct SettingsImage *image)
{
    if (image->si_Data != NULL)
        FreeVec(image->si_Data);
    image->si_Data = NULL;
    image->si_Length = 0;
}

/* Creates every missing directory between the volume and the file's name
 * (HOME:Settings/SYS for a first save in the user layer). Existing
 * directories are left alone; a failure surfaces as the Open that follows. */
static void make_parent_dirs(struct SettingsBase *SettingsBase, const char *path)
{
    char partial[SETTINGS_FULL_PATH];
    const char *colon = strchr(path, ':');
    size_t i, start = colon != NULL ? (size_t)(colon - path) + 1 : 0;
    BPTR lock;

    for (i = start; path[i] != '\0' && i < sizeof(partial) - 1; i++) {
        if (path[i] != '/')
            continue;
        memcpy(partial, path, i);
        partial[i] = '\0';
        lock = Lock((CONST_STRPTR)partial, SHARED_LOCK);
        if (lock == BNULL)
            lock = CreateDir((CONST_STRPTR)partial);
        if (lock != BNULL)
            UnLock(lock);
    }
}

/* Write to <path>.new in the same directory, then take the old name: in one
 * rename where the handler allows it, with a delete first where it answers
 * ERROR_OBJECT_EXISTS (ram-handler). A failed write leaves the old file. */
LONG settings_image_write_atomic(struct SettingsBase *SettingsBase, CONST_STRPTR path,
    const struct SettingsImage *image)
{
    char temporary[SETTINGS_FULL_PATH];
    BPTR file;
    LONG error;

    if (strlen((const char *)path) + 5 >= sizeof(temporary))
        return SETTING_ERROR_BUFFER;
    strcpy(temporary, (const char *)path);
    strcat(temporary, ".new");
    file = Open((CONST_STRPTR)temporary, MODE_NEWFILE);
    if (file == BNULL && (IoErr() == ERROR_OBJECT_NOT_FOUND || IoErr() == ERROR_DIR_NOT_FOUND)) {
        make_parent_dirs(SettingsBase, (const char *)path);
        file = Open((CONST_STRPTR)temporary, MODE_NEWFILE);
    }
    if (file == BNULL)
        return settings_code_from_dos(IoErr());
    if (Write(file, image->si_Data, image->si_Length) != (LONG)image->si_Length) {
        error = IoErr();
        Close(file);
        DeleteFile((CONST_STRPTR)temporary);
        SetIoErr(error);
        return SETTING_ERROR_IO;
    }
    if (!Close(file)) {
        error = IoErr();
        DeleteFile((CONST_STRPTR)temporary);
        SetIoErr(error);
        return SETTING_ERROR_IO;
    }
    if (!Rename((CONST_STRPTR)temporary, path)) {
        error = IoErr();
        if (error == ERROR_OBJECT_EXISTS) {
            if (!DeleteFile(path) || !Rename((CONST_STRPTR)temporary, path)) {
                error = IoErr();
                DeleteFile((CONST_STRPTR)temporary);
                SetIoErr(error);
                return SETTING_ERROR_IO;
            }
        } else {
            DeleteFile((CONST_STRPTR)temporary);
            SetIoErr(error);
            return SETTING_ERROR_IO;
        }
    }
    return SETTING_OK;
}

BOOL settings_layer_path(const struct settings_schema *schema, ULONG layer,
    STRPTR path, ULONG path_size)
{
    CONST_STRPTR prefix;
    switch (layer) {
    case SETTING_LAYER_APPLIED: prefix = SETTINGS_PATH_APPLIED; break;
    case SETTING_LAYER_USER:    prefix = SETTINGS_PATH_USER; break;
    case SETTING_LAYER_MACHINE: prefix = SETTINGS_PATH_MACHINE; break;
    default: return FALSE;
    }
    if (strlen((const char *)prefix) + strlen(schema->file) + 1 > path_size)
        return FALSE;
    strcpy((char *)path, (const char *)prefix);
    strcat((char *)path, schema->file);
    return TRUE;
}

static struct settings_schema *load_schema(struct SettingsBase *SettingsBase,
    CONST_STRPTR stem, ULONG stem_length, LONG *error)
{
    char path[SETTINGS_FULL_PATH];
    struct SettingsImage text;
    struct settings_schema *schema = NULL;
    char message[96];
    int result;

    if (strlen(SETTINGS_SCHEMA_DIR) + stem_length + 8 >= sizeof(path)) {
        *error = SETTING_ERROR_BUFFER;
        return NULL;
    }
    strcpy(path, SETTINGS_SCHEMA_DIR);
    memcpy(path + strlen(path), stem, stem_length);
    path[strlen(SETTINGS_SCHEMA_DIR) + stem_length] = '\0';
    strcat(path, ".schema");
    *error = settings_image_read(SettingsBase, (CONST_STRPTR)path, &text);
    if (*error != SETTING_OK) {
        if (*error == SETTING_ERROR_NO_FILE)
            *error = SETTING_ERROR_NO_SCHEMA;
        return NULL;
    }
    result = settings_schema_parse((const char *)text.si_Data, text.si_Length,
        settings_lib_alloc, settings_lib_free, NULL, &schema, message, sizeof(message));
    settings_image_free(&text);
    if (result != SETTINGS_E_OK) {
        *error = result == SETTINGS_E_MEMORY ? SETTING_ERROR_MEMORY : SETTING_ERROR_NO_SCHEMA;
        return NULL;
    }
    if (strlen(schema->stem) != stem_length ||
        strncmp(schema->stem, (const char *)stem, stem_length) != 0) {
        settings_schema_free(schema, settings_lib_free, NULL);
        *error = SETTING_ERROR_NO_SCHEMA;
        return NULL;
    }
    return schema;
}

const struct settings_schema *settings_schema_for_stem(struct SettingsBase *SettingsBase,
    CONST_STRPTR stem, ULONG stem_length, LONG *error)
{
    struct SettingsSchemaNode *node;
    struct settings_schema *schema = NULL;

    *error = SETTING_OK;
    ObtainSemaphore(&SettingsBase->sb_Lock);
    ForeachNode(&SettingsBase->sb_Schemas, node) {
        if (strlen(node->ssn_Schema->stem) == stem_length &&
            strncmp(node->ssn_Schema->stem, (const char *)stem, stem_length) == 0) {
            schema = node->ssn_Schema;
            break;
        }
    }
    if (schema == NULL) {
        schema = load_schema(SettingsBase, stem, stem_length, error);
        if (schema != NULL) {
            node = AllocVec(sizeof(*node), MEMF_PUBLIC | MEMF_CLEAR);
            if (node == NULL) {
                settings_schema_free(schema, settings_lib_free, NULL);
                schema = NULL;
                *error = SETTING_ERROR_MEMORY;
            } else {
                node->ssn_Schema = schema;
                AddTail((struct List *)&SettingsBase->sb_Schemas, (struct Node *)node);
            }
        }
    }
    ReleaseSemaphore(&SettingsBase->sb_Lock);
    return schema;
}

const struct settings_schema *settings_schema_for_key(struct SettingsBase *SettingsBase,
    CONST_STRPTR key, const struct settings_record **record, LONG *error)
{
    const struct settings_schema *schema;
    const char *text = (const char *)key;
    const char *slash;

    *record = NULL;
    if (text == NULL || text[0] == '\0') {
        *error = SETTING_ERROR_UNKNOWN;
        return NULL;
    }
    slash = strchr(text, '/');
    if (slash == NULL || slash == text) {
        *error = SETTING_ERROR_UNKNOWN;
        return NULL;
    }
    schema = settings_schema_for_stem(SettingsBase, key, (ULONG)(slash - text), error);
    if (schema == NULL)
        return NULL;
    *record = settings_schema_find(schema, text);
    if (*record == NULL) {
        *error = SETTING_ERROR_UNKNOWN;
        return NULL;
    }
    *error = SETTING_OK;
    return schema;
}

LONG settings_describe(const struct settings_schema *schema,
    const struct settings_record *record, struct SettingDescription *description)
{
    memset(description, 0, sizeof(*description));
    description->sd_Size = sizeof(*description);
    description->sd_Key = record->key;
    description->sd_File = schema->file;
    description->sd_Type = record->type;
    description->sd_Level = record->level;
    description->sd_Applier = record->applier;
    description->sd_Live = record->live != 0;
    description->sd_Min = record->min;
    description->sd_Max = record->max;
    description->sd_Unit = record->unit[0] != '\0' ? record->unit : NULL;
    description->sd_Default = record->has_default ? record->dflt : NULL;
    description->sd_ChoiceCount = record->choice_count;
    description->sd_ChoiceNames = record->choice_names;
    description->sd_ChoiceValues = (const LONG *)record->choice_values;
    description->sd_Offset = record->offset;
    description->sd_FieldSize = record->size;
    return SETTING_OK;
}


/* Reads the file of a layer. The user layer falls back to the machine layer
 * when the user has no copy; *source_layer says which one answered. */
LONG settings_read_layer(struct SettingsBase *SettingsBase, const struct settings_schema *schema,
    ULONG layer, struct SettingsImage *image, ULONG *source_layer)
{
    char path[SETTINGS_FULL_PATH];
    LONG result;

    if (!settings_layer_path(schema, layer, (STRPTR)path, sizeof(path)))
        return SETTING_ERROR_RANGE;
    result = settings_image_read(SettingsBase, (CONST_STRPTR)path, image);
    *source_layer = layer;
    if (result == SETTING_ERROR_NO_FILE && layer == SETTING_LAYER_USER) {
        if (!settings_layer_path(schema, SETTING_LAYER_MACHINE, (STRPTR)path, sizeof(path)))
            return SETTING_ERROR_RANGE;
        result = settings_image_read(SettingsBase, (CONST_STRPTR)path, image);
        *source_layer = SETTING_LAYER_MACHINE;
    }
    return result;
}
