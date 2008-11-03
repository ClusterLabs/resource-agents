include ../../../make/defines.mk

TARGET= fence_scsi fence_scsi_test scsi_reserve_notify
INITDT= scsi_reserve
NOTIFYD= scsi_reserve_notify

include $(OBJDIR)/make/fencebuild.mk

$(NOTIFYD): $(S)/$(NOTIFYD).in
	cat $(S)/$(NOTIFYD).in | sed \
		-e 's#@INITDT@#${initddir}#g' \
	> $(NOTIFYD)
