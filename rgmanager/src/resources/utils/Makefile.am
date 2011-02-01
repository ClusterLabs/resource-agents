MAINTAINERCLEANFILES	= Makefile.in

TARGET			= config-utils.sh

commonscripts		= fs-lib.sh \
			  httpd-parse-config.pl \
			  member_util.sh \
			  messages.sh \
			  named-parse-config.pl \
			  ra-skelet.sh \
			  tomcat-parse-config.pl

EXTRA_DIST		= $(commonscripts) \
			  $(TARGET).in

rasutilsdir		= ${CLUSTERDATA}/utils

rasutils_SCRIPTS	= $(commonscripts) \
			  $(TARGET)


$(TARGET): $(TARGET).in
	cat $^ | sed \
		-e 's#@''CONFDIR@#${CONFDIR}#g' \
		-e 's#@''CONFFILE@#${CONFFILE}#g' \
	> $@

clean-local:
	rm -f $(TARGET)
