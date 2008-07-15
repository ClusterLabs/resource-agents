#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <endian.h>
#include <byteswap.h>

#include "old_parser.h"

#define DEFAULT_BBS 16384 /* default basic block size */


static int upgrade_file_archive(char *location);
static int upgrade_device_archive(char *location);

int upgrade(char *location){
  struct stat stat_buf;

  if(stat(location, &stat_buf)){
    fprintf(stderr, "Unable to stat %s: %s\n", location, strerror(errno));
    return -errno;
  }

  if(S_ISBLK(stat_buf.st_mode)){
    return upgrade_device_archive(location);
  } else {
    return upgrade_file_archive(location);
  }
}

static int _upgrade_file_archive(int fd){
  int error = 0;
  ccs_node_t *cluster_cn = NULL;
  ccs_node_t *fence_cn = NULL;
  ccs_node_t *nodes_cn = NULL;
  ccs_node_t *tmp_cn;

  if((error = parse_ccs_file(fd, &cluster_cn, "cluster.ccs"))){
    return error;
  }

  if((error = parse_ccs_file(fd, &nodes_cn, "nodes.ccs"))){
    return error;
  }

  if((error = parse_ccs_file(fd, &fence_cn, "fence.ccs"))){
    return error;
  }

  tmp_cn = find_ccs_node(cluster_cn, "cluster/name", '/');
  if(!tmp_cn || !tmp_cn->v){
    fprintf(stderr, "Unable to find cluster name.\n");
    error = -EINVAL;
    goto fail;
  }

  printf("<cluster name=\"%s\" config_version=\"1\">\n", tmp_cn->v->v.str);

  /* No cman on upgrade
  printf("\n<cman>\n");
  printf("</cman>\n");
  */

  tmp_cn = find_ccs_node(cluster_cn, "cluster/lock_gulm/servers", '/');
  printf("\n<gulm>\n");
  if(tmp_cn && tmp_cn->v){
    ccs_value_t *server;
    for(server = tmp_cn->v; server; server = server->next)
      printf("    <lockserver name=\"%s\"/>\n", server->v.str);
  }
  printf("</gulm>\n");

  printf("\n<clusternodes>\n");

  for(tmp_cn = nodes_cn->child; tmp_cn; tmp_cn = tmp_cn->sib){
    ccs_node_t *fence, *method, *device, *params;

    printf("\n  <clusternode name=\"%s\" votes=\"1\">\n", tmp_cn->key);
    for(fence = tmp_cn->child; fence; fence = fence->sib){
      if(!strcmp(fence->key, "fence")){
	printf("    <fence>\n");
	for(method = fence->child; method; method = method->sib){
	  printf("      <method name=\"%s\">\n", method->key);
	  for(device = method->child; device; device=device->sib){
	    printf("        <device name=\"%s\"", device->key);
	    for(params = device->child; params; params = params->sib){
	      if (params->v->type == CCS_STRING)
		printf(" %s=\"%s\"", params->key, params->v->v.str);
	      else if (params->v->type == CCS_INT)
		printf(" %s=\"%d\"", params->key, params->v->v.i);
	      else if (params->v->type == CCS_FLOAT)
		printf(" %s=[DECIMAL NOT CONVERTED]", params->key);
	      else
		printf(" %s=[BAD TYPE CONVERSION]", params->key);
	    }
	    printf("/>\n");
	  }
	  printf("      </method>\n");
	}
	printf("    </fence>\n");
      }
    }
    printf("  </clusternode>\n");
  }

  printf("\n</clusternodes>\n");

  printf("\n<fencedevices>\n");
  for(tmp_cn = fence_cn->child; tmp_cn; tmp_cn = tmp_cn->sib){
    ccs_node_t *params;

    printf("  <fencedevice name=\"%s\"", tmp_cn->key);
    for(params = tmp_cn->child; params; params = params->sib){
      printf(" %s=\"%s\"", params->key, params->v->v.str);
    }
    printf("/>\n");
  }

  printf("\n</fencedevices>\n");

  printf("</cluster>\n");

 fail:
  if(cluster_cn) free_ccs_node(cluster_cn);
  if(fence_cn) free_ccs_node(fence_cn);
  if(nodes_cn) free_ccs_node(nodes_cn);
  return error;
}

static int upgrade_file_archive(char *location){
  int error = 0;
  int fd=-1;
  
  if((fd = open(location, O_RDONLY)) < 0){
    fprintf(stderr, "Unable to open %s: %s\n", location, strerror(errno));
    error = -errno;
    goto fail;
  }
  
  error = _upgrade_file_archive(fd);

 fail:
  if(fd >= 0) close(fd);
  return error;
}


#define CCS_DH_MAGIC  0x122473

/* CCS Device Header */
typedef struct ccs_dh_s {
  u_int32_t dh_magic;
  u_int32_t dh_format;
  u_int32_t dh_generation;
  u_int32_t dh_active;
  u_int32_t dh_size;     /* Size of archive on disk */
  u_int32_t dh_maxsize;
  char   dh_reserved[64];
} ccs_dh_t;

static void ccs_dh_in(ccs_dh_t *dh, char *buf){
  ccs_dh_t *tmp = (ccs_dh_t *)buf;

#if __BYTE_ORDER == __BIG_ENDIAN
  dh->dh_magic   = tmp->dh_magic;
  dh->dh_active  = tmp->dh_active;
  dh->dh_size    = tmp->dh_size;
  dh->dh_maxsize = tmp->dh_maxsize;
#else
  dh->dh_magic   = bswap_32(tmp->dh_magic);
  dh->dh_active  = bswap_32(tmp->dh_active);
  dh->dh_size    = bswap_32(tmp->dh_size);
  dh->dh_maxsize = bswap_32(tmp->dh_maxsize);
#endif
}

static int upgrade_device_archive(char *location){
  int error = 0;
  int dev_fd=-1, tmp_fd=-1;
  char tmp_file[64];
  void *buffer = NULL;
  char *buffer_p;
  ccs_dh_t dev_header;

  if(posix_memalign(&buffer, DEFAULT_BBS, DEFAULT_BBS)){
    fprintf(stderr, "Unable to allocate aligned memory.\n");
    return -ENOMEM;
  }
  buffer_p = (char *)buffer;

  if((dev_fd = open(location, O_RDONLY | O_DIRECT)) < 0){
    fprintf(stderr, "Unable to open %s: %s\n", location, strerror(errno));
    error = -errno;
    goto fail;
  }

  if(read(dev_fd, buffer_p, DEFAULT_BBS) < DEFAULT_BBS){
    fprintf(stderr, "Unable to read %s: %s\n", location, strerror(errno));
    error = -errno;
    goto fail;
  }

  ccs_dh_in(&dev_header, buffer_p);
  if(dev_header.dh_magic == CCS_DH_MAGIC){
    if(!dev_header.dh_active){
      if(lseek(dev_fd, dev_header.dh_maxsize, SEEK_SET)< 0){
	fprintf(stderr, "Unable to lseek in %s: %s\n", location, strerror(errno));
	error = -errno;
	goto fail;
      }
    }
  } else {
    fprintf(stderr, "The specified device does not contain a CCS archive.\n");
    error = -EINVAL;
    goto fail;
  }

  sprintf(tmp_file, "/tmp/tmp_%d", getpid());

  tmp_fd = open(tmp_file, O_RDWR | O_CREAT |O_TRUNC, S_IRUSR|S_IWUSR);
  if(tmp_fd < 0){
    fprintf(stderr, "Unable to create temporary archive: %s\n", strerror(errno));
    error = -errno;
    goto fail;
  }
  unlink(tmp_file);

  while(dev_header.dh_size){
    int write_size;
    if(read(dev_fd, buffer_p, DEFAULT_BBS) < DEFAULT_BBS){
      fprintf(stderr, "Bad read on %s: %s.\n", location, strerror(errno));
      error = -errno;
      goto fail;
    }
    write_size = (dev_header.dh_size < DEFAULT_BBS) ? 
      dev_header.dh_size : DEFAULT_BBS;
    if(write(tmp_fd, buffer_p, write_size) < write_size){
      fprintf(stderr, "Unable to write to temporary archive: %s.\n", strerror(errno));
      error = -errno;
      goto fail;
    }
    dev_header.dh_size -= write_size;
  }
  lseek(tmp_fd, 0, SEEK_SET);

  error = _upgrade_file_archive(tmp_fd);
  
 fail:
  if(buffer_p) free(buffer_p);
  if(dev_fd >= 0) close(dev_fd);
  if(tmp_fd >= 0) close(tmp_fd);
  return error;
}

