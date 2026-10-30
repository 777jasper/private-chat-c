#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include "net_io.h"
#include "protocol.h"

#define PORT 5555

int main(void){
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd<0){
		perror("scoket");
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

	printf("Connected to server\n");

	const char *msg1 = "hello";
	const char *msg2 = "world";

	/* send msg1 as a MSG_CHAT frame */
	if(send_frame(fd, MSG_CHAT, (const uint8_t *)msg1, strlen(msg1))<0){
		printf("send_frame: msg1 failed");
		close(fd);
		return 1;
	}

	if(send_frame(fd, MSG_CHAT, (const uint8_t *)msg2, strlen(msg2))<0){
		printf("send_frame: msg2 failed");
		close(fd);
		return 1;
	}

	if(send_frame(fd, MSG_OK, NULL, 0)<0){
		printf("send_frame: Empty message failed");
		close(fd);
		return 1;
	}

	/* Now read the three echoes back */
	for(int i=0; i<3; i++){
		uint8_t type;
		uint8_t *payload;
		uint32_t len;

		if(recv_frame(fd, &type, &payload, &len)<0){
			printf("recv_frame: failed");
			close(fd);
			return 1;
		}
		printf("Echoed frame %d: type=%u len=%u text=\"%.*s\"\n", i+1, type, len, (int)len, payload ? (char *)payload : "");
		free(payload);
	}
	close(fd);
	printf("Done\n");
}

