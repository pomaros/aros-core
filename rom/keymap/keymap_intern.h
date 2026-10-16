/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * This file is distributed under the AROS Public License, version 1.1.
 */
#ifndef KEYMAP_INTERN_H
#define KEYMAP_INTERN_H
/*
    Copyright © 1995-2020, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Keymaps internal structure
    Lang: english
*/
#ifndef AROS_LIBCALL_H
#   include <aros/libcall.h>
#endif
#ifndef EXEC_EXECBASE_H
#   include <exec/execbase.h>
#endif
#ifndef EXEC_TYPES_H
#   include <exec/types.h>
#endif
#ifndef DEVICES_KEYMAP_H
#   include <devices/keymap.h>
#endif
#include <dos/bptr.h>

#define KEYMAPNAME "keymap.library"

/* uncomment the following line to use keymaps with 0x40 entries
 * in highkeymap, as opposed to 0x38 that AmigaOS uses */
//define NONSTANDARD_KEYMAP

struct KeymapBase;
#if !defined(DEFAULT_KEYMAP)
extern const UBYTE keymaptype_table[8][8];
extern const UBYTE keymapstr_table[8][8];
#endif

/* Structures */
struct BufInfo
{
    UBYTE *Buffer;
    LONG BufLength;
    LONG CharsWritten;
};

struct KeyInfo
{
    UBYTE       Key_MapType; /* KCF_xxx */ 
    
    /* 4 character combo, pointer to string descr, or pointer to deadkey descr,
    ** all ccording to Key_MapType
    */
    IPTR        Key_Mapping;
    
    UBYTE       KCFQual; /* The qualifiers for the keycode, converted to KCF_xxx format */ 
};


/* Prototypes */
BOOL WriteToBuffer(struct BufInfo *bufinfo, UBYTE *string, LONG numchars);
WORD GetKeyInfo(struct KeyInfo *ki, UWORD code, UWORD qual, struct KeyMap *km);
WORD GetDeadKeyIndex(UWORD code, UWORD qual, struct KeyMap *km);
        
/* Macros */
#define GetBitProperty(ubytearray, idx) \
    ( (ubytearray)[(idx) / 8] & ( 1 << ((idx) & 0x07) ))

/* Get one of the for characters in km_LoKeyMap or km_HiKeyMap addresses,
** id can be 0, 1, 2, 3
*/
#define GetMapChar(key_mapping, idx)    \
        ( (key_mapping >> ((3 - (idx)) * 8)) & 0x000000FF )
    
#define KMBase(x) ((struct KeymapBase *)x)


/* State is accessed only with local interrupts disabled and SwitchLock held. */
struct KeymapSwitcher
{
    ULONG Lock;
    struct KeyMap *Alternate;
    UWORD Qualifier;
    UWORD Code;
    BOOL Active;
};

struct KeyMap *KeymapSelect(struct KeymapBase *base, struct KeyMap *requested);

/* Librarybase struct */
struct KeymapBase
{
    struct Library               LibNode;
    struct KeyMap               *DefaultKeymap;
    struct KeyMapResource        KeymapResource;
    struct KeymapSwitcher        Switcher;
};


/* Published keymaps remain alive and immutable, including in interrupt users.
 * SMP readers acquire initialized contents without taking a sleeping lock. */
static inline struct KeyMap *KeymapDefaultLoad(struct KeymapBase *base)
{
#ifdef __AROSEXEC_SMP__
    return __atomic_load_n(&base->DefaultKeymap, __ATOMIC_ACQUIRE);
#else
    return base->DefaultKeymap;
#endif
}

static inline void KeymapDefaultStore(struct KeymapBase *base, struct KeyMap *map)
{
#ifdef __AROSEXEC_SMP__
    __atomic_store_n(&base->DefaultKeymap, map, __ATOMIC_RELEASE);
#else
    base->DefaultKeymap = map;
#endif
}

#endif /* KEYMAP_INTERN_H */
