#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

/* Size of the buffer that holds a hashed password (text plus the final 00). */
#define AUTH_HASH_SIZE 128

/* Initializes libsodium. Call once at startup. Returns 0 on success, -1 on failure. */
int auth_init(void);

/* Hashes password into out. out_size must be at least AUTH_HASH_SIZE. Returns 0 on success, -1 on failure. */
int auth_hash_password(const char *password, char *out, size_t out_size);

#endif

