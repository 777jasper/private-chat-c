#include <string.h>
#include <sodium.h>
#include "auth.h"

/* Compile-time safety check: our buffer size must be big enough for libsodium. */
_Static_assert(AUTH_HASH_SIZE >= crypto_pwhash_STRBYTES,
               "AUTH_HASH_SIZE is too small for libsodium");

/* A valid hash of a throwaway password, made once at startup. */
static char dummy_hash[AUTH_HASH_SIZE];

int auth_init(void){
	if(sodium_init() < 0)
		return -1;

	if(crypto_pwhash_str(dummy_hash, "not-a-real-password", strlen("not-a-real-password"), crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
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


int auth_verify_password(const char *stored_hash, const char *password){

	if(crypto_pwhash_str_verify(stored_hash, password, strlen(password)) == 0)
		return 0;
	return -1;
}

void auth_dummy_verify(const char *password){
	int rc = crypto_pwhash_str_verify(dummy_hash, password, strlen(password));
	(void)rc;
}
