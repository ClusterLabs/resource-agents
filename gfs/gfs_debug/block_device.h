#ifndef __BLOCK_DEVICE_DOT_H__
#define __BLOCK_DEVICE_DOT_H__


void find_device_size(void);
char *get_block(uint64_t bn, int fatal);

void print_size(void);
void print_hexblock(void);
void print_rawblock(void);


#endif /* __BLOCK_DEVICE_DOT_H__ */

