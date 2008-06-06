TARGETS= client clientd

all: $(TARGETS)

include ../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk


%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean: generalclean
