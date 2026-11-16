#include <stdlib.h>
#include <string.h>
#include "msgcrypt.h"

int msgcrypt_encrypt(const uint8_t *plain, size_t plain_len, const uint8_t their_pk[crypto_box_PUBLICKEYBYTES],
		const uint8_t my_sk[crypto_box_SECRETKEYBYTES], uint8_t **out, size_t *out_len){

	if(plain_len > MSGCRYPT_MAX_PLAIN)
		return -1;

	size_t total = MSGCRYPT_OVERHEAD + plain_len;

	uint8_t *buf = malloc(total);
	if(buf == NULL)
		return -1;

	/* put a fresh random nonce at the start of the buf */
	randombytes_buf(buf, crypto_box_NONCEBYTES);

	/* encrypt into the space after the nonce */
	if(crypto_box_easy(buf + crypto_box_NONCEBYTES, plain, plain_len, buf, their_pk, my_sk)!=0){
		free(buf);
		return -1;
	}

	*out = buf;
	*out_len = total;
	return 0;
}

int msgcrypt_decrypt(const uint8_t *in, size_t in_len, const uint8_t their_pk[crypto_box_PUBLICKEYBYTES],
		const uint8_t my_sk[crypto_box_SECRETKEYBYTES], uint8_t **plain, size_t *plain_len){

	if(in_len < MSGCRYPT_OVERHEAD)
		return -1;

	size_t c_len = in_len - crypto_box_NONCEBYTES;
	size_t p_len = c_len - crypto_box_MACBYTES;

	uint8_t *buf = malloc(p_len + 1);
	if(buf == NULL)
		return -1;

	/* decrypt */
	if(crypto_box_open_easy(buf, in + crypto_box_NONCEBYTES, c_len, in, their_pk, my_sk) != 0){
		free(buf);
		return -1;
	}

	buf[p_len] = 0;
	*plain = buf;
	*plain_len = p_len;
	return 0;
}

int msgcrypt_seal_credentials(const uint8_t challenge[MSGCRYPT_CHALLENGE_SIZE],
		const char *user, const char *pass, const uint8_t server_pk[crypto_box_PUBLICKEYBYTES],
		uint8_t **out, size_t *out_len){

	size_t u = strlen(user);
	size_t p = strlen(pass);
	size_t plain_len = MSGCRYPT_CHALLENGE_SIZE + u + 1 + p + 1;
	if(plain_len > MSGCRYPT_MAX_PLAIN)
		return -1;
	uint8_t *plain = malloc(plain_len);
	if(plain == NULL)
		return -1;
	size_t total = plain_len + crypto_box_SEALBYTES;
	uint8_t *buf = malloc(total);
	if(buf == NULL){
		sodium_memzero(plain, plain_len);
		free(plain);
		return -1;
	}

	/* fill plain */
	memcpy(plain, challenge, MSGCRYPT_CHALLENGE_SIZE);
	memcpy(plain + MSGCRYPT_CHALLENGE_SIZE, user, u + 1);
	memcpy(plain + MSGCRYPT_CHALLENGE_SIZE + u + 1, pass, p + 1);

	int rc = crypto_box_seal(buf, plain, plain_len, server_pk);
	sodium_memzero(plain, plain_len);
	free(plain);

	if(rc != 0){
		free(buf);
		return -1;
	}
	*out = buf;
	*out_len = total;
	return 0;
}

