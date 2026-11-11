#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "net_io.h"
#include "protocol.h"

#define PORT      5555
#define USER_BUF  64
#define PASS_BUF  256

/* Reads one line from the keyboard into buf (without the newline).
   Returns 0 on success, -1 on end of input or error. */
static int read_line(const char *prompt, char *buf, size_t size)
{
    printf("%s", prompt);
    fflush(stdout);                      /* make the prompt appear now */

    if(fgets(buf, size, stdin) == NULL)
	    return -1;
    /* look for the newline */
    char *nl = strchr(buf, '\n');
    if(nl != NULL)
	    *nl = '\0'; /* cut the line at the newline */
    else{
	    /* the line was longer than buf; throw away the rest */
	    int ch;
	    while((ch = getchar()) != '\n' && ch!=EOF);
    }
    return 0;
}

/* Same as read_line, but the typed characters are not shown. */
static int read_password(const char *prompt, char *buf, size_t size)
{
    struct termios old, t;

    if(tcgetattr(0, &old) == -1){
	    perror("tcgetattr");
	    return -1;
    }
    t = old;
    t.c_lflag &= ~ECHO;
    if(tcsetattr(0, TCSANOW, &t) == -1){
	    perror("tcsetattr");
	    return -1;
    }

    int rc = read_line(prompt, buf, size);
    
    /* put the old settings back */
    tcsetattr(0, TCSANOW, &old);
    printf("\n");
    return rc;
}

/* Connects to the server. Returns the socket, or -1 on failure. */
static int connect_server(void)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		perror("socket");
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr,0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(PORT);
	
	if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
		perror("connect");
		close(fd);
		return -1;
	}

	return fd;
}

int main(void)
{
    char command[USER_BUF];
    char username[USER_BUF];
    char password[PASS_BUF];

    /* read the three values */
    if(read_line("Command (register/login): ", command, sizeof command) == -1 ||
	read_line("Username: ", username, sizeof username) == -1 ||
	read_password("Password: ", password, sizeof password) == -1)
	    return 1;

    /* choose the message type from the command */
    uint8_t msg_type;
    if(strcmp(command, "register") == 0)
	    msg_type = MSG_REGISTER;
    else if(strcmp(command, "login") == 0)
	    msg_type = MSG_LOGIN;
    else{
	    explicit_bzero(password, sizeof password); /* wipe the password */
	    fprintf(stderr, "unknown command: %s\n", command);
	    return 1;
    }

    /* build the payload "username 00 password 00" in malloc'd buffer(size u_len + 1 + p_len + 1) */
    size_t u_len = strlen(username);
    size_t p_len = strlen(password);
    size_t total = u_len + 1 + p_len +1;

    if(total > MAX_PAYLOAD){
	    fprintf(stderr, "input too long\n");
	    explicit_bzero(password, sizeof password);
	    return 1;
    }

    uint8_t *payload = malloc(total);
    if(payload == NULL){
	    explicit_bzero(password, sizeof password);
	    perror("malloc");
	    return 1;
    }

    /* copy the username including its 00 into the start of payload */
    memcpy(payload, username, u_len + 1);

    /* copy the password including its 00 right after it */
    memcpy(payload + u_len + 1, password, p_len + 1);

    /* wipe the password as soon as it has been copied */
    explicit_bzero(password, sizeof password);

    /* connect */
    int fd = connect_server();
    if(fd == -1){
	    explicit_bzero(payload, total); /* it still holds the password */
	    free(payload);
	    return 1;
    }

    /* send, then wipe and free the payload on EVERY outcome */
    int rc = send_frame(fd, msg_type, payload, (uint32_t)total);
    explicit_bzero(payload, total);
    free(payload);
    if(rc == -1){
	    fprintf(stderr, "send failed\n");
	    close(fd);
	    return 1;
    }

    /* read the reply */
    uint8_t type;
    uint8_t *reply;
    uint32_t reply_len;
    if(recv_frame(fd, &type, &reply, &reply_len) == -1){
	    fprintf(stderr, "no reply from server\n");
	    close(fd);
	    return 1;
    }

    /* print it, clean up, and report success or failure */
    printf("%.*s\n", (int)reply_len, reply ? (char *)reply: "");
    free(reply);
    close(fd);
    return (type == MSG_OK) ? 0 : 1;
}
