#ifndef __SIGNALS_H
#define __SIGNALS_H

void *setup_signal(int, void (*)(int));
int block_signal(int sig);
int unblock_signal(int sig);
int block_all_signals(void);

#endif
