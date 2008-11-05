include ../../../make/defines.mk

TARGET= fence_scsi fence_scsi_test scsi_reserve_notify scsi_reserve
INITDT= scsi_reserve
NOTIFYD= scsi_reserve_notify

include $(OBJDIR)/make/fencebuild.mk

$(NOTIFYD): $(S)/$(NOTIFYD).in
	cat $(S)/$(NOTIFYD).in | sed \
		-e 's#@INITDT@#${initddir}#g' \
	> $(NOTIFYD)

$(INITDT): $(S)/$(INITDT).in
	cat $(S)/$(INITDT).in | sed \
		-e 's#@INITDDIR@#${initddir}#g' \
	> $(INITDT)
