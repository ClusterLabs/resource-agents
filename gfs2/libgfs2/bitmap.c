/* Basic bitmap manipulation */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "libgfs2.h"

#define BITMAP_SIZE(size, cpb) (size / cpb)
#define BITMAP_SIZE1(size) (size >> 3)
#define BITMAP_SIZE4(size) (size >> 1)

#define BITMAP_BYTE_OFFSET(x, map) ((x % map->chunks_per_byte) \
                                    * map->chunksize )

/* BITMAP_BYTE_OFFSET1 is for chunksize==1, which implies chunks_per_byte==8 */
/* Reducing the math, we get:                                                */
/* #define BITMAP_BYTE_OFFSET1(x) ((x % 8) * 1)                              */
/* #define BITMAP_BYTE_OFFSET1(x) (x % 8)                                    */
/* #define BITMAP_BYTE_OFFSET1(x) (x & 0x0000000000000007)                   */
#define BITMAP_BYTE_OFFSET1(x) (x & 0x0000000000000007)

/* BITMAP_BYTE_OFFSET4 is for chunksize==4, which implies chunks_per_byte==2 */
/* Reducing the math, we get:                                                */
/* #define BITMAP_BYTE_OFFSET4(x) ((x % 2) * 4)                              */
/* #define BITMAP_BYTE_OFFSET4(x) ((x & 0x0000000000000001) * 4)             */
/* #define BITMAP_BYTE_OFFSET4(x) ((x & 0x0000000000000001) << 2)            */
#define BITMAP_BYTE_OFFSET4(x) ((x & 0x0000000000000001) << 2)

#define BITMAP_MASK(chunksize) ((2 << (chunksize - 1)) - 1)
/* BITMAP_MASK1 is  for chunksize==1                                         */
/* Reducing the math, we get:                                                */
/* #define BITMAP_MASK1(chunksize) ((2 << (1 - 1)) - 1)                      */
/* #define BITMAP_MASK1(chunksize) ((2 << 0) - 1)                            */
/* #define BITMAP_MASK1(chunksize) ((2) - 1)                                 */
#define BITMAP_MASK1(chunksize) (1)

/* BITMAP_MASK4 is  for chunksize==4                                         */
/* #define BITMAP_MASK(chunksize) ((2 << (4 - 1)) - 1)                       */
/* #define BITMAP_MASK(chunksize) ((2 << 3) - 1)                             */
/* #define BITMAP_MASK(chunksize) (0x10 - 1)                                 */
#define BITMAP_MASK4(chunksize) (0xf)

uint64_t gfs2_bitmap_size(struct gfs2_bmap *bmap) {
	return bmap->size;
}

int gfs2_bitmap_create(struct gfs2_bmap *bmap, uint64_t size,
					   uint8_t chunksize)
{
	if((((chunksize >> 1) << 1) != chunksize) && chunksize != 1)
		return -1;
	if(chunksize > 8)
		return -1;
	bmap->chunksize = chunksize;
	bmap->chunks_per_byte = 8 / chunksize;

	bmap->size = size;

	/* Have to add 1 to BITMAP_SIZE since it's 0-based and mallocs
	 * must be 1-based */
	bmap->mapsize = BITMAP_SIZE(size, bmap->chunks_per_byte)+1;

	if(!(bmap->map = malloc(sizeof(char) * bmap->mapsize)))
		return -ENOMEM;
	if(!memset(bmap->map, 0, sizeof(char) * bmap->mapsize)) {
		free(bmap->map);
		bmap->map = NULL;
		return -ENOMEM;
	}
	return 0;
}

int gfs2_bitmap_set(struct gfs2_bmap *bmap, uint64_t offset, uint8_t val)
{
	static char *byte;
	static uint64_t b;

	if(offset < bmap->size) {
		if (bmap->chunksize == 1) {
			byte = bmap->map + BITMAP_SIZE1(offset);
			b = BITMAP_BYTE_OFFSET1(offset);
			*byte |= (val & BITMAP_MASK1(bmap->chunksize));
		} else {
			byte = bmap->map + BITMAP_SIZE4(offset);
			b = BITMAP_BYTE_OFFSET4(offset);
			*byte |= (val & BITMAP_MASK4(bmap->chunksize)) << b;
		}
		return 0;
	}
	return -1;
}

int gfs2_bitmap_get(struct gfs2_bmap *bmap, uint64_t bit, uint8_t *val)
{
	static char *byte;
	static uint64_t b;

	if(bit < bmap->size) {
		if (bmap->chunksize == 1) {
			byte = bmap->map + BITMAP_SIZE1(bit);
			b = BITMAP_BYTE_OFFSET1(bit);
			*val = (*byte & (BITMAP_MASK1(bmap->chunksize) << b )) >> b;
		} else {
			byte = bmap->map + BITMAP_SIZE4(bit);
			b = BITMAP_BYTE_OFFSET4(bit);
			*val = (*byte & (BITMAP_MASK4(bmap->chunksize) << b )) >> b;
		}
		return 0;
	}
	return -1;
}

int gfs2_bitmap_clear(struct gfs2_bmap *bmap, uint64_t offset)
{
	static char *byte;
	static uint64_t b;

	if(offset < bmap->size) {
		if (bmap->chunksize == 1) {
			byte = bmap->map + BITMAP_SIZE1(offset);
			b = BITMAP_BYTE_OFFSET1(offset);
			*byte &= ~(BITMAP_MASK1(bmap->chunksize) << b);
		} else {
			byte = bmap->map + BITMAP_SIZE4(offset);
			b = BITMAP_BYTE_OFFSET4(offset);
			*byte &= ~(BITMAP_MASK4(bmap->chunksize) << b);
		}
		return 0;
	}
	return -1;

}

void gfs2_bitmap_destroy(struct gfs2_bmap *bmap)
{
	if(bmap->map)
		free(bmap->map);
	bmap->size = 0;
	bmap->mapsize = 0;
	bmap->chunksize = 0;
	bmap->chunks_per_byte = 0;
}
