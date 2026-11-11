#ifndef PROTOCOL_H
#define PROTOCOL_H

#include<stdint.h> /* gives us fixed-size types like uint32_t */

#define HEADER_SIZE 5 /* length : 4 bytes + type : 1 byte */
#define MAX_PAYLOAD 65536 /* largest payload we will accept (in bytes */

/* Message types. Each name gets a number that goes in the "type" byte.
 * Payload formats:
   MSG_REGISTER, MSG_LOGIN: username 00 password 00
   MSG_SEND:                recipient 00 body        (body runs to the end of the frame)
   MSG_DELIVER:             sender 00 body
   MSG_OK, MSG_ERROR:       a short text, no 00                              
   MSG_SET_KEY:             32 bytes (the public key)
   MSG_GET_KEY:             username (no 00)
   MSG_KEY:                 username 00 key (32 bytes) */

typedef enum{
	MSG_REGISTER = 1,
	MSG_LOGIN = 2,
	MSG_OK = 3,
	MSG_ERROR = 4,
	MSG_CHAT = 5,
	MSG_SEND = 6, /* client -> server: recipient 00 body. */
	MSG_DELIVER = 7, /* server -> client: sender 00 body. */
	MSG_SET_KEY = 8, /* client -> server: 32 byte public key. */
	MSG_GET_KEY = 9, /* client -> server: username. */
	MSG_KEY = 10 /* server -> client: username 00 key (32 bytes) */
}msg_type_t;

#endif
