#ifndef __BASIC_DOT_H__
#define __BASIC_DOT_H__


EXTERN unsigned int sd_diptrs INIT(0);
EXTERN unsigned int sd_inptrs INIT(0);
EXTERN unsigned int sd_jbsize INIT(0);
EXTERN unsigned int sd_hash_bsize INIT(0);
EXTERN unsigned int sd_hash_ptrs INIT(0);
EXTERN uint32_t sd_max_height INIT(0);
EXTERN uint64_t sd_heightsize[GFS_MAX_META_HEIGHT];
EXTERN uint32_t sd_max_jheight INIT(0);
EXTERN uint64_t sd_jheightsize[GFS_MAX_META_HEIGHT];


void verify_gfs(void);
void must_be_gfs(void);
void scan_device(void);
void print_superblock(void);
void identify_block(void);

void print_dirents(char *data, unsigned int offset);


#endif /* __BASIC_DOT_H__ */

