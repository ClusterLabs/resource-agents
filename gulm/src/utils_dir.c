/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "gulm_defines.h"

/**
 * build_tmp_path - 
 * @file: 
 * @path: 
 * 
 * build up the path into the tmp dir, whereever it may be.
 * 
 * Returns: int
 */
int build_tmp_path(char *file, char **path)
{
   char *tmp, *p;
   tmp = getenv("TMPDIR");
   if(tmp == NULL ) tmp = "/tmp";
   p = malloc( strlen(tmp) + strlen(file) + 2);
   if( p == NULL ) return -ENOMEM;
   strcpy(p, tmp);
   strcat(p, "/");
   strcat(p, file);
   *path = p;
   return 0;
}

/**
 * pid_lock - only one.
 */
void pid_lock(char *path, char *lf)
{
   struct flock lock;
   char buf[12], plf[1024];
   int fd, val;

   snprintf(plf, 1024, "%s/%s.pid", path, lf);

   if( (fd = open(plf, O_WRONLY | O_CREAT,
                  (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) < 0) {
      die(ExitGulm_PidLock, "open error on \"%s\": %s\n",
            plf, strerror(errno));
   }

   lock.l_type = F_WRLCK;
   lock.l_start = 0;
   lock.l_whence = SEEK_SET;
   lock.l_len = 0;

   if (fcntl(fd, F_SETLK, &lock) < 0) {
      if (errno == EACCES || errno == EAGAIN) {
         die(ExitGulm_PidLock, "%s already running\n", lf);
      } else {
         die(ExitGulm_PidLock, "fcntl F_SETLK error: %s\n", strerror(errno));
      }
   }

   if (ftruncate(fd, 0) < 0) {
      die(ExitGulm_PidLock, "ftruncate error: %s\n", strerror(errno));
   }

   snprintf(buf, 12, "%d\n", getpid());
   if (write(fd, buf, strlen(buf)) != strlen(buf)) {
      die(ExitGulm_PidLock,
          "write error to \"%s\": %s\n", plf, strerror(errno));
   }

   if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
      die(ExitGulm_PidLock, "fcntl F_GETFD error: %s\n", strerror(errno));
   }

   val |= FD_CLOEXEC;
   if (fcntl(fd, F_SETFD, val) < 0) {
      die(ExitGulm_PidLock, "fcntl F_SETFD error: %s\n", strerror(errno));
   }
}

/**
 * clear_pid - 
 */
void clear_pid(char *path, char *lf)
{
   char plf[1024];
   snprintf(plf, 1024, "%s/%s.pid", path, lf);
   unlink(plf);
}

/* vim: set ai cin et sw=3 ts=3 : */
