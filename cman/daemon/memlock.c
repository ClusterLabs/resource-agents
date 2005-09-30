/*
 * Copyright (C) 2003-2004 Sistina Software, Inc. All rights reserved.  
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is part of LVM2.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "config.h"
#include "logging.h"


static size_t _size_stack;
static size_t _size_malloc_tmp;
static size_t _size_malloc = 2000000;

static void *_malloc_mem = NULL;
static int _memlock_count = 0;

static void _touch_memory(void *mem, size_t size)
{
	size_t pagesize = getpagesize();
	void *pos = mem;
	void *end = mem + size - sizeof(long);

	while (pos < end) {
		*(long *) pos = 1;
		pos += pagesize;
	}
}

static void _allocate_memory(void)
{
	void *stack_mem, *temp_malloc_mem;

	if ((stack_mem = alloca(_size_stack)))
		_touch_memory(stack_mem, _size_stack);

	if ((temp_malloc_mem = malloc(_size_malloc_tmp)))
		_touch_memory(temp_malloc_mem, _size_malloc_tmp);

	if ((_malloc_mem = malloc(_size_malloc)))
		_touch_memory(_malloc_mem, _size_malloc);

	free(temp_malloc_mem);
}

static void _release_memory(void)
{
	free(_malloc_mem);
}

/* Stop memory getting swapped out */
static void _lock_memory(void)
{
#ifdef MCL_CURRENT
	if (mlockall(MCL_CURRENT | MCL_FUTURE))
		log_msg(LOG_ERR, "mlockall failed: %s", strerror(errno));
#endif
	_allocate_memory();

	errno = 0;
}

static void _unlock_memory(void)
{
#ifdef MCL_CURRENT
	if (munlockall())
		log_msg(LOG_ERR, "munlockall failed: %s", strerror(errno));
#endif
	_release_memory();
}

void memlock_inc(void)
{
	if (!_memlock_count++)
		_lock_memory();
	P_DAEMON("memlock_count inc to %d", _memlock_count);
}

void memlock_dec(void)
{
	if (_memlock_count && (!--_memlock_count))
		_unlock_memory();
	P_DAEMON("memlock_count dec to %d", _memlock_count);
}

int memlock(void)
{
	return _memlock_count;
}

void memlock_init(void)
{
	_size_stack = cman_config[RESERVED_STACK].value * 1024;
	_size_malloc_tmp = cman_config[RESERVED_MEMORY].value * 1024;
}

