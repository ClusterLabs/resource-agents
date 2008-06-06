#ifndef __GFS2_TOOL_DOT_H__
#define __GFS2_TOOL_DOT_H__


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
void print_args(int argc, char **argv);
void print_jindex(int argc, char **argv);
void print_journals(int argc, char **argv);
void print_rindex(int argc, char **argv);
void print_quota(int argc, char **argv);
void print_list(void);
void do_shrink(int argc, char **argv);
void do_withdraw(int argc, char **argv);


/* From sb.c */

void do_sb(int argc, char **argv);


/* From tune.c */

void get_tune(int argc, char **argv);
void set_tune(int argc, char **argv);

#endif /* __GFS2_TOOL_DOT_H__ */
