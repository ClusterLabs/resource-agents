#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/mount.h>

#include "gnbd.h"

#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12, 114, uint64_t)
#endif

#include "list.h"
#include "gnbd_utils.h"
#include "local_req.h"
#include "device.h"
#include "gserv.h"
#include "extern_req.h"

list_decl(device_list);

int have_devices(void)
{
  return !list_empty(&device_list);
}

int last_uncached_device(char *name)
{
  dev_info_t *dev = NULL;
  list_t *list_item;
  int uncached_devs = 0;
  int found = 0;

  list_foreach(list_item, &device_list) {
    dev = list_entry(list_item, dev_info_t, list);
    if (strcmp(name, dev->name) == 0){
      found = 1;
      if (dev->timeout == 0)
        return LOCAL_SUCCESS_REPLY;
    }
    else if (dev->timeout)
      uncached_devs++;
  }
  if (!found){
    log_fail("cannot find gnbd '%s' to remove\n", name);
    return -ENODEV;
  }
  if (uncached_devs)
    return LOCAL_SUCCESS_REPLY;
  return LOCAL_RM_CLUSTER_REPLY;
}

dev_info_t *find_device(char *name)
{
  list_t *list_item;
  dev_info_t *dev_info = NULL;
  
  list_foreach(list_item, &device_list) {
    dev_info = list_entry(list_item, dev_info_t, list);
    if (strcmp(name, dev_info->name) == 0)
      break;
    dev_info = NULL;
  }
  
  return dev_info;
}

int open_file(char *path, unsigned int flags, int *devfd)
{
  int fd;
  int err;
  int open_flags = 0;
  
  *devfd = -1;

  /* FIXME -- This stuff might be horribly wrong.  I need AIO and nocopy to
     not get shouted at, and that might change this. */
  if (flags & GNBD_FLAGS_READONLY)
    open_flags = O_RDONLY;
  else
    open_flags = O_RDWR | O_SYNC;
  if (flags & GNBD_FLAGS_UNCACHED)
    open_flags |= O_DIRECT;
  fd = open(path, open_flags);
  if (fd < 0){
    err = -errno;
    log_err("cannot open %s in %s%s mode : %s\n", path,
           (flags & GNBD_FLAGS_READONLY)? "O_RDONLY" : "O_RDWR | O_SYNC",
           (flags & GNBD_FLAGS_UNCACHED)? " | O_DIRECT" : "",
           strerror(errno));
    return err;
  }

  *devfd = fd;
  return 0;
}
  
int get_size(int fd, uint64_t *sectors)
{
  /* Should I use the uint64 size, when there's a good chance that off_t will
     be correct in 2.6 */
  struct stat stat_buf;
  uint64_t size_64;
  unsigned long sectors_long;
  off_t off_mask = ~(off_t)511;
  uint64_t mask = ~(uint64_t)511;
  
  stat_buf.st_size = 0;
  if (fstat(fd, &stat_buf) < 0){
    log_err("cannot fstat gnbd device file : %s\n", strerror(errno));
    return -errno;
  }
  if (S_ISREG(stat_buf.st_mode)){
    if (stat_buf.st_size < 512){
      log_err("gnbd device file must be larger than 512 bytes\n");
      /* FIXME -- There must be something better */
      return -ENOSPC;
    }
    if (stat_buf.st_size != (stat_buf.st_size & off_mask))
      log_verbose("gnbd device file size not a multiple of 512 bytes\n"
                  "export size will be rounded down\n");
    *sectors = (uint64_t)stat_buf.st_size >> 9;
    return 0;
  }
  if (S_ISBLK(stat_buf.st_mode)){
    if (ioctl(fd, BLKGETSIZE64, &size_64) >= 0){
      if (size_64 != (size_64 & mask)){
        log_verbose("gnbd device size not a multiple of 512 bytes\n"
                    "export size will be rounded down\n");
      }
      *sectors = size_64 >> 9;
      return 0;
    }
    /* FIXME -- is this correct in 2.6 */
    if (ioctl(fd, BLKGETSIZE, &sectors_long) >= 0){
      *sectors = (uint64_t)sectors_long;
      return 0;
    }
    log_err("couldn't get size for gnbd block device : %s\n", strerror(errno));
    return -errno;
  }
  log_err("gnbd device must be either a block device or a file\n");
  return -EINVAL;
}

int create_device(char *name, char *path, char *uid, unsigned int timeout,
                  unsigned int flags)
{
  int err;
  dev_info_t *dev;
  int devfd;
 
  if (is_clustered == 0 && timeout != 0){
    log_err("cannot export uncached devices when gnbd_serv is started with -n\n");
    err = -ENOTSUP;
    goto fail;
  }

  if (find_device(name)){
    log_err("gnbd name '%s' already in use\n", name);
    err = -EBUSY;
    goto fail;
  }
  
  dev = malloc(sizeof(dev_info_t));
  if (!dev){
    log_err("couldn't allocate memory for gnbd '%s' info\n", name);
    err = -ENOMEM;
    goto fail;
  }
  
  memset(dev, 0, sizeof(dev_info_t));
  
  dev->timeout = timeout;
  dev->flags = flags;
  dev->name = malloc(buflen(name));
  if (!dev->name){
    log_err("couldn't allocate memory for gnbd '%s' name\n", name);
    err = -ENOMEM;
    goto fail_info;
  }
  strcpy(dev->name, name);

  dev->path = malloc(buflen(path));
  if (!dev->path){
    log_err("couldn't allocate memory for gnbd '%s' path (%s)\n", name, path);
    err = -ENOMEM;
    goto fail_name;
  }
  strcpy(dev->path, path);

  if (uid && uid[0]){
    dev->uid = malloc(buflen(uid));
    if (!dev->uid){
      log_err("couldn't allocate memory for gnbd '%s' uid (%s)\n", name, uid);
      err = -ENOMEM;
      goto fail_path;
    }
    strcpy(dev->uid, uid);
  } else
    dev->uid = NULL;

  err = open_file(dev->path, dev->flags, &devfd);
  if (err < 0)
    goto fail_malloc;
  
  err = get_size(devfd, &dev->sectors);
  if (err < 0)
    goto fail_file;

  close(devfd);

  list_add(&dev->list, &device_list);
  
  log_verbose("gnbd device '%s' serving %s exported with %Lu sectors\n",
              dev->name, dev->path, (long long unsigned int)dev->sectors);
  return 0;

 fail_file:
  close(devfd);
 fail_malloc:
  free(dev->uid);
 fail_path:
  free(dev->path);
 fail_name:
  free(dev->name);
 fail_info:
  free(dev);
 fail:
  return err;
}

int invalidate_device(char *name, int sock)
{
  int err;
  dev_info_t *dev;
  dev = find_device(name);
  if (!dev){
    log_fail("cannot find gnbd device '%s' to remove\n", name);
    return -ENODEV;
  }
  dev->flags |= GNBD_FLAGS_INVALID;
  err = kill_gserv(0, dev, sock);
  if (err < 0)
    return err;
  return 0;
}



int remove_device(char *name)
{
  dev_info_t *dev;
  dev = find_device(name);
  if (!dev){
    log_fail("cannot find gnbd device '%s' to remove\n", name);
    return -ENODEV;
  }
  if (find_gserv_info(NULL, dev)){
    log_fail("device '%s' is in use. Cannot remove\n", name);
    return -EBUSY;
  }
  list_del(&dev->list);
  if (dev->uid)
    free(dev->uid);
  free(dev->path);
  free(dev->name);
  free(dev);
  
  return 0;
}

int get_dev_names(char **buffer, uint32_t *list_size)
{
  import_info_t *ptr;
  dev_info_t *dev;
  list_t *list_item;
  int count = 0;

  *buffer = NULL;
  list_foreach(list_item, &device_list){
    dev = list_entry(list_item, dev_info_t, list);
    if ((dev->flags & GNBD_FLAGS_INVALID) == 0)
      count++;
  }
  if (count == 0){
    *list_size = 0;
    return 0;
  }
  ptr = (import_info_t *)malloc(sizeof(import_info_t) * count);
  if (!ptr){
    log_err("cannot allocate memory for names replay\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (uint32_t)(sizeof(import_info_t) * count);
  list_foreach(list_item, &device_list){
    dev = list_entry(list_item, dev_info_t, list);
    if (dev->flags & GNBD_FLAGS_INVALID)
      continue;
    strncpy(ptr->name, dev->name, 32);
    ptr->name[31] = 0;
    ptr->timeout = (uint32_t)dev->timeout;
    ptr->flags = (dev->flags & GNBD_FLAGS_READONLY)? GNBD_READ_ONLY : 0;
    ptr++;
  }
  
  return 0;
}

int get_dev_uid(char *name, char **buffer, uint32_t *size)
{
  dev_info_t *dev;

  *buffer = NULL;
  *size = 0;
  dev = find_device(name);
  if (!dev)
    return -ENODEV;
  if (!dev->uid)
    return 0;
  *buffer = dev->uid;
  *size = buflen(*buffer);
  return 0;
}


int get_dev_info(char **buffer, uint32_t *list_size)
{
  info_req_t *ptr;
  dev_info_t *dev;
  list_t *list_item;
  int count = 0;

  *buffer = NULL;
  list_foreach(list_item, &device_list)
    count++;
  if (count == 0){
    *list_size = 0;
    return 0;
  }
  ptr = (info_req_t *)malloc(sizeof(info_req_t) * count);
  if (!ptr){
    log_err("cannot allocate memory for info replay\n");
    return -ENOMEM;
  }
  *buffer = (char *)ptr;
  *list_size = (uint32_t)(sizeof(info_req_t) * count);
  list_foreach(list_item, &device_list){
    dev = list_entry(list_item, dev_info_t, list);
    ptr->sectors = (uint64_t)dev->sectors;
    ptr->timeout = (uint32_t)dev->timeout;
    ptr->flags = (uint8_t)dev->flags;
    strncpy(ptr->name, dev->name, 32);
    ptr->name[31] = 0;
    strncpy(ptr->path, dev->path, 1024);
    ptr->path[1023] = 0;
    if (dev->uid){
      strncpy(ptr->uid, dev->uid, 64);
      ptr->uid[63] = 0;
    } else
      ptr->uid[0] = 0;
    ptr++;
  }

  return 0;
}
