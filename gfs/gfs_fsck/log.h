/*****************************************************************************
******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
******************************************************************************
*****************************************************************************/

#ifndef __LOG_H
#define __LOG_H

#define MSG_DEBUG	7
#define MSG_INFO	6
#define MSG_NOTICE	5
#define MSG_WARN	4
#define MSG_ERROR	3
#define MSG_CRITICAL	2
#define MSG_NULL	1



#define print_log(priority, format...) \
do { \
	print_fsck_log(priority, __FILE__, __LINE__, ## format); \
} while(0)

#define log_debug(format...) \
do { \
	print_log(MSG_DEBUG, format); \
} while(0)

#define log_info(format...) \
do { \
	print_log(MSG_INFO, format); \
} while(0)

#define log_notice(format...) \
do { \
	print_log(MSG_NOTICE, format); \
} while(0)

#define log_warn(format...) \
do { \
	print_log(MSG_WARN, format); \
} while(0)

#define log_err(format...) \
do { \
	print_log(MSG_ERROR, format); \
} while(0)

#define log_crit(format...) \
do { \
	print_log(MSG_CRITICAL, format); \
} while(0)

#define stack log_debug("<backtrace> - %s()\n", __func__)

void increase_verbosity(void);
void decrease_verbosity(void);
void print_fsck_log(int priority, char *file, int line, const char *format, ...);

#endif /* __LOG_H */
