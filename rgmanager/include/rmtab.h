/** @file
 * Header for rmtab.c.
 */
/*
 * Author: Lon H. Hohberger <lhh at redhat.com>
 */
#ifndef _RMTAB_H
#define _RMTAB_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Shamelessly ripped from nfs-utils */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

#ifndef _PATH_RMTAB
#define _PATH_RMTAB "/var/lib/nfs/rmtab"
#endif

/* private types */
#ifndef MSG_RMTAB_UPDATE
#define MSG_RMTAB_UPDATE 978
#endif

#ifndef MSG_RMTAB_BOOT
#define MSG_RMTAB_BOOT 603
#endif

/* Just a mem-size trim for now */
#define __MAXPATHLEN 1024

/**
 * rmtab node list entry.
 *
 * This contains all the information necessary to reconstruct a line in
 * /var/lib/nfs/rmtab.
 */
typedef struct _rmtab_node {
	struct _rmtab_node *rn_next;	/**< Next pointer */
	char *rn_hostname;		/**< Mount entry hostname */
	char *rn_path;			/**< Export mounted */
	uint32_t rn_count;		/**< Number of times export is
					     mounted. */
} rmtab_node;


/*
 * list addition (insert)
 */
int __rmtab_insert(rmtab_node **head, rmtab_node *rnew);
rmtab_node *rmtab_insert(rmtab_node **head, rmtab_node *pre, char *host,
			 char *path, int count);

/*
 * list deletion/removal/etc.
 */
rmtab_node *__rmtab_remove(rmtab_node **head, rmtab_node *entry);
rmtab_node *rmtab_remove(rmtab_node **head, char *host, char *path);
void rmtab_kill(rmtab_node **head);

/*
 * diff/merge functions
 */
int rmtab_diff(rmtab_node *old, rmtab_node *new, rmtab_node **diff);
int rmtab_merge(rmtab_node **head, rmtab_node *patch);

/*
 * Read/write/import/export...
 */
int rmtab_import(rmtab_node **head, FILE *fp);
int rmtab_export(rmtab_node *head, FILE *fp);

int rmtab_read(rmtab_node **head, char *filename);
int rmtab_write_atomic(rmtab_node *head, char *filename);

/*
 * Translation to/from network block [array] style
 */
int rmtab_pack(char *dest, rmtab_node *head);
int rmtab_unpack(rmtab_node **dest, char *src, size_t srclen);

/*
 * utility functions
 */
int rmtab_cmp_min(rmtab_node *left, rmtab_node *right);
int rmtab_cmp(rmtab_node *left, rmtab_node *right);
size_t rmtab_pack_size(rmtab_node *head);
int rmtab_move(rmtab_node **dest, rmtab_node **src);

/*
 * DEBUG junk
 */
#ifdef DEBUG
int rmtab_dump(rmtab_node *head);
#endif

#endif
