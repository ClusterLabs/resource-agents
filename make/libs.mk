# handle objects
ifndef OBJS
	OBJS = $(TARGET).o
endif

# we always build the static version
ifndef STATICLIB
	STATICLIB = $(TARGET).a
endif

# handle the shared version
ifndef MAKESTATICLIB
	ifndef LIBDIRT
		LIBDIRT=$(TARGET).a \
			$(TARGET).so.$(SOMAJOR).$(SOMINOR)
	endif
	ifndef LIBSYMT
		LIBSYMT=$(TARGET).so \
			$(TARGET).so.$(SOMAJOR)
	endif
	ifndef INCDIRT
		INCDIRT=$(TARGET).h
	endif
	ifndef SHAREDLIB
		SHAREDLIB=$(TARGET).so.${SOMAJOR}.${SOMINOR}
	endif

all: $(STATICLIB) $(SHAREDLIB)

$(SHAREDLIB): $(OBJS)
	$(CC) -shared -o $@ -Wl,-soname=$(TARGET).so.$(SOMAJOR) $^ $(LDFLAGS)
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so
	ln -sf $(TARGET).so.$(SOMAJOR).$(SOMINOR) $(TARGET).so.$(SOMAJOR)

else

all: $(STATICLIB)

endif

$(STATICLIB): $(OBJS)
	${AR} cru $@ $^
	${RANLIB} $@

clean: generalclean

-include $(OBJS:.o=.d)
