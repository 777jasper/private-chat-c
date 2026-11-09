#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

/* Size of the buffer that holds a hashed password (text plus the final 00). */
#define AUTH_HASH_SIZE 128

/* Initializes libsodium. Call once at startup. Returns 0 on success, -1 on failure. */
int auth_init(void);

/* Hashes password into out. out_size must be at least AUTH_HASH_SIZE. Returns 0 on success, -1 on failure. */
int auth_hash_password(const char *password, char *out, size_t out_size);

/* Checks password against a stored hash. Returns 0 if it matches, -1 if it does not (or on error). */
int auth_verify_password(const char *stored_hash, const char *password);

/* Runs a password check against a fake hash and discards the result.
   Call it when a username doesn't exist, so that login takes the same
   time whether or not the user exists. */
void auth_dummy_verify(const char *password);

#endif

