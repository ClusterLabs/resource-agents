TARGET= libdlm

LIBDIRT=$(TARGET).a \
	$(TARGET)_lt.a \
	$(TARGET).so.${SOMAJOR}.${SOMINOR} \
	$(TARGET)_lt.so.${SOMAJOR}.${SOMINOR}

LIBSYMT=$(TARGET).so \
	$(TARGET)_lt.so \
	$(TARGET).so.$(SOMAJOR) \
	$(TARGET)_lt.so.$(SOMAJOR)

INCDIRT=$(TARGET).h

UDEVT=51-dlm.rules

include ../../make/defines.mk

SHAREDLIB=$(TARGET).so.${SOMAJOR}.${SOMINOR} $(TARGET)_lt.so.${SOMAJOR}.${SOMINOR}
STATICLIB=$(TARGET).a $(TARGET)_lt.a

all: $(STATICLIB) $(SHAREDLIB)

include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

AISTARGET= libaislock

CFLAGS += -I$(S)
CFLAGS += -I${incdir}
CFLAGS += -I$(KERNEL_SRC)/include

PTHREAD_LDFLAGS += -lpthread 

$(TARGET).a: $(TARGET).o $(AISTARGET).o
	${AR} cru $@ $^
	${RANLIB} $@

$(TARGET)_lt.a: $(TARGET)_lt.o
	${AR} cru $@ $^
	${RANLIB} $@

$(TARGET).so.${SOMAJOR}.${SOMINOR}: $(TARGET).po $(AISTARGET).po
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(SOMAJOR) $< $(PTHREAD_LDFLAGS) $(LDFLAGS)
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so.$(SOMAJOR)

$(TARGET)_lt.so.${SOMAJOR}.${SOMINOR}: $(TARGET)_lt.po
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET)_lt.so.$(SOMAJOR) $< $(LDFLAGS)
	ln -sf $(TARGET)_lt.so.$(SOMAJOR).$(SOMINOR) $(TARGET)_lt.so
	ln -sf $(TARGET)_lt.so.$(SOMAJOR).$(SOMINOR) $(TARGET)_lt.so.$(SOMAJOR)

%_lt.o: $(S)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

%_lt.po: $(S)/%.c
	$(CC) $(CFLAGS) -fPIC -c -o $@ $<

%.o: $(S)/%.c
	$(CC) $(CFLAGS) -D_REENTRANT -c -o $@ $<

%.po: $(S)/%.c
	$(CC) $(CFLAGS) -fPIC -D_REENTRANT -c -o $@ $<

clean: generalclean

-include $(TARGET).d
-include $(TARGET)_lt.d
-include $(AISTARGET).d
