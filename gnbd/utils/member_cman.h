#include "libcman.h"

extern cman_handle_t ch;
extern int cman_cb;
extern int cman_reason;

int can_shutdown(void *private);
int default_process_member(void);
int setup_member(void *private);
void exit_member(void);
