include make/defines.mk


REALSUBDIRS = gfs-kernel/src/gfs \
	      common cman/lib config cman dlm fence/libfenced group \
	      fence gfs gfs2 rgmanager bindings doc \
	      contrib

SUBDIRS = $(filter-out \
	  $(if ${without_common},common) \
	  $(if ${without_gfs-kernel/src/gfs},gfs-kernel/src/gfs) \
	  $(if ${without_config},config) \
	  $(if ${without_cman},cman/lib) \
	  $(if ${without_cman},cman) \
	  $(if ${without_dlm},dlm) \
	  $(if ${without_fence},fence/libfenced) \
	  $(if ${without_group},group) \
	  $(if ${without_fence},fence) \
	  $(if ${without_gfs},gfs) \
	  $(if ${without_gfs2},gfs2) \
	  $(if ${without_rgmanager},rgmanager) \
	  $(if ${without_bindings},bindings) \
	  , $(REALSUBDIRS))

all: ${SUBDIRS}

${SUBDIRS}:
	[ -n "${without_$@}" ] || ${MAKE} -C $@ all

# Kernel

gfs-kernel: gfs-kernel/src/gfs

# Dependencies

common:
config: cman/lib
cman: common config
dlm: config
fence/libfenced:
group: cman dlm fence/libfenced
fence: group
gfs:
gfs2: group
rgmanager: cman dlm
bindings: cman
contrib: gfs2

oldconfig:
	@if [ -f $(OBJDIR)/.configure.sh ]; then \
		sh $(OBJDIR)/.configure.sh; \
	else \
		echo "Unable to find old configuration data"; \
	fi

install:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

uninstall:
	set -e && for i in ${SUBDIRS}; do ${MAKE} -C $$i $@; done

clean:
	set -e && for i in ${REALSUBDIRS}; do \
		contrib_code=1 \
		legacy_code=1 \
		${MAKE} -C $$i $@;\
	done

distclean: clean
	rm -f make/defines.mk
	rm -f .configure.sh
	rm -f *tar.gz
	rm -rf build

.PHONY: ${REALSUBDIRS}
