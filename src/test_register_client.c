#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net_io.h"
#include "protocol.h"

#define PORT 5555

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s username password\n", argv[0]);
        return 1;
    }

    const char *username = argv[1];
    const char *password = argv[2];

    size_t u_len = strlen(username);
    size_t p_len = strlen(password);
    size_t total = u_len + 1 + p_len + 1;   /* both texts plus both 00 bytes */

    if (total > MAX_PAYLOAD) {
        fprintf(stderr, "input too long\n");
        return 1;
    }

    uint8_t *payload = malloc(total);
    if (payload == NULL) {
        perror("malloc");
        return 1;
    }
    
    /* copy the username INCLUDING its 00 into the start of payload. */
    memcpy(payload, username, u_len + 1);
    
    /* copy the password INCLUDING its 00 right after it. */
    memcpy(payload + u_len + 1, password, p_len + 1);

    /* create the socket, build the address (loopback, PORT) and connect.On any failure: perror(...), free(payload), return 1.*/
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){
	    perror("socket");
	    free(payload);
	    return 1;
    }
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(PORT);

    if(connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
	    perror("connect");
	    free(payload);
	    close(fd);
	    return 1;
    }



    /* send the frame. */
    int rc = send_frame(fd, MSG_REGISTER, payload, (uint32_t)total);
    free(payload);
    if(rc == -1){
	    printf("send_frame: failed\n");
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
