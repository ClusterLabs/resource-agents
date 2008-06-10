include ../make/defines.mk

SUBDIRS=libgfs gfs_debug gfs_fsck gfs_grow gfs_jadd gfs_mkfs gfs_quota gfs_tool man init.d

all: ${SUBDIRS}

%:
	set -e && \
	for i in ${SUBDIRS}; do \
		${MAKE} -C $$i $@; \
	done; \
	if [ "$@" = "install" ]; then \
		mkdir -p ${DESTDIR}/sbin; \
		cd ${DESTDIR}/sbin; \
			ln -sf mount.gfs2 mount.gfs; \
		mkdir -p ${sbindir}; \
		cd ${sbindir}; \
			ln -sf gfs2_edit gfs_edit; \
	fi; \
	if [ "$@" = "uninstall" ]; then \
		cd ${DESTDIR}/sbin; rm -f mount.gfs; \
		cd ${sbindir}; rm -f gfs_edit; \
	fi
