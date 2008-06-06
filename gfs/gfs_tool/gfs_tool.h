#ifndef __GFS_TOOL_DOT_H__
#define __GFS_TOOL_DOT_H__


#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#define die(fmt, args...) \
do { \
	fprintf(stderr, "%s: ", prog_name); \
	fprintf(stderr, fmt, ##args); \
	exit(EXIT_FAILURE); \
} while (0)


extern char *prog_name;
extern char *action;
extern int override;
extern int expert;
extern int debug;
extern int continuous;
extern int interval;


/* From counters.c */

void print_counters(int argc, char **argv);


/* From df.c */

void print_df(int argc, char **argv);


/* From layout.c */

void print_layout(int argc, char **argv);


/* From main.c */

void print_usage(void);


/* From misc.c */

void do_file_flush(int argc, char **argv);
void do_freeze(int argc, char **argv);
void margs(int argc, char **argv);
void print_lockdump(int argc, char **argv);
void set_flag(int argc, char **argv);
void print_stat(int argc, char **argv);
void print_sb(int argc, char **argv);
void print_jindex(int argc, char **argv);
void print_rindex(int argc, char **argv);
void print_quota(int argc, char **argv);
void print_list(void);
void reclaim_metadata(int argc, char **argv);
void do_shrink(int argc, char **argv);
void do_withdraw(int argc, char **argv);


/* From sb.c */

void do_sb(int argc, char **argv);


/* From tune.c */

void get_tune(int argc, char **argv);
void set_tune(int argc, char **argv);


/* From util.c */

void check_for_gfs(int fd, char *path);
char *get_list(void);
char **str2lines(char *str);
char *mp2cookie(char *mp, int ioctl_ok);
char *name2value(char *str, char *name);
uint32_t name2u32(char *str, char *name);
uint64_t name2u64(char *str, char *name);


#endif /* __GFS_TOOL_DOT_H__ */
