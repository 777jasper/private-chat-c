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
