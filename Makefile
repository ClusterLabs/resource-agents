include make/defines.mk

REALSUBDIRS = gnbd-kernel/src gfs-kernel/src/gfs \
	      config cman ccs dlm fence/libfenced group fence gfs gfs2 gnbd rgmanager bindings

SUBDIRS = $(filter-out \
	  $(if ${without_gnbd-kernel/src},gnbd-kernel/src) \
	  $(if ${without_gfs-kernel/src/gfs},gfs-kernel/src/gfs) \
	  $(if ${without_cman},cman) \
	  $(if ${without_ccs},ccs) \
	  $(if ${without_dlm},dlm) \
	  $(if ${without_fence},fence/libfenced) \
	  $(if ${without_group},group) \
	  $(if ${without_fence},fence) \
	  $(if ${without_gfs},gfs) \
	  $(if ${without_gfs2},gfs2) \
	  $(if ${without_gnbd},gnbd) \
	  $(if ${without_rgmanager},rgmanager) \
	  $(if ${without_bindings},bindings) \
	  , $(REALSUBDIRS))

all: scripts ${SUBDIRS}

# Fix scripts permissions
scripts:
	chmod 755 ${SRCDIR}/scripts/*.pl ${SRCDIR}/scripts/fenceparse

${SUBDIRS}:
	[ -n "${without_$@}" ] || ${MAKE} -C $@ all

# Kernel

gnbd-kernel: gnbd-kernel/src
gfs-kernel: gfs-kernel/src/gfs

# Dependencies

config:
cman: config
ccs: cman
dlm: config
fence/libfenced:
group: cman dlm fence/libfenced
fence: group
gfs:
gfs2: group
gnbd: cman
rgmanager: cman dlm
bindings: cman

install: all
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

uninstall:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

clean:
	set -e && for i in ${REALSUBDIRS}; do ${MAKE} -C $$i $@; done

distclean: clean
	rm -f make/defines.mk
	rm -f *tar.gz
	rm -rf build

.PHONY: scripts ${REALSUBDIRS}
