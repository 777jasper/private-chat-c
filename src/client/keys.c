#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sodium.h>
#include "keys.h"

/* Returns 0 if name is safe to use inside a file name, -1 otherwise. */
static int name_is_safe(const char *name)
{
    size_t n = strlen(name);
    if (n == 0 || n > 32)
        return -1;

    for(size_t i = 0; i<n; i++){
	    unsigned char ch = name[i];
	    if(!isalnum(ch) && ch != '_')
		    return -1;
    }

    return 0;
}

int keys_load_or_create(const char *username,
                        unsigned char pk[crypto_box_PUBLICKEYBYTES],
                        unsigned char sk[crypto_box_SECRETKEYBYTES])
{
    if (name_is_safe(username) == -1) {
        fprintf(stderr, "keys: unsafe username\n");
        return -1;
    }

    const char *home = getenv("HOME");
    if (home == NULL) {
        fprintf(stderr, "keys: HOME is not set\n");
        return -1;
    }

    char dir[512];
    char path[600];

    /* build both paths with snprintf, and treat truncation as failure */
    int a = snprintf(dir, sizeof dir, "%s/.chatapp", home);
    int b = snprintf(path, sizeof path, "%s/.chatapp/%s.key", home, username);
    if(a<0 || (size_t)a >= sizeof dir || b<0 || (size_t)b >= sizeof path){
	    fprintf(stderr, "keys: path too long\n");
	    return -1;
    }


    /* Case 1: the key file already exists. */
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
	    ssize_t n = read(fd, sk, crypto_box_SECRETKEYBYTES);
	    close(fd);
            if (n != (ssize_t)crypto_box_SECRETKEYBYTES) {
		    fprintf(stderr, "keys: damaged key file %s\n", path);
		    return -1;
                   }
                   crypto_scalarmult_base(pk, sk);     /* recompute the public key */
                   return 0;
    }
    if (errno != ENOENT) {
        perror("keys: open");
        return -1;
    }

    /* Case 2: first run for this user, so make a new keypair. */

    crypto_box_keypair(pk, sk);


    /* create the folder, and accept "already exists" */
    if (mkdir(dir, 0700) == -1 && errno != EEXIST) {
	    perror("keys: mkdir");
            sodium_memzero(sk, crypto_box_SECRETKEYBYTES);
            return -1;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd == -1){
	    perror("keys: create");
            sodium_memzero(sk, crypto_box_SECRETKEYBYTES);
            return -1;
    }
    ssize_t w = write(fd, sk, crypto_box_SECRETKEYBYTES);
    close(fd);
    if (w != (ssize_t)crypto_box_SECRETKEYBYTES) {
	    unlink(path);
	    sodium_memzero(sk, crypto_box_SECRETKEYBYTES);
	    return -1;
    }
    
    return 0;
}
