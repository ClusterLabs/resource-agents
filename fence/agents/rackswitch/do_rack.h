
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>

#include <signal.h>

#include "copyright.cf"

#define SA struct sockaddr


#define MAXBUF 1200

#define DID_SUCCESS 0
#define DID_FAILURE 1 

