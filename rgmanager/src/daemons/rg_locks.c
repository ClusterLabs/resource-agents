/*
  Copyright Red Hat, Inc. 2004

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to the
  Free Software Foundation, Inc.,  675 Mass Ave, Cambridge, 
  MA 02139, USA.
*/
#include <pthread.h>
#include <stdio.h>
#include <ccs.h>

#include <mallocdbg.h>

static int __rg_quorate = 0;
static int __rg_lock = 0;
static int __rg_threadcnt = 0;
static int __rg_initialized = 0;

static pthread_mutex_t locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t unlock_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t zero_cond = PTHREAD_COND_INITIALIZER;
static pthread_cond_t init_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t __ccs_mutex = PTHREAD_MUTEX_INITIALIZER;

int
rg_initialized(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_initialized;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_set_initialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	__rg_initialized = 1;
	pthread_cond_broadcast(&init_cond);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_uninitialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	__rg_initialized = 0;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_wait_initialized(void)
{
	pthread_mutex_lock(&locks_mutex);
	while (!__rg_initialized)
		pthread_cond_wait(&init_cond, &locks_mutex);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


/**
  not sure if ccs is thread safe or not
  */
int
ccs_lock(void)
{
	int ret;
	pthread_mutex_lock(&__ccs_mutex);
       	ret = ccs_connect();
	if (ret < 0) {
		pthread_mutex_unlock(&__ccs_mutex);
		return -1;
	}
	return 0;
}


int
ccs_unlock(int fd)
{
	int ret;

       	ret = ccs_disconnect(fd);
	pthread_mutex_unlock(&__ccs_mutex);
	if (ret < 0) {
		return -1;
	}
	return 0;
}


int
rg_lockall(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (!__rg_lock)
		__rg_lock = 1;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_locked(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_lock;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_unlockall(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_lock)
		__rg_lock = 0;
	pthread_cond_broadcast(&unlock_cond);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_wait_unlockall(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (!__rg_lock) {
		pthread_mutex_unlock(&locks_mutex);
		return 0;
	}

	pthread_cond_wait(&unlock_cond, &locks_mutex);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_quorate(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (!__rg_quorate)
		__rg_quorate = 1;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_set_inquorate(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_quorate)
		__rg_quorate = 0;
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_quorate(void)
{
	int ret;
	pthread_mutex_lock(&locks_mutex);
	ret = __rg_quorate;
	pthread_mutex_unlock(&locks_mutex);
	return ret;
}


int
rg_inc_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	++__rg_threadcnt;
#ifdef DEBUG
	printf("%s: %d threads active\n", __FILE__, __rg_threadcnt);
#endif
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_dec_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	--__rg_threadcnt;
	if (__rg_threadcnt <= 0) {
		__rg_threadcnt = 0;
		pthread_cond_broadcast(&zero_cond);
	}
#ifdef DEBUG
	printf("%s: %d threads active\n", __FILE__, __rg_threadcnt);
#endif
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}


int
rg_wait_threads(void)
{
	pthread_mutex_lock(&locks_mutex);
	if (__rg_threadcnt)
		pthread_cond_wait(&zero_cond, &locks_mutex);
	pthread_mutex_unlock(&locks_mutex);
	return 0;
}

