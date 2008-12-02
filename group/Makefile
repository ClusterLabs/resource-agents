include ../make/defines.mk
include $(OBJDIR)/make/passthrough.mk

SUBDIRS = lib dlm_controld

ifndef without_gfs
SUBDIRS += libgfscontrol gfs_control gfs_controld
endif

ifndef enable_pacemaker
SUBDIRS += tool daemon man
endif
