TARGET += Makefile \
	  META.yml

all: $(TARGET)
	${MAKE} LD_RUN_PATH="";

%.pm: $(S)/%.pm.in
	cat $< | \
	sed \
		-e 's/@VERSION@/${RELEASE_VERSION}/g' \
	> $@

%.yml: $(S)/%.yml.in
	cat $< | \
	sed \
		-e 's/@VERSION@/${RELEASE_VERSION}/g' \
	> $@

Makefile: META.yml $(PMTARGET)
	perl Makefile.PL INC='$(CFLAGS)' LIBS='$(LDFLAGS)' INSTALLDIRS=vendor

install:
	${MAKE} -f Makefile install

uninstall:
	echo uninstall target not supported yet

clean:
	-${MAKE} -f Makefile clean
	rm -f $(TARGET) Makefile.old
