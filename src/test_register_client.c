#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net_io.h"
#include "protocol.h"
#include "keys.h"
#include "msgcrypt.h"
#include "sealedauth.h"

#define PORT 5555

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s username password\n", argv[0]);
        return 1;
    }

    const char *command = argv[1];
    const char *username = argv[2];
    const char *password = argv[3];

    /* pick the message type from the command. */
    uint8_t msg_type;
    if(strcmp(command, "register") == 0)
	    msg_type = MSG_REGISTER;
    else if(strcmp(command, "login") == 0)
	    msg_type = MSG_LOGIN;
    else{
	    fprintf(stderr, "unknown command: %s\n", command);
	    return 1;
    }

    if(sodium_init() < 0){
	    fprintf(stderr, "libsodium failed to initialize\n");
	    return 1;
    }

    uint8_t server_pk[crypto_box_PUBLICKEYBYTES];
    if(keys_load_server_pk(server_pk) == -1)
	    return 1;
    uint8_t sealed_type = (msg_type == MSG_LOGIN) ? MSG_LOGIN_SEALED : MSG_REGISTER_SEALED;


    /* create the socket, build the address (loopback, PORT) and connect.On any failure: perror(...), free(payload), return 1.*/
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){
	    perror("socket");
	    return 1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    if(connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
	    perror("connect");
	    close(fd);
	    return 1;
    }

    /* send the frame. */
    if(send_sealed_auth(fd, sealed_type, username, password, server_pk) == -1){
	    close(fd);
	    return 1;
    }
    /* read the reply. */
    uint8_t type;
    uint8_t *reply;
    uint32_t reply_len;
    if(recv_frame(fd, &type, &reply, &reply_len)<0){
	    printf("recv_frame:failed\n");
	    close(fd);
	    return 1;
    }


    /* print the result, using %.*s because the reply has no 00. */
    printf("type=%u text=\"%.*s\"\n", type, (int)reply_len,reply ? (char *)reply : "");

    free(reply);
    close(fd);
    return 0;
}
