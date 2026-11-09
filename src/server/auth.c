#include <string.h>
#include <sodium.h>
#include "auth.h"

/* Compile-time safety check: our buffer size must be big enough for libsodium. */
_Static_assert(AUTH_HASH_SIZE >= crypto_pwhash_STRBYTES,
               "AUTH_HASH_SIZE is too small for libsodium");

int auth_init(void){
	if(sodium_init() < 0)
		return -1;
	return 0;
}

int auth_hash_password(const char *password, char *out, size_t out_size){
	if(out_size < AUTH_HASH_SIZE)
		return -1;
	if(crypto_pwhash_str(out, password, strlen(password),crypto_pwhash_OPSLIMIT_INTERACTIVE,crypto_pwhash_MEMLIMIT_INTERACTIVE)!=0)
		return -1;
	return 0;
}
