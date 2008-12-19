include ../make/defines.mk
include $(OBJDIR)/make/passthrough.mk

SUBDIRS = lib dlm_controld

ifndef enable_pacemaker
SUBDIRS += libgfscontrol gfs_control gfs_controld tool daemon man
endif
