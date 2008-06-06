/** @file
 * Header for findproc.c
 */
#ifndef __PROC_H
#define __PROC_H

int findkillproc(char *, pid_t *, size_t, int);
int findproc(char *, pid_t *, size_t);
int killall(char *, int);

#endif
