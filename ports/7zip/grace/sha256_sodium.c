#include "crypto_gate.h"

#include "Sha256.h"

#include <sodium/core.h>
#include <sodium/crypto_hash_sha256.h>
#include <string.h>

typedef char grace_sha256_state_size_check[
    (sizeof(CSha256) >= sizeof(crypto_hash_sha256_state)) ? 1 : -1
];

static int g_sodium_ready;

static void grace_sha256_prepare(void)
{
    if (!g_sodium_ready) {
        sodium_init();
        g_sodium_ready = 1;
    }
}

static crypto_hash_sha256_state *grace_sha256_state(CSha256 *p)
{
    return (crypto_hash_sha256_state *)(void *)p;
}

BoolInt Sha256_SetFunction(CSha256 *p, unsigned algo)
{
    (void)p;
    (void)algo;
    return 1;
}

void Sha256_InitState(CSha256 *p)
{
    Sha256_Init(p);
}

void Sha256_Init(CSha256 *p)
{
    grace_sha256_prepare();
    if (!grace_crypto_permitted()) {
        memset(p, 0, sizeof(*p));
        return;
    }
    (void) crypto_hash_sha256_init(grace_sha256_state(p));
}

void Sha256_Update(CSha256 *p, const Byte *data, size_t size)
{
    if (!grace_crypto_permitted()) {
        return;
    }
    (void) crypto_hash_sha256_update(grace_sha256_state(p), data, (unsigned long long) size);
}

void Sha256_Final(CSha256 *p, Byte *digest)
{
    if (!grace_crypto_permitted()) {
        memset(digest, 0, SHA256_DIGEST_SIZE);
        return;
    }
    (void) crypto_hash_sha256_final(grace_sha256_state(p), digest);
}

void Sha256Prepare(void)
{
    grace_sha256_prepare();
}
