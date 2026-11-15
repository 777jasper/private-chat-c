#include <string.h>
#include <sodium.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "auth.h"

/* Compile-time safety check: our buffer size must be big enough for libsodium. */
_Static_assert(AUTH_HASH_SIZE >= crypto_pwhash_STRBYTES,
               "AUTH_HASH_SIZE is too small for libsodium");

/* A valid hash of a throwaway password, made once at startup. */
static char dummy_hash[AUTH_HASH_SIZE];

static unsigned char server_pk[crypto_box_PUBLICKEYBYTES];
static unsigned char server_sk[crypto_box_SECRETKEYBYTES];

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

int auth_server_keys_init(const char *sk_path, const char *pk_path){

	int fd = open(sk_path, O_RDONLY);

	if(fd >= 0){
		/* the key file exists, so read it */
		ssize_t n = read(fd, server_sk, sizeof server_sk);
                close(fd);
                if (n != (ssize_t)sizeof server_sk) {
			fprintf(stderr, "server: damaged key file %s\n", sk_path);
			sodium_memzero(server_sk, sizeof server_sk);
			return -1;
		}
		crypto_scalarmult_base(server_pk, server_sk);
	}
	else if(errno == ENOENT){
		/* first run, so make a keypair and save the secret half */
		crypto_box_keypair(server_pk, server_sk);
		fd = open(sk_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd == -1) {
			perror("server: create key file");
			sodium_memzero(server_sk, sizeof server_sk);
			return -1;
		}
		ssize_t w = write(fd, server_sk, sizeof server_sk);
                close(fd);
                if (w != (ssize_t)sizeof server_sk) {
			unlink(sk_path);
                        sodium_memzero(server_sk, sizeof server_sk);
                        return -1;
		}
	}
	else{
		perror("server: open key file");
		return -1;
	}

	/* write the public key (overwrite is fine, it is derived) */
	fd = open(pk_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if(fd == -1){
		perror("server: write public key");
		sodium_memzero(server_sk, sizeof server_sk);
		return -1;
	}
	ssize_t pw = write(fd, server_pk, sizeof server_pk);
	close(fd);
	if(pw != (ssize_t)sizeof server_pk){
		sodium_memzero(server_sk, sizeof server_sk);
		return -1;
	}
	return 0;
}

void auth_cleanup(void){
	sodium_memzero(server_sk, sizeof server_sk);
}

void auth_random_bytes(unsigned char *buf, size_t n){

    randombytes_buf(buf, n);
}

int auth_open_sealed(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len){

	if (in_len < crypto_box_SEALBYTES)
		return -1;

	size_t p_len = in_len - crypto_box_SEALBYTES;
	uint8_t *buf = malloc(p_len > 0 ? p_len : 1);
	if (buf == NULL)
		return -1;

	if (crypto_box_seal_open(buf, in, in_len, server_pk, server_sk) != 0){
		sodium_memzero(buf, p_len);
		free(buf);
		return -1;
	}

	*out = buf;
	*out_len = p_len;
	return 0;
}

void auth_free_secret(uint8_t *p, size_t n){
	if(p!=NULL){
		sodium_memzero(p, n);
		free(p);
	}
}
