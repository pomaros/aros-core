/*
    Copyright (C) 1995-2014, The AROS Development Team. All rights reserved.

    Copyright (C) 2026 John KNIPPER <code@jkn.me>
    Licensed under the AROS Public License (APL), Version 1.1.

    Desc: Sorts the contents of a file
*/
/*****************************************************************************

    NAME

        Sort

    SYNOPSIS

        FROM/A,TO/A,COLSTART/K,CASE/S,NUMERIC/S

    LOCATION

        C:
           
    FUNCTION

        Sorts the contents of a text file
        
    INPUTS

        FROM      -- file to read from
        TO        -- file to output to
        COLSTART  -- column at which the comparison begins
        CASE      -- sort is case sensitive. Uppercase items are output first
        NUMERIC   -- lines are interpreted as numbers

    RESULT

    EXAMPLE

    BUGS

    SEE ALSO

    INTERNALS

    HISTORY

******************************************************************************/

#include <clib/macros.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <dos/exall.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/locale.h>
#include <libraries/locale.h>
#include <utility/tagitem.h>

#define DEBUG 0
#include <aros/debug.h>

#define TEMPLATE "FROM/A,TO/A,COLSTART/K,CASE/S,NUMERIC/S"

#define ARG_FROM        0
#define ARG_TO          1
#define ARG_COLSTART    2
#define ARG_CASE        3
#define ARG_NUMERIC     4

#define ARG_NUM         5

const TEXT version[] = "$VER: Sort 41.3 (4.10.2026)";

struct sorted_data
{
  struct sorted_data * next;
  UBYTE              * data;
  ULONG                len; /* Line bytes, excluding the newline. */
};

struct Locale * locale;

int compare(struct sorted_data * sd1,
            struct sorted_data * sd2,
            ULONG col,
            BOOL case_on)
{
  ULONG len = MIN(sd1->len, sd2->len);
 
  /* FIXME:  It seems like StrnCmp of locale does not work. */
#if 1
  LONG retval = 0;
  
  if (case_on)
  {
    ULONG i = col;
    
    while (i < len)
    {
      BOOL a,b;
      a = IsUpper(locale,(ULONG)*(sd1->data+i));
      b = IsUpper(locale,(ULONG)*(sd2->data+i));
      
      if (a == b)
      {
        if (0 != (retval = StrnCmp(locale,
                           sd1->data+i,
                           sd2->data+i,
                           1,
                           SC_COLLATE2)))
          break;
      }
      else
      {
       retval = b - a;
       break;
      }
      
      i++;
    }
  }
  else
  {

    retval=StrnCmp(locale,
                   sd1->data+col,
                   sd2->data+col,
                   len - col,
                   SC_COLLATE2);

    if (0 == retval)
    {
      if (sd1->len < sd2->len)
        retval = -100;
      else if (sd1->len > sd2->len)
        retval = +100;
    }
  }
  
  return retval;

#else
  int i = col;
  char * str1 = sd1->data;
  char * str2 = sd2->data;

  while (i < len)
  {
    if (str1[i] != str2[i])
      return (int)(str1[i] - str2[i]);

    i++;
  }
  
  return (int)sd1->len - (int)sd2->len;
#endif
}


struct sorted_data * sort(UBYTE * data,
                          ULONG   data_len,
                          STRPTR  colstart,
                          BOOL    case_on,
                          BOOL    is_numeric)
{
  ULONG pos = 0;
  ULONG col = 0;
  struct sorted_data * first = NULL;
  struct sorted_data * cur   = NULL;
  struct sorted_data * tooshort = NULL;
  struct sorted_data * tooshort_last = NULL;
  
  if (colstart)
  {
    while (*colstart >= '0' && *colstart <= '9')
      col = col * 10 + *colstart++ - '0';
    if (col > 0)
      col-=1;
  }

  while (pos < data_len)
  {
    ULONG begin = pos;
    ULONG len;

    while (pos < data_len && data[pos] != 0x0a)
      pos++;

    len = pos - begin;
    if (pos < data_len) pos++;

    cur = AllocMem(sizeof(struct sorted_data), MEMF_ANY|MEMF_CLEAR);
    if (!cur)
    {
      while (first)
      {
        struct sorted_data *next = first->next;
        FreeMem(first, sizeof(*first)); first = next;
      }
      while (tooshort)
      {
        struct sorted_data *next = tooshort->next;
        FreeMem(tooshort, sizeof(*tooshort)); tooshort = next;
      }
      SetIoErr(ERROR_NO_FREE_STORE);
      return NULL;
    }

    if (cur)
    {
      cur->data = data + begin;
      cur->len  = len;
    
      if (len > col)
      {
        /*
        ** Insert it into the list of sorted lines
        */
        if (NULL != first)
        {
          /*
          ** To be first in the list?
          */
          if (compare(cur, first, col, case_on) < 0)
          {
            cur->next = first;
            first     = cur;
          }
          else
          {
            struct sorted_data * _cur = first;

            while (1)
            {
              /*
              ** Insert it after the current one and before the
              ** next one?
              */
              if (NULL == _cur->next)
              {
                _cur->next = cur;
                break;
              }
              if (compare(cur, _cur->next, col, case_on) < 0)
              {
                cur -> next = _cur->next;
                _cur-> next = cur;
                break;
              }
       
              _cur = _cur->next;
            }
          }
        } /* if (NULL != first) */
        else
          first = cur;
      }
      else
      {
        /* this line is too short to sort it in */
        if (NULL == tooshort)
        {
          tooshort = cur;
          tooshort_last = cur;
        }
        else
        {
          tooshort_last->next = cur;
          tooshort_last = cur;
        }
      }
    } /* if (cur) */
      
  }

  if (NULL != tooshort)
  {
    tooshort_last->next = first;
    first = tooshort;
  }

  return first;
}


LONG write_data(struct sorted_data * start, BPTR file_out)
{
  BOOL write = TRUE;
  LONG error = 0;
  LONG count;
  
  while (start)
  {
    struct sorted_data * next = start->next;

    if (write)
    {
      count = Write(file_out, start->data, start->len);
      if (count != (LONG)start->len || Write(file_out, "\n", 1) != 1)
      {
        error = IoErr();
        if (!error) error = ERROR_DISK_FULL;
        write = FALSE;
      }
    }

    FreeMem(start, sizeof(struct sorted_data));
    start = next;
  }

  return error;
}

int __nocommandline;

int main (void)
{
  IPTR args[ARG_NUM] = { 0 };
  struct RDArgs *rda = NULL;
  struct FileInfoBlock *fib = NULL;
  BPTR lock_in = BNULL, in = BNULL, out = BNULL;
  UBYTE *data = NULL;
  LONG error = 0, length = 0;

  locale = OpenLocale(NULL);
  if (!locale)
  {
    PutStr("Could not open locale!\n");
    return RETURN_FAIL;
  }

  rda = ReadArgs(TEMPLATE, args, NULL);
  if (!rda) { error = IoErr(); goto cleanup; }
  lock_in = Lock((STRPTR)args[ARG_FROM], ACCESS_READ);
  if (!lock_in) { error = IoErr(); goto cleanup; }
  fib = AllocDosObject(DOS_FIB, NULL);
  if (!fib) { error = ERROR_NO_FREE_STORE; goto cleanup; }
  if (!Examine(lock_in, fib)) { error = IoErr(); goto cleanup; }
  if (fib->fib_DirEntryType > 0)
  { error = ERROR_OBJECT_WRONG_TYPE; goto cleanup; }
  if (fib->fib_Size < 0) { error = ERROR_BAD_NUMBER; goto cleanup; }

  if (fib->fib_Size)
  {
    data = AllocVec(fib->fib_Size, MEMF_ANY);
    if (!data) { error = ERROR_NO_FREE_STORE; goto cleanup; }
  }
  in = OpenFromLock(lock_in);
  if (!in) { error = IoErr(); goto cleanup; }
  lock_in = BNULL; /* Only a successful conversion consumes the lock. */
  while (length < fib->fib_Size)
  {
    LONG count = Read(in, data + length, fib->fib_Size - length);
    if (count < 0) { error = IoErr(); goto cleanup; }
    if (!count) break;
    length += count;
  }
  /* Preserve the destination until input access and reading have succeeded. */
  if (!Close(in)) { in = BNULL; error = IoErr(); goto cleanup; }
  in = BNULL;
  out = Open((STRPTR)args[ARG_TO], MODE_NEWFILE);
  if (!out) { error = IoErr(); goto cleanup; }
  if (length)
  {
    struct sorted_data *sd = sort(data, length,
      (STRPTR)args[ARG_COLSTART], (BOOL)args[ARG_CASE], (BOOL)args[ARG_NUMERIC]);
    error = sd ? write_data(sd, out) : ERROR_NO_FREE_STORE;
  }

cleanup:
  /* Keep the first failure: cleanup calls may replace IoErr(). */
  if (in && !Close(in) && !error) error = IoErr();
  if (out && !Close(out) && !error) error = IoErr();
  if (lock_in) UnLock(lock_in);
  if (data) FreeVec(data);
  if (fib) FreeDosObject(DOS_FIB, fib);
  if (rda) FreeArgs(rda);
  CloseLocale(locale);
  if (error)
  {
    PrintFault(error, "Sort");
    return RETURN_FAIL;
  }
  return RETURN_OK;
}
