#ifndef PROTOCOL_H
#define PROTOCOL_H

#include<stdint.h> /* gives us fixed-size types like uint32_t */

#define HEADER_SIZE 5 /* length : 4 bytes + type : 1 byte */
#define MAX_PAYLOAD 65536 /* largest payload we will accept (in bytes */

/* Message types. Each name gets a number that goes in the "type" byte. */
/* Payload formats:
   MSG_REGISTER, MSG_LOGIN: username 00 password 00
   MSG_SEND:                recipient 00 body        (body runs to the end of the frame)
   MSG_DELIVER:             sender 00 body
   MSG_OK, MSG_ERROR:       a short text, no 00                               */
typedef enum{
	MSG_REGISTER = 1,
	MSG_LOGIN = 2,
	MSG_OK = 3,
	MSG_ERROR = 4,
	MSG_CHAT = 5,
	MSG_SEND = 6, /* client -> server: recipient 00 body. */
	MSG_DELIVER = 7 /* server -> client: sender 00 body. */
}msg_type_t;

#endif
