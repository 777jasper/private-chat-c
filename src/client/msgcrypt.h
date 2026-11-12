#ifndef MSGCRYPT_H
#define MSGCRYPT_H

#include <stddef.h>
#include <stdint.h>
#include <sodium.h>


/* Extra bytes that encryption adds: the nonce plus the MAC. */
#define MSGCRYPT_OVERHEAD (crypto_box_NONCEBYTES + crypto_box_MACBYTES)

/* Largest plaintext we will encrypt. Keeps the result under MAX_PAYLOAD. */
#define MSGCRYPT_MAX_PLAIN 60000

/* Encrypts plain for the owner of their_pk, signed with my_sk. out receives a malloc'd buffer "nonce | ciphertext" and *out_len its size. The caller must free(*out). Returns 0 on success, -1 on failure. */
int msgcrypt_encrypt(const uint8_t *plain, size_t plain_len,
                     const uint8_t their_pk[crypto_box_PUBLICKEYBYTES],
                     const uint8_t my_sk[crypto_box_SECRETKEYBYTES],
                     uint8_t **out, size_t *out_len);

/* Decrypts a buffer made by msgcrypt_encrypt. plain receives a malloc'd buffer (with one extra 00 byte after the text, for convenience) and *plain_len its length, not counting that extra byte. The caller must free(*plain). Returns 0 on success, -1 if the data is too short, damaged, or the keys are wrong. */
int msgcrypt_decrypt(const uint8_t *in, size_t len, const uint8_t their_pk[crypto_box_PUBLICKEYBYTES],
			const uint8_t my_sk[crypto_box_SECRETKEYBYTES], uint8_t **plain, size_t *plain_len);

#endif /* MSGCRYPT_H */
