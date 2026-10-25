/* Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1 */
#include "engine.h"
#include "vendor/argon2/include/argon2.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned counter;
static int random_bytes(void *ctx, unsigned char *p, size_t n) {
  size_t i;
  (void)ctx;
  for (i = 0; i < n; i++)
    p[i] = (unsigned char)++counter;
  return 0;
}
static int failed_random(void *ctx, unsigned char *p, size_t n) {
  (void)ctx;
  (void)p;
  (void)n;
  return -1;
}
static int contains(const unsigned char *p, size_t n, const char *m) {
  size_t i, l = strlen(m);
  for (i = 0; i + l <= n; i++)
    if (!memcmp(p + i, m, l))
      return 1;
  return 0;
}

/* Argon2id19 official known-answer vector: P-H-C/phc-winner-argon2
 * 62358ba2123abd17fccf2a108a301d4b52c01a7c, kats/argon2id.
 * KAT file SHA256:
 * ba05643e504fc5778dda99e2d9f42ebe7d22ebb3923cc719fd591b1b14a8d28d Parameters
 * and expected digest are test facts; test code is original APL.
 */
static void argon2_known_answer(void) {
  static const unsigned char expected[32] = {
      0x0d, 0x64, 0x0d, 0xf5, 0x8d, 0x78, 0x76, 0x6c, 0x08, 0xc0, 0x37,
      0xa3, 0x4a, 0x8b, 0x53, 0xc9, 0xd0, 0x1e, 0xf0, 0x45, 0x2d, 0x75,
      0xb6, 0x5e, 0xb5, 0x25, 0x20, 0xe9, 0x6b, 0x01, 0xe6, 0x59};
  unsigned char password[32], salt[16], secret[8], ad[12], digest[32];
  argon2_context context;
  memset(password, 1, sizeof(password));
  memset(salt, 2, sizeof(salt));
  memset(secret, 3, sizeof(secret));
  memset(ad, 4, sizeof(ad));
  memset(&context, 0, sizeof(context));
  context.out = digest;
  context.outlen = sizeof(digest);
  context.pwd = password;
  context.pwdlen = sizeof(password);
  context.salt = salt;
  context.saltlen = sizeof(salt);
  context.secret = secret;
  context.secretlen = sizeof(secret);
  context.ad = ad;
  context.adlen = sizeof(ad);
  context.t_cost = 3;
  context.m_cost = 32;
  context.lanes = 4;
  context.threads = 1;
  context.version = ARGON2_VERSION_13;
  assert(argon2_ctx(&context, Argon2_id) == ARGON2_OK);
  assert(!memcmp(digest, expected, sizeof(expected)));
}

static void maximal_vault(void) {
  struct kr_engine *a = kr_engine_new(random_bytes, NULL),
                   *b = kr_engine_new(random_bytes, NULL), *candidate;
  unsigned char owner[32] = {9}, value[KR_VALUE_MAX], *sealed;
  char name[KR_NAME_MAX + 1], renamed[KR_NAME_MAX + 1],
      enumerated[KR_NAME_MAX + 1];
  size_t n, cap;
  int i;
  assert(a && b);
  memset(value, 0x5a, sizeof(value));
  assert(!kr_engine_unlock(a, "max vault", 9, NULL, 0));
  for (i = 0; i < KR_ENTRY_MAX; i++) {
    memset(name, 'x', KR_NAME_MAX);
    snprintf(name, 4, "%03d", i);
    name[3] = 'x';
    name[KR_NAME_MAX] = 0;
    assert(!kr_engine_store(a, owner, name, value, sizeof(value)));
  }
  assert(!kr_engine_seal(a, &sealed, &n));
  assert(n == KR_VAULT_MAX);
  assert(!kr_engine_unlock(b, "max vault", 9, sealed, n));
  cap = sizeof(value);
  assert(!kr_engine_fetch(b, owner, name, value, &cap));
  assert(cap == KR_VALUE_MAX);
  kr_engine_dispose(sealed, n);
  candidate = kr_engine_clone(a);
  assert(candidate);
  memcpy(renamed, name, sizeof(renamed));
  memcpy(renamed, "new", 3);
  /* A new entry exceeds capacity before deleting on the isolated candidate. */
  assert(kr_engine_store(candidate, owner, renamed, value, sizeof(value)) ==
         KR_BOUNDS);
  assert(!kr_engine_delete(candidate, owner, name));
  assert(!kr_engine_store(candidate, owner, renamed, value, sizeof(value)));
  cap = sizeof(value);
  assert(!kr_engine_fetch(a, owner, name, value, &cap));
  assert(kr_engine_fetch(a, owner, renamed, value, &cap) == KR_NOTFOUND);
  assert(kr_engine_fetch(candidate, owner, name, value, &cap) == KR_NOTFOUND);
  assert(!kr_engine_fetch(candidate, owner, renamed, value, &cap));
  assert(!kr_engine_seal(candidate, &sealed, &n));
  assert(n == KR_VAULT_MAX);
  kr_engine_lock(b);
  assert(!kr_engine_unlock(b, "max vault", 9, sealed, n));
  cap = sizeof(value);
  assert(!kr_engine_fetch(b, owner, renamed, value, &cap));
  assert(kr_engine_fetch(b, owner, name, value, &cap) == KR_NOTFOUND);
  cap = sizeof(enumerated);
  assert(!kr_engine_enumerate(b, owner, 63, enumerated, &cap));
  assert(!strcmp(enumerated, renamed));
  cap = sizeof(enumerated);
  assert(kr_engine_enumerate(b, owner, 64, enumerated, &cap) == KR_NOTFOUND);
  kr_engine_dispose(sealed, n);
  kr_engine_free(candidate);
  kr_engine_free(a);
  kr_engine_free(b);
}

int main(int argc, char **argv) {
  argon2_known_answer();
  maximal_vault();
  struct kr_engine *e = kr_engine_new(random_bytes, NULL),
                   *f = kr_engine_new(random_bytes, NULL), *clone;
  char marker[41];
  unsigned char owner[32] = {1}, other[32] = {2}, buf[4096], *sealed, *plain,
                *second;
  size_t n, s, ps, ns;
  char name[128];
  FILE *file;
  int i;
  for (i = 0; i < 40; i++)
    marker[i] = (char)('A' + (i * 7 + 3) % 26);
  marker[40] = 0;
  assert(argc == 3);
  assert(e && f);
  assert(kr_engine_is_locked(e));
  s = sizeof(buf);
  assert(kr_engine_fetch(e, owner, "wifi", buf, &s) == KR_LOCKED);
  assert(kr_engine_store(e, owner, "wifi", marker, strlen(marker)) ==
         KR_LOCKED);
  assert(!kr_engine_unlock(e, "physical host pass", 18, NULL, 0));
  assert(!kr_engine_store(e, owner, "wifi", marker, strlen(marker)));
  s = sizeof(buf);
  assert(!kr_engine_fetch(e, owner, "wifi", buf, &s));
  assert(s == strlen(marker) && !memcmp(buf, marker, s));
  s = sizeof(buf);
  assert(kr_engine_fetch(e, other, "wifi", buf, &s) == KR_NOTFOUND);
  assert(kr_engine_delete(e, other, "wifi") == KR_NOTFOUND);
  s = sizeof(name);
  assert(kr_engine_enumerate(e, other, 0, name, &s) == KR_NOTFOUND);
  s = sizeof(name);
  assert(!kr_engine_enumerate(e, owner, 0, name, &s) && !strcmp(name, "wifi"));
  s = 1;
  assert(kr_engine_fetch(e, owner, "wifi", buf, &s) == KR_BOUNDS &&
         s == strlen(marker));
  assert(kr_engine_store(e, owner, "bad", buf, 4097) == KR_BOUNDS);
  memset(name, 'x', sizeof(name));
  assert(kr_engine_store(e, owner, name, buf, 1) == KR_BOUNDS);
  assert(!kr_engine_store(e, owner, "binary", "\0\xff\x80\0", 4));
  s = sizeof(buf);
  assert(!kr_engine_fetch(e, owner, "binary", buf, &s));
  assert(s == 4 && !memcmp(buf, "\0\xff\x80\0", 4));
  assert(!kr_engine_delete(e, owner, "binary"));
  assert(!kr_engine_seal(e, &sealed, &n));
  assert(!contains(sealed, n, marker));
  assert(!kr_engine_test_unsealed(e, &plain, &ps));
  assert(contains(plain, ps, marker));
  file = fopen(argv[2], "wb");
  assert(file && fwrite(plain, 1, ps, file) == ps && !fclose(file));
  assert(!kr_engine_seal(e, &second, &ns));
  assert(n == ns && memcmp(sealed, second, n));
  kr_engine_dispose(second, ns);
  file = fopen(argv[1], "wb");
  assert(file && fwrite(sealed, 1, n, file) == n && !fclose(file));
  assert(!kr_engine_unlock(f, "physical host pass", 18, sealed, n));
  s = sizeof(buf);
  assert(!kr_engine_fetch(f, owner, "wifi", buf, &s) && s == strlen(marker));
  clone = kr_engine_clone(f);
  assert(clone);
  assert(!kr_engine_delete(clone, owner, "wifi"));
  s = sizeof(buf);
  assert(!kr_engine_fetch(f, owner, "wifi", buf, &s));
  kr_engine_free(clone);
  kr_engine_lock(f);
  assert(kr_engine_is_locked(f));
  assert(kr_engine_unlock(f, "wrong", 5, sealed, n) == KR_AUTH &&
         kr_engine_is_locked(f));
  sealed[n - 1] ^= 1;
  assert(kr_engine_unlock(f, "physical host pass", 18, sealed, n) == KR_AUTH &&
         kr_engine_is_locked(f));
  sealed[n - 1] ^= 1;
  sealed[12] ^= 1;
  assert(kr_engine_unlock(f, "physical host pass", 18, sealed, n) == KR_FORMAT);
  sealed[12] ^= 1;
  assert(kr_engine_unlock(f, "physical host pass", 18, sealed, n - 1) ==
         KR_FORMAT);
  for (i = 1; i < 64; i++) {
    snprintf(name, sizeof(name), "entry%d", i);
    assert(!kr_engine_store(e, owner, name, buf, sizeof(buf)));
  }
  assert(kr_engine_store(e, owner, "overflow", buf, 1) == KR_BOUNDS);
  assert(!kr_engine_delete(e, owner, "entry63"));
  memset(name, 'y', 127);
  name[127] = 0;
  assert(!kr_engine_store(e, owner, name, buf, sizeof(buf)));
  assert(!kr_engine_seal(e, &second, &ns));
  assert(ns <= KR_VAULT_MAX);
  kr_engine_lock(f);
  assert(!kr_engine_unlock(f, "physical host pass", 18, second, ns));
  kr_engine_dispose(second, ns);
  kr_engine_free(f);
  f = kr_engine_new(failed_random, NULL);
  assert(!kr_engine_unlock(f, "physical host pass", 18, sealed, n));
  assert(kr_engine_seal(f, &second, &ns) == KR_ENTROPY);
  s = sizeof(buf);
  assert(!kr_engine_fetch(f, owner, "wifi", buf, &s));
  kr_engine_lock(f);
  assert(kr_engine_unlock(f, "pass", 4, NULL, 0) == KR_ENTROPY);
  kr_engine_free(f);
  kr_engine_dispose(sealed, n);
  kr_engine_dispose(plain, ps);
  kr_engine_free(e);
  printf("{\"result\":\"PASS\",\"sanitizers\":[\"ASan\",\"UBSan\"],\"storage\":"
         "true,\"owners\":true,\"locked\":true,\"sealed_marker_absent\":true,"
         "\"unsealed_positive_control\":true,\"tamper\":true,\"bad_"
         "passphrase\":true,\"bounds\":true,\"entropy_failure\":true}\n");
  return 0;
}
