#ifndef _DS_H
#define _DS_H

int ds_init(void);
int ds_key_init(char *keyid, int maxsize, int timeout);
int ds_key_finish(char *keyid);
int ds_write(char *keyid, void *buf, size_t maxlen);
int ds_read(char *keyid, void *buf, size_t maxlen);
int ds_finish(void);

#define DS_MIN_SIZE 512

#endif
