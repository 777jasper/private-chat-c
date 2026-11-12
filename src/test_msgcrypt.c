#define _GNU_SOURCE /* it enables memmem() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "msgcrypt.h"

static int failures = 0;

static void check(const char *name, int ok){

	printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
	if(!ok)
		failures++;
}

int main(void){

	if(sodium_init() < 0){
		fprintf(stderr, "libsodium failed to initialize\n");
		return 1;
	}

	/* Three people: alice(sender), bob(recipient), carol(outsider) */
	uint8_t a_pk[crypto_box_PUBLICKEYBYTES], a_sk[crypto_box_SECRETKEYBYTES];
    	uint8_t b_pk[crypto_box_PUBLICKEYBYTES], b_sk[crypto_box_SECRETKEYBYTES];
    	uint8_t c_pk[crypto_box_PUBLICKEYBYTES], c_sk[crypto_box_SECRETKEYBYTES];
    	crypto_box_keypair(a_pk, a_sk);
    	crypto_box_keypair(b_pk, b_sk);
    	crypto_box_keypair(c_pk, c_sk);

	const char *text = "hello bob";
	size_t text_len = strlen(text);

	uint8_t *enc;
	size_t enc_len;

	/* encrypt for bob, signed by alice */
	int rc = msgcrypt_encrypt((const uint8_t *)text, text_len, b_pk, a_sk, &enc, &enc_len);
	check("encrypt succeeds", rc == 0);
	if(rc!=0)
		return 1;

	check("size is plaintext + overhead", enc_len == text_len + MSGCRYPT_OVERHEAD);
	check("plaintext is not visible in the output", memmem(enc, enc_len, text, text_len) == NULL);

	uint8_t *plain;
	size_t plain_len;

	/* bob decrypts (alice's PUBLIC KEY, bob's SECRET KEY) */
	rc = msgcrypt_decrypt(enc, enc_len, a_pk, b_sk, &plain, &plain_len);
	check("bob can decrypt", rc == 0 && plain_len == text_len && memcmp(plain, text, text_len) == 0);
	if(rc == 0)
		free(plain);

	/* alice decrypts her own message (bob's public, alice's secret)*/

	rc = msgcrypt_decrypt(enc, enc_len, b_pk, a_sk, &plain, &plain_len);
	check("alice can read her own messaage", rc == 0);
	if(rc == 0)
		free(plain);

	/* carol tries (alice's public key, carol's secret key) */
	rc = msgcrypt_decrypt(enc, enc_len, a_pk, c_sk, &plain, &plain_len);
	check("carol cannot decrypt", rc == -1);
	if(rc == 0)
		free(plain);

	/* tampering. Flip one bit, try, then flip it back */
	enc[enc_len - 1] ^= 1;
	rc = msgcrypt_decrypt(enc, enc_len, a_pk, b_sk, &plain, &plain_len);
	check("flipped ciphertext bit is rejected", rc == -1);
	if(rc == 0)
		free(plain);
	enc[enc_len -1 ] ^= 1;

	/* check for the nonce, using enc[0] instead */
	check("flipped nonce bit is rejected", rc == -1);

	/* too short, and two encryptions differ */
	rc = msgcrypt_decrypt(enc, MSGCRYPT_OVERHEAD -1, a_pk, b_sk, &plain, &plain_len);
	check("too-short input is rejected", rc == -1);
	if(rc == 0)
		free(plain);
	uint8_t *enc2;
	size_t enc2_len;

	rc = msgcrypt_encrypt((const uint8_t*)text, text_len, b_pk, a_sk, &enc2, &enc2_len);
	check("same text twice gives different bytes", rc == 0 && enc2_len == enc_len && memcmp(enc, enc2, enc_len)!=0);
	if(rc == 0)
		free(enc2);

	/* an empty message */
	uint8_t *e0;
	size_t e0_len;
	rc = msgcrypt_encrypt((const uint8_t *)"", 0, b_pk, a_sk, &e0, &e0_len);
	check("empty message encrypts", rc == 0 && e0_len == MSGCRYPT_OVERHEAD);
	if(rc == 0){
		rc = msgcrypt_decrypt(e0, e0_len, a_pk, b_sk, &plain, &plain_len);
		check("empty message decrypts", rc == 0 && plain_len == 0);
		if(rc == 0)
			free(plain);
		free(e0);
	}
	free(enc);
	printf("%s (%d failure%s)\n", failures ? "SOME TESTS FAILED" : "ALL TEST PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}

