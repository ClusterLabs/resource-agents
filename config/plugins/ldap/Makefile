TARGET= config_ldap.lcrso

LCRSOT=$(TARGET)

DOCS =	99cluster.ldif \
	example.ldif

all: ${TARGET}

include ../../../make/defines.mk
include $(OBJDIR)/make/cobj.mk
include $(OBJDIR)/make/clean.mk
include $(OBJDIR)/make/install.mk
include $(OBJDIR)/make/uninstall.mk

CFLAGS += -fPIC
CFLAGS += -I${ldapincdir}
CFLAGS += -I${incdir}

LDFLAGS += -L${ldaplibdir} -lldap
LDFLAGS += -L${libdir}

OBJS=	configldap.o

${TARGET}: ${OBJS}
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDFLAGS)

clean: generalclean 

-include $(OBJS:.o=.d)
