
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include <sys/socket.h>
#include <sys/types.h>

#include <linux/net.h>
#include <linux/fs.h>
#include <linux/socket.h>
/*#include <linux/in.h>*/
#include <arpa/inet.h>

#include <signal.h>

#include "copyright.cf"

#define SA struct sockaddr


#define MAXBUF 1200

#define DID_SUCCESS 0
#define DID_FAILURE 1 

