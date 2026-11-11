#ifndef KEYS_H
#define KEYS_H

#include <sodium.h>

/* Loads this user's keypair from ~/.chatapp/<username>.key, or creates and
   saves a new one if the file doesn't exist. sodium_init() must already
   have been called. Returns 0 on success, -1 on failure. */
int keys_load_or_create(const char *username, unsigned char pk[crypto_box_PUBLICKEYBYTES], unsigned char sk[crypto_box_SECRETKEYBYTES]);

#endif /* KEYS_H */

