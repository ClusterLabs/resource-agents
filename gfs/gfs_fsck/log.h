#ifndef __LOG_H
#define __LOG_H

#define MSG_DEBUG	7
#define MSG_INFO	6
#define MSG_NOTICE	5
#define MSG_WARN	4
#define MSG_ERROR	3
#define MSG_CRITICAL	2
#define MSG_NULL	1



#define print_log(iif, priority, format...)	\
do { \
	print_fsck_log(iif, priority, __FILE__, __LINE__, ## format);	\
} while(0)

#define log_debug(format...) \
do { \
	print_log(0, MSG_DEBUG, format);		\
} while(0)

#define log_info(format...) \
do { \
	print_log(0, MSG_INFO, format);		\
} while(0)

#define log_notice(format...) \
do { \
	print_log(0, MSG_NOTICE, format);	\
} while(0)

#define log_warn(format...) \
do { \
	print_log(0, MSG_WARN, format);		\
} while(0)

#define log_err(format...) \
do { \
	print_log(0, MSG_ERROR, format);		\
} while(0)

#define log_crit(format...) \
do { \
	print_log(0, MSG_CRITICAL, format);	\
} while(0)

#define stack log_debug("<backtrace> - %s()\n", __func__)

#define log_at_debug(format...)		\
do { \
	print_log(1, MSG_DEBUG, format);	\
} while(0)

#define log_at_info(format...) \
do { \
	print_log(1, MSG_INFO, format);		\
} while(0)

#define log_at_notice(format...) \
do { \
	print_log(1, MSG_NOTICE, format);	\
} while(0)

#define log_at_warn(format...) \
do { \
	print_log(1, MSG_WARN, format);		\
} while(0)

#define log_at_err(format...) \
do { \
	print_log(1, MSG_ERROR, format);		\
} while(0)

#define log_at_crit(format...) \
do { \
	print_log(1, MSG_CRITICAL, format);	\
} while(0)

void increase_verbosity(void);
void decrease_verbosity(void);
void print_fsck_log(int iif, int priority, char *file, int line, const char *format, ...);
int query(struct fsck_sb *sbp, const char *format, ...);


#endif /* __LOG_H */
