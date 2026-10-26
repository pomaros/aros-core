/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "__pipe_name.h"
#define N 10000
#define THREADS 8
static char names[N * THREADS][POSIXC_PIPE_NAME_SIZE];
static void *worker(void *opaque)
{
 size_t base=(size_t)opaque*N;
 for(size_t i=0;i<N;i++) __posixc_pipe_name(names[base+i], ULONG_MAX);
 return NULL;
}
static int compare(const void *a,const void *b) { return strcmp(a,b); }
int main(void)
{
 pthread_t threads[THREADS];
 for(size_t i=0;i<THREADS;i++) assert(!pthread_create(&threads[i],NULL,worker,(void *)i));
 for(size_t i=0;i<THREADS;i++) assert(!pthread_join(threads[i],NULL));
 qsort(names,N*THREADS,sizeof(names[0]),compare);
 for(size_t i=0;i<N*THREADS;i++) {
  char *end; const char *p=names[i]+strlen("PIPE:cpipe-");
  assert(strncmp(names[i],"PIPE:cpipe-",strlen("PIPE:cpipe-"))==0);
  assert(strtoul(p,&end,16)==ULONG_MAX && *end=='-');
  unsigned long seq=strtoul(end+1,&end,10);
  assert(*end==0 && seq>0 && seq<=N*THREADS);
  if(i) assert(strcmp(names[i-1],names[i]));
 }
 puts("PASS 80000 unique full-width names from 8 concurrent callers");
}
