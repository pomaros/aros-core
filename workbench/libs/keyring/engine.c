/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#include "engine.h"
#include "vendor/argon2/include/argon2.h"
#include <mbedtls/chachapoly.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 64u
#define TAG 16u
struct entry {
  unsigned char owner[32];
  char name[128];
  size_t size;
  unsigned char value[4096];
};
struct kr_engine {
  kr_random_fn random;
  void *context;
  int unlocked;
  unsigned char key[32], salt[16];
  size_t count;
  struct entry entries[64];
};
static void wipe(void *p, size_t n) {
  volatile unsigned char *v = p;
  while (n--)
    *v++ = 0;
}
void kr_engine_dispose(void *p, size_t n) {
  if (p) {
    wipe(p, n);
    free(p);
  }
}
struct kr_engine *kr_engine_new(kr_random_fn fn, void *ctx) {
  struct kr_engine *e = calloc(1, sizeof(*e));
  if (e) {
    e->random = fn;
    e->context = ctx;
  }
  return e;
}
struct kr_engine *kr_engine_clone(const struct kr_engine *e) {
  struct kr_engine *n;
  if (!e)
    return NULL;
  n = malloc(sizeof(*n));
  if (n)
    memcpy(n, e, sizeof(*n));
  return n;
}
void kr_engine_lock(struct kr_engine *e) {
  if (e) {
    wipe(e->key, 32);
    wipe(e->salt, 16);
    wipe(e->entries, sizeof(e->entries));
    e->count = 0;
    e->unlocked = 0;
  }
}
void kr_engine_free(struct kr_engine *e) { kr_engine_dispose(e, sizeof(*e)); }
int kr_engine_is_locked(const struct kr_engine *e) {
  return !e || !e->unlocked;
}
static void put32(unsigned char *p, uint32_t n) {
  p[0] = (unsigned char)n;
  p[1] = (unsigned char)(n >> 8);
  p[2] = (unsigned char)(n >> 16);
  p[3] = (unsigned char)(n >> 24);
}
static uint32_t get32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static size_t namelen(const char *n) {
  size_t i;
  if (!n)
    return 0;
  for (i = 0; i <= KR_NAME_MAX; i++)
    if (!n[i])
      return i;
  return 0;
}
static int find(const struct kr_engine *e, const unsigned char *o,
                const char *n) {
  size_t i;
  for (i = 0; i < e->count; i++)
    if (!memcmp(e->entries[i].owner, o, 32) && !strcmp(e->entries[i].name, n))
      return (int)i;
  return -1;
}
int kr_engine_store(struct kr_engine *e, const unsigned char o[32],
                    const char *n, const void *v, size_t s) {
  int i;
  size_t l;
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  l = namelen(n);
  if (!o || !l || s > KR_VALUE_MAX || (!v && s))
    return KR_BOUNDS;
  i = find(e, o, n);
  if (i < 0) {
    if (e->count == KR_ENTRY_MAX)
      return KR_BOUNDS;
    i = (int)e->count++;
  }
  wipe(&e->entries[i], sizeof(struct entry));
  memcpy(e->entries[i].owner, o, 32);
  memcpy(e->entries[i].name, n, l);
  if (s)
    memcpy(e->entries[i].value, v, s);
  e->entries[i].size = s;
  return 0;
}
int kr_engine_fetch(const struct kr_engine *e, const unsigned char o[32],
                    const char *n, void *v, size_t *s) {
  int i;
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  if (!o || !namelen(n) || !s)
    return KR_BOUNDS;
  i = find(e, o, n);
  if (i < 0)
    return KR_NOTFOUND;
  if (*s < e->entries[i].size || (!v && e->entries[i].size)) {
    *s = e->entries[i].size;
    return KR_BOUNDS;
  }
  *s = e->entries[i].size;
  if (*s)
    memcpy(v, e->entries[i].value, *s);
  return 0;
}
int kr_engine_delete(struct kr_engine *e, const unsigned char o[32],
                     const char *n) {
  int i;
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  if (!o || !namelen(n))
    return KR_BOUNDS;
  i = find(e, o, n);
  if (i < 0)
    return KR_NOTFOUND;
  wipe(&e->entries[i], sizeof(struct entry));
  if ((size_t)i < e->count - 1)
    memmove(&e->entries[i], &e->entries[i + 1],
            (e->count - (size_t)i - 1) * sizeof(struct entry));
  e->count--;
  wipe(&e->entries[e->count], sizeof(struct entry));
  return 0;
}
int kr_engine_enumerate(const struct kr_engine *e, const unsigned char o[32],
                        size_t index, char *n, size_t *s) {
  size_t i, l;
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  if (!o || !s)
    return KR_BOUNDS;
  for (i = 0; i < e->count; i++)
    if (!memcmp(o, e->entries[i].owner, 32)) {
      if (index) {
        index--;
        continue;
      }
      l = strlen(e->entries[i].name) + 1;
      if (*s < l || !n) {
        *s = l;
        return KR_BOUNDS;
      }
      memcpy(n, e->entries[i].name, l);
      *s = l;
      return 0;
    }
  return KR_NOTFOUND;
}
static int encode(const struct kr_engine *e, unsigned char **out,
                  size_t *size) {
  size_t i, n = 4, pos = 4;
  unsigned char *p;
  for (i = 0; i < e->count; i++)
    n += 40 + strlen(e->entries[i].name) + e->entries[i].size;
  p = malloc(n);
  if (!p)
    return KR_NOMEM;
  put32(p, (uint32_t)e->count);
  for (i = 0; i < e->count; i++) {
    const struct entry *v = &e->entries[i];
    size_t l = strlen(v->name);
    memcpy(p + pos, v->owner, 32);
    put32(p + pos + 32, (uint32_t)l);
    put32(p + pos + 36, (uint32_t)v->size);
    pos += 40;
    memcpy(p + pos, v->name, l);
    pos += l;
    memcpy(p + pos, v->value, v->size);
    pos += v->size;
  }
  *out = p;
  *size = n;
  return 0;
}
static int decode(struct kr_engine *e, const unsigned char *p, size_t s) {
  size_t n, i, pos = 4;
  if (s < 4)
    return KR_FORMAT;
  n = get32(p);
  if (n > KR_ENTRY_MAX)
    return KR_FORMAT;
  for (i = 0; i < n; i++) {
    size_t l, v;
    char name[128];
    int r;
    if (s - pos < 40)
      return KR_FORMAT;
    l = get32(p + pos + 32);
    v = get32(p + pos + 36);
    if (!l || l > KR_NAME_MAX || v > KR_VALUE_MAX || s - pos - 40 < l + v)
      return KR_FORMAT;
    memcpy(name, p + pos + 40, l);
    name[l] = 0;
    if (memchr(name, 0, l) || find(e, p + pos, name) >= 0)
      return KR_FORMAT;
    r = kr_engine_store(e, p + pos, name, p + pos + 40 + l, v);
    if (r)
      return r;
    pos += 40 + l + v;
  }
  return pos == s ? 0 : KR_FORMAT;
}
int kr_engine_unlock(struct kr_engine *e, const void *pass, size_t ps,
                     const void *sealed, size_t ss) {
  const unsigned char *h = sealed;
  unsigned char *plain = NULL;
  size_t n = 0;
  int r = KR_FORMAT;
  mbedtls_chachapoly_context c;
  if (!e || e->unlocked || !pass || !ps || ps > 1024)
    return KR_BOUNDS;
  if (!h) {
    if (ss)
      return KR_FORMAT;
    if (!e->random || e->random(e->context, e->salt, 16))
      return KR_ENTROPY;
  } else {
    if (ss < HEADER + TAG + 4 || ss > KR_VAULT_MAX ||
        memcmp(h, "AROSKR01", 8) || get32(h + 8) != 1 ||
        get32(h + 12) != 65536 || get32(h + 16) != 3 || get32(h + 20) != 1 ||
        get32(h + 24) != 19 || get32(h + 28) != ss - HEADER - TAG)
      return KR_FORMAT;
    memcpy(e->salt, h + 32, 16);
    n = ss - HEADER - TAG;
  }
  if (argon2id_hash_raw(3, 65536, 1, pass, ps, e->salt, 16, e->key, 32) !=
      ARGON2_OK) {
    r = KR_NOMEM;
    goto fail;
  }
  if (h) {
    plain = malloc(n);
    if (!plain) {
      r = KR_NOMEM;
      goto fail;
    }
    mbedtls_chachapoly_init(&c);
    r = mbedtls_chachapoly_setkey(&c, e->key);
    if (!r)
      r = mbedtls_chachapoly_auth_decrypt(&c, n, h + 48, h, HEADER,
                                          h + HEADER + n, h + HEADER, plain);
    mbedtls_chachapoly_free(&c);
    if (r) {
      r = KR_AUTH;
      goto fail;
    }
  }
  e->unlocked = 1;
  if (h) {
    r = decode(e, plain, n);
    if (r)
      goto fail;
  }
  kr_engine_dispose(plain, n);
  return 0;
fail:
  kr_engine_dispose(plain, n);
  kr_engine_lock(e);
  return r;
}
int kr_engine_seal(struct kr_engine *e, unsigned char **out, size_t *size) {
  unsigned char *p = NULL, *h;
  size_t n = 0;
  int r;
  mbedtls_chachapoly_context c;
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  if (!out || !size)
    return KR_BOUNDS;
  *out = NULL;
  *size = 0;
  r = encode(e, &p, &n);
  if (r)
    return r;
  h = calloc(1, HEADER + n + TAG);
  if (!h) {
    kr_engine_dispose(p, n);
    return KR_NOMEM;
  }
  memcpy(h, "AROSKR01", 8);
  put32(h + 8, 1);
  put32(h + 12, 65536);
  put32(h + 16, 3);
  put32(h + 20, 1);
  put32(h + 24, 19);
  put32(h + 28, (uint32_t)n);
  memcpy(h + 32, e->salt, 16);
  if (!e->random || e->random(e->context, h + 48, 12)) {
    r = KR_ENTROPY;
    goto done;
  }
  mbedtls_chachapoly_init(&c);
  r = mbedtls_chachapoly_setkey(&c, e->key);
  if (!r)
    r = mbedtls_chachapoly_encrypt_and_tag(&c, n, h + 48, h, HEADER, p,
                                           h + HEADER, h + HEADER + n);
  mbedtls_chachapoly_free(&c);
  if (r)
    r = KR_AUTH;
done:
  kr_engine_dispose(p, n);
  if (r) {
    kr_engine_dispose(h, HEADER + n + TAG);
    return r;
  }
  *out = h;
  *size = HEADER + n + TAG;
  return 0;
}
#ifdef KR_ENGINE_TEST_UNSEALED
int kr_engine_test_unsealed(const struct kr_engine *e, unsigned char **p,
                            size_t *s) {
  if (kr_engine_is_locked(e))
    return KR_LOCKED;
  return encode(e, p, s);
}
#endif
