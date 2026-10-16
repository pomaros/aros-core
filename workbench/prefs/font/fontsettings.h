#ifndef FONTSETTINGS_H
#define FONTSETTINGS_H
/*
    Copyright (C) 2026 John KNIPPER <code@jkn.me>.
    Distributed under the AROS Public License, version 1.1.

    The Font editor's store: font.prefs read and written through
    settings.library, one layer at a time. Self-contained so the guest
    settings probe links the same code the editor runs.
*/
#include <exec/types.h>
#include <prefs/font.h>

#define FONTSETTINGS_COUNT 3     /* FP_WBFONT, FP_SYSFONT, FP_SCREENFONT */
#define FONTSETTINGS_CHUNK 156   /* sizeof the on-disk FileFontPrefs     */

/* The on-disk FONT chunk, big-endian, from and to the in-memory form. */
void FontSettings_Encode(const struct FontPrefs *fp, UBYTE chunk[FONTSETTINGS_CHUNK]);
void FontSettings_Decode(const UBYTE chunk[FONTSETTINGS_CHUNK], struct FontPrefs *fp);

/* SettingsBase must be open (settings.library 1). layer is a
 * SETTING_LAYER_*. Load fills the three entries the file carries and
 * returns SETTING_OK, SETTING_ERROR_NO_FILE when the layer (and the
 * machine layer below the user's) has no FONT chunk, or another
 * SETTING_ERROR_*; *source receives the layer that answered when not
 * NULL. Store rewrites the file of the layer with the three entries,
 * keeping its other chunks, through the library's atomic replace. */
LONG FontSettings_Load(struct FontPrefs fp[FONTSETTINGS_COUNT], ULONG layer, ULONG *source);
LONG FontSettings_Store(const struct FontPrefs fp[FONTSETTINGS_COUNT], ULONG layer);

#endif /* FONTSETTINGS_H */
