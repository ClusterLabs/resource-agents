/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#ifndef __KDBL_DOT_H__
#define __KDBL_DOT_H__


/* Printf */

int kdbl_printf(const char *fmt, ...)
__attribute__ ((format (printf, 1, 2)));
void kdbl_printf_dump2console(void);


/* Trace */

#define KDBL_TRACE_TEST(array, flag) \
((array)[(flag) / 8] & (1 << ((flag) % 8)))
#define KDBL_TRACE_SET(array, flag) \
do { \
	(array)[(flag) / 8] |= (1 << ((flag) % 8)); \
} while (0)
#define KDBL_TRACE_CLEAR(array, flag) \
do { \
	(array)[(flag) / 8] &= ~(unsigned char)(1 << ((flag) % 8)); \
} while (0)

int kdbl_trace_create_array(char *program, char *version,
			    unsigned int flags, unsigned char **array);
void kdbl_trace_destroy_array(unsigned char *array);


/* Profile */

static __inline__ uint64_t
kdbl_profile_enter(void)
{
	struct timeval tv;
	do_gettimeofday(&tv);
	return ((uint64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
}
void kdbl_profile_exit(void *cookie, unsigned int flag, uint64_t start);

int kdbl_profile_create_array(char *program, char *version,
			    unsigned int flags, void **cookie);
void kdbl_profile_destroy_array(void *cookie);


#endif /* __KDBL_DOT_H__ */
