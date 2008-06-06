#ifndef __GFS_IOCTL_DOT_H__
#define __GFS_IOCTL_DOT_H__

#define _GFSC_(x)               (('G' << 8) | (x))

/* Ioctls implemented */

#define GFS_IOCTL_IDENTIFY      _GFSC_(35)
#define GFS_IOCTL_SUPER         _GFSC_(45)

struct gfs_ioctl {
        unsigned int gi_argc;
        char **gi_argv;

        char __user *gi_data;
        unsigned int gi_size;
        uint64_t gi_offset;
};

#ifdef CONFIG_COMPAT
struct gfs_ioctl_compat {
	unsigned int gi_argc;
	uint32_t gi_argv;

	uint32_t gi_data;
	unsigned int gi_size;
	uint64_t gi_offset;
};
#endif

#endif /* ___GFS_IOCTL_DOT_H__ */
