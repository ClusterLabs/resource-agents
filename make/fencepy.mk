all: $(TARGET)

$(TARGET):
	: > $(TARGET)
	awk "{print}(\$$1 ~ /#BEGIN_VERSION_GENERATION/){exit 0}" $(S)/$(TARGET).py >> $(TARGET)
	echo "RELEASE_VERSION=\"${RELEASE_VERSION}\";" >> $(TARGET)
	${DEF2VAR} ${SRCDIR}/config/copyright.cf sh REDHAT_COPYRIGHT >> $(TARGET)
	echo "BUILD_DATE=\"(built `date`)\";" >> $(TARGET)
	awk -v p=0 "(\$$1 ~ /#END_VERSION_GENERATION/){p = 1} {if(p==1)print}" $(S)/$(TARGET).py >> $(TARGET)
	chmod +x $(TARGET)

install: all
	if [ ! -d ${sbindir} ]; then \
		install -d ${sbindir}; \
	fi
	install -m755 ${TARGET} ${sbindir}

uninstall:
	${UNINSTALL} ${TARGET} ${sbindir}

clean:
	rm -f $(TARGET)
