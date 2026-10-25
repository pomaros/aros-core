/*
 * Copyright (C) 2026 John Knipper
 * Licensed under the AROS Public License (APL) Version 1.1
 */
#ifndef KEYRING_ENGINE_H
#define KEYRING_ENGINE_H
#include <stddef.h>
#include <stdint.h>
#define KR_OWNER_BYTES 32
#define KR_NAME_MAX 127
#define KR_VALUE_MAX 4096
#define KR_ENTRY_MAX 64
#define KR_VAULT_MAX                                                           \
  (64u + 16u + 4u + KR_ENTRY_MAX * (32u + 8u + KR_NAME_MAX + KR_VALUE_MAX))
enum {
  KR_OK = 0,
  KR_LOCKED = -1,
  KR_AUTH = -2,
  KR_BOUNDS = -3,
  KR_OWNER = -4,
  KR_NOTFOUND = -5,
  KR_NOMEM = -6,
  KR_ENTROPY = -7,
  KR_FORMAT = -8
};
typedef int (*kr_random_fn)(void *context, unsigned char *out, size_t size);
struct kr_engine;
struct kr_engine *kr_engine_new(kr_random_fn random, void *context);
struct kr_engine *kr_engine_clone(const struct kr_engine *source);
void kr_engine_free(struct kr_engine *engine);
void kr_engine_lock(struct kr_engine *engine);
int kr_engine_is_locked(const struct kr_engine *engine);
/* NULL sealed/zero length creates an empty vault. Existing unlock is rejected.
 */
int kr_engine_unlock(struct kr_engine *engine, const void *pass,
                     size_t pass_size, const void *sealed, size_t sealed_size);
/* Caller owns returned bytes; kr_engine_dispose wipes and frees them. */
int kr_engine_seal(struct kr_engine *engine, unsigned char **sealed,
                   size_t *size);
void kr_engine_dispose(void *bytes, size_t size);
int kr_engine_store(struct kr_engine *, const unsigned char owner[32],
                    const char *name, const void *value, size_t size);
/* *size is capacity on entry, actual/required size on success/KR_BOUNDS. */
int kr_engine_fetch(const struct kr_engine *, const unsigned char owner[32],
                    const char *name, void *value, size_t *size);
int kr_engine_delete(struct kr_engine *, const unsigned char owner[32],
                     const char *name);
int kr_engine_enumerate(const struct kr_engine *, const unsigned char owner[32],
                        size_t index, char *name, size_t *size);
#ifdef KR_ENGINE_TEST_UNSEALED
int kr_engine_test_unsealed(const struct kr_engine *, unsigned char **bytes,
                            size_t *size);
#endif
#endif
