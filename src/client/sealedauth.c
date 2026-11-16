#include <stdio.h>
#include <stdlib.h>
#include "net_io.h"
#include "protocol.h"
#include "msgcrypt.h"
#include "sealedauth.h"

int send_sealed_auth(int fd, uint8_t sealed_type,const char *user, const char *pass,
		const uint8_t server_pk[crypto_box_PUBLICKEYBYTES]){
	/* ask for a challenge */
	if(send_frame(fd, MSG_CHALLENGE_REQ, NULL, 0) == -1){
		fprintf(stderr, "could not request a challenge\n");
		return -1;
	}
	uint8_t  type;
	uint8_t *ch;
	uint32_t ch_len;

	/* receive it and check it is what we expect */
	if (recv_frame(fd, &type, &ch, &ch_len) == -1) {
		fprintf(stderr, "no challenge from server\n");
		return -1;
	}
	if(type != MSG_CHALLENGE || ch_len != MSGCRYPT_CHALLENGE_SIZE){
		free(ch);
		fprintf(stderr, "unexpected reply to challenge request\n");
		return -1;
	}
	uint8_t *sealed;
	size_t   sealed_len;

	/* seal, then free the challenge whatever happened */
	int rc = msgcrypt_seal_credentials(ch, user, pass, server_pk, &sealed, &sealed_len);
	free(ch);
	if(rc == -1){
		fprintf(stderr, "could not encrypt the login\n");
		return -1;
	}

	/* send it, free it, report the result */
	rc = send_frame(fd, sealed_type, sealed, (uint32_t)sealed_len);
	free(sealed);
	return (rc == 0) ? 0 : -1;
}

