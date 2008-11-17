#ifndef __RG_LOCKS_H
#define __RG_LOCKS_H

int rg_running(void);

int rg_locked(void);
int rg_lockall(int flag);
int rg_unlockall(int flag);

int rg_quorate(void);
int rg_set_quorate(void);
int rg_set_inquorate(void);

int rg_inc_threads(void);
int rg_dec_threads(void);
int rg_wait_threads(void);

int rg_initialized(void);
int rg_set_initialized(int);
int rg_clear_initialized(int);
int rg_wait_initialized(int);

#define FL_INIT 0x1
#define FL_CONFIG 0x2

int rg_inc_status(void);
int rg_dec_status(void);
int rg_set_statusmax(int max);

int ccs_lock(void);
int ccs_unlock(int fd);

#endif

