#ifndef __gnbd_utils_h__
#define __gnbd_utils_h__

#include <stdio.h>
#include <signal.h>
#include <syslog.h>

typedef enum {QUIET, NORMAL, VERBOSE} verbosity_level;

extern verbosity_level verbosity;
extern char *program_name;
extern pid_t program_pid;
extern char *program_dir;
extern char sysfs_buf[4096];

#define printm(fmt, args...)\
do{\
  if(verbosity != QUIET)\
    printf("%s: " fmt, program_name , ##args); \
} while(0)

#define printv(fmt, args...)\
do{\
  if(verbosity == VERBOSE)\
    printf("%s: " fmt, program_name , ##args); \
} while(0)

#define printe(fmt, args...)\
fprintf(stderr, "%s: ERROR " fmt, program_name , ##args)

#define print_err(fmt, args...)\
fprintf(stderr, "%s: ERROR [%s:%d] " fmt, program_name, \
        __FILE__, __LINE__ , ##args)

#define log_msg(fmt, args...)\
do{\
  if(verbosity != QUIET)\
    syslog(LOG_NOTICE, fmt, ## args);\
} while(0)

#define log_verbose(fmt, args...)\
do{\
  if(verbosity == VERBOSE)\
    syslog(LOG_NOTICE, fmt, ## args);\
} while(0)

#define log_always(fmt, args...) syslog(LOG_NOTICE, fmt, ##args)

#define log_warn(fmt, args...)\
do{\
  if(verbosity != QUIET)\
    syslog(LOG_NOTICE, "WARNING " fmt, ##args);\
} while(0)

/* FIXME -- I need a log_fail, so that I can differentiate between errors
   that shouldn't happen, and expected errors that I need to log */
#define log_err(fmt, args...)\
syslog(LOG_ERR, "ERROR [%s:%d] " fmt, __FILE__, __LINE__ , ##args)

#define log_fail(fmt, args...)\
syslog(LOG_ERR, "ERROR " fmt, ##args)

#define fail_startup(fmt, args...)\
do {\
  printe(fmt, ##args);\
  log_err(fmt, ##args);\
  kill(program_pid, SIGUSR2); \
  exit(1);\
} while(0)


#define finish_startup(fmt, args...)\
do {\
  printm(fmt, ##args);\
  log_msg(fmt, ##args);\
  kill(program_pid, SIGUSR1);\
  close(STDIN_FILENO);\
  close(STDOUT_FILENO);\
  close(STDERR_FILENO);\
} while(0)

typedef uint32_t ip_t;
#define beip_to_cpu be32_to_cpu
#define cpu_to_beip cpu_to_be32
char *beip_to_str(ip_t ip);



int daemonize(void);
int get_my_nodename(char *buf, int is_clustered);
int __check_lock(char *file, int *pid);
int check_lock(char *file, int *pid);
int pid_lock(char *extra_info);
void daemonize_and_exit_parent(void);
int open_max(void);
int parse_server(char *buf, char *name, uint16_t *port);
char *get_sysfs_attr(int minor, char *attr_name);
char *do_get_sysfs_attr(int minor, char *attr_name);
int do_set_sysfs_attr(int minor_nr, char *attribute, char *val);
int __set_sysfs_attr(int minor_nr, char *attribute, char *val);
void set_sysfs_attr(int minor_nr, char *attribute, char *val);

#endif /* __gnbd_utils_h__ */
