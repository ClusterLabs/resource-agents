/* General cman bits */
extern int write_cman_pipe(char *message);
extern void close_cman_pipe(void);

/* How we announce ourself in syslog */
#define CMAN_NAME "CMAN"
