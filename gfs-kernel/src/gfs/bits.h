#ifndef __BITS_DOT_H__
#define __BITS_DOT_H__

#define BFITNOENT (0xFFFFFFFF)

void gfs_setbit(struct gfs_rgrpd *rgd,
		unsigned char *buffer, unsigned int buflen,
		uint32_t block, unsigned char new_state);
unsigned char gfs_testbit(struct gfs_rgrpd *rgd,
			  unsigned char *buffer, unsigned int buflen,
			  uint32_t block);
uint32_t gfs_bitfit(unsigned char *buffer, unsigned int buflen,
		    uint32_t goal, unsigned char old_state);
uint32_t gfs_bitcount(struct gfs_rgrpd *rgd,
		      unsigned char *buffer, unsigned int buflen,
		      unsigned char state);

#endif /* __BITS_DOT_H__ */
