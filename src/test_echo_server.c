#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include "net_io.h"

#define PORT 5555

int main(void){

	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0){
		perror("socket");
		return 1;
	}

	/* Lets you restart the server immediately without "Address already in use" errors. */
	int yes = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server_addr.sin_port = htons(PORT);

	if(bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr))<0){
			perror("bind");
			close(listen_fd);
			return 1;
	}

	if(listen(listen_fd, 5)<0){
		perror("listen");
		close(listen_fd);
		return 1;
	}

	printf("Echo server waiting on 127.0.0.1:%d\n", PORT);

	int client_fd = accept(listen_fd, NULL, NULL);
	if(client_fd < 0){
		perror("accept");
		close(listen_fd);
		return 1;
	}
	printf("Client connected\n");

	/* Echo loop: receive a frame, send the frame back. */
	for(;;){
		uint8_t type;
		uint8_t *payload;
		uint32_t len;

		if(recv_frame(client_fd, &type, &payload, &len)<0){
			printf("recv_frame failed: client disconnected or sent a bad frame\n");
			break;
		}

		printf("Got frame: type=%u, len=%u\n", type, len);

		int rc = send_frame(client_fd, type, payload, len);
		free(payload);
		if(rc==-1){
			printf("send_frame: failed");
			break;
		}
	}

	printf("Client gone, shutting down\n");
	close(client_fd);
	close(listen_fd);
	return 0;
}



			
