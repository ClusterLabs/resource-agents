TARGET= fence_ack_manual

SBINDIRT=$(TARGET)

all: ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk


# Using manual override instead of the binary version.
# (this version does not require any configuration params
# in cluster.conf, but fencing must fail first)
${TARGET}: 
	cp $(S)/$@.sh $@

clean: generalclean
