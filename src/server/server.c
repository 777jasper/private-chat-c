#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include "net_io.h"
#include "protocol.h"
#include "db.h"

#define PORT 5555
#define BACKLOG 16

/* serves one client until it disconnects */
static void handle_client(int client_fd){

	for(;;){
		uint8_t type;
		uint8_t *payload;
		uint32_t len;

		if(recv_frame(client_fd, &type, &payload, &len)<0){
			
			break;
		}

		int rc = send_frame(client_fd, type, payload, len);
		free(payload);
		if(rc==-1){
			
			break;
		}
	}
}

/* Runs in its own thread, one per client */
static void *client_thread(void *arg){

	/* copy the descriptor out of the block main gave us, then free it */
	int client_fd = *(int *)arg;
	free(arg);

	handle_client(client_fd);
	close(client_fd);

	return NULL;
}

int main(void){

	/* open the database. */
	if(db_init("data/chat.db") == -1){
		fprintf(stderr, "server: database setup failed\n");
		return 1;
	}


	int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0){
		perror("socket");
		return 1;
	}

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

	if(listen(listen_fd, BACKLOG)<0){
		perror("listen");
		close(listen_fd);
		return 1;
	}

	printf("Server listening on 127.0.0.1:%d\n", PORT);

	for(;;){
		int client_fd = accept(listen_fd, NULL, NULL);
		if(client_fd<0){
			if(errno == EINTR)
				continue;
			else{
				perror("accept");
				continue;
			}
		}

		int *arg = malloc(sizeof *arg);
		if(arg == NULL){
			close(client_fd);
			continue;
		}
		*arg = client_fd;
		
		/* start the thread */
		pthread_t tid;

		int rc = pthread_create(&tid, NULL, client_thread, arg);

		if(rc!=0){
			fprintf(stderr, "pthread_create: %s\n", strerror(rc));
			free(arg);
			close(client_fd);
			continue;
		}
		pthread_detach(tid);
	}

	close(listen_fd); // close listen_fd
	db_close(); // close the database
	return 0;
}

