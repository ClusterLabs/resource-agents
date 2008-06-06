#ifndef __device_h__
#define __device_h__

#include "list.h"

/*FIXME -- this should go someplace global */
#define buflen(x) (strlen(x) + 1)

#define GNBD_FLAGS_READONLY        1
#define GNBD_FLAGS_UNCACHED        2
#define GNBD_FLAGS_INVALID         4

extern int is_clustered;

struct dev_info_s {
  uint64_t sectors;
  /* FIXME -- should these get changed back to their uint types */
  unsigned int timeout;
  unsigned int flags;
  char *name;
  char *path;
  char *uid;
  list_t list;
};
typedef struct dev_info_s dev_info_t;

int have_devices(void);  
int open_file(char *path, unsigned int flags, int *devfd);
int get_size(int fd, uint64_t *sectors);
int create_device(char *name, char *path, char *uid, unsigned int timeout,
                  unsigned int flags);
int invalidate_device(char *name, int sock);
int remove_device(char *name);
int get_dev_names(char **buffer, uint32_t *list_size);
int get_dev_info(char **buffer, uint32_t *list_size);
dev_info_t *find_device(char *name);
int last_uncached_device(char *name);
int get_dev_uid(char *name, char **buffer, uint32_t *size);

#endif /* __device_h__ */
