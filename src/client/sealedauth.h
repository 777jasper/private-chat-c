#ifndef SEALEDAUTH_H
#define SEALEDAUTH_H

#include <stdint.h>
#include <sodium.h>

/* Asks the server for a challenge, seals it with the credentials and sends the sealed frame. Returns 0 on success, -1 on failure.*/
int send_sealed_auth(int fd, uint8_t sealed_type,const char *user, const char *pass,
		const uint8_t server_pk[crypto_box_PUBLICKEYBYTES]);

#endif /* SEALEDAUTH_H */
