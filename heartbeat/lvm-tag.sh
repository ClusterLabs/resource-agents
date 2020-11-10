# lvm-tag.sh
#
# Description: LVM management with tags
#
#
# Author:      David Vossel
#              Interface to LVM by Dejan Muhamedagic
# Support:     users@clusterlabs.org
# License:     GNU General Public License (GPL)
# Copyright:   (C) 2017 Dejan Muhamedagic
#

##
# Verify tags setup
##

verify_tags_environment()
{
	##
	# The volume_list must be initialized to something in order to
	# guarantee our tag will be filtered on startup
	##
	if ! lvm dumpconfig activation/volume_list; then
		ocf_log err  "LVM: Improper setup detected"
		ocf_exit_reason "The volume_list filter must be initialized in lvm.conf for exclusive activation without clvmd"
		return $OCF_ERR_GENERIC
	fi

	##
	# Our tag must _NOT_ be in the volume_list.  This agent
	# overrides the volume_list during activation using the
	# special tag reserved for cluster activation
	##
	if lvm dumpconfig activation/volume_list | grep -e "\"@$OUR_TAG\"" -e "\"${OCF_RESKEY_volgrpname}\""; then
		ocf_log err "LVM:  Improper setup detected"
		ocf_exit_reason "The volume_list in lvm.conf must not contain the cluster tag, \"$OUR_TAG\", or volume group, $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

check_initrd_warning()
{
	# First check to see if there is an initrd img we can safely
	# compare timestamps agaist.  If not, don't even bother with
	# this check.  This is known to work in rhel/fedora distros
	ls "/boot/*$(uname -r)*.img" > /dev/null 2>&1
	if [ $? -ne 0 ]; then
		return
	fi

	##
	# Now check to see if the initrd has been updated.
	# If not, the machine could boot and activate the VG outside
	# the control of pacemaker
	##
	if [ "$(find /boot -name *.img -newer /etc/lvm/lvm.conf)" = "" ]; then
		ocf_log warn "LVM:  Improper setup detected"
		ocf_log warn "* initrd image needs to be newer than lvm.conf"

		# While dangerous if not done the first time, there are many
		# cases where we don't simply want to fail here.  Instead,
		# keep warning until the user remakes the initrd - or has
		# it done for them by upgrading the kernel.
		#
		# initrd can be updated using this command.
		# dracut -H -f /boot/initramfs-$(uname -r).img $(uname -r)
		#
	fi
}

##
# does this vg have our tag
##
check_tags()
{
	local owner=`vgs -o tags --noheadings $OCF_RESKEY_volgrpname | tr -d ' '`

	if [ -z "$owner" ]; then
		# No-one owns this VG yet
		return 1
	fi

	if [ "$OUR_TAG" = "$owner" ]; then
		# yep, this is ours
		return 0
	fi

	# some other tag is set on this vg
	return 2
}

strip_tags()
{
	local i

	for i in `vgs --noheadings -o tags $OCF_RESKEY_volgrpname | sed s/","/" "/g`; do
		ocf_log info "Stripping tag, $i"

		# LVM version 2.02.98 allows changing tags if PARTIAL
		vgchange --deltag $i $OCF_RESKEY_volgrpname
	done

	if [ ! -z `vgs -o tags --noheadings $OCF_RESKEY_volgrpname | tr -d ' '` ]; then
		ocf_exit_reason "Failed to remove ownership tags from $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

set_tags()
{
	check_tags
	case $? in
	0)
		# we already own it.
		return $OCF_SUCCESS
		;;
	2)
		# other tags are set, strip them before setting
		if ! strip_tags; then
			return $OCF_ERR_GENERIC
		fi
		;;
	*)
		: ;;
	esac

	vgchange --addtag $OUR_TAG $OCF_RESKEY_volgrpname
	if [ $? -ne 0 ]; then
		ocf_exit_reason "Failed to add ownership tag to $OCF_RESKEY_volgrpname"
		return $OCF_ERR_GENERIC
	fi

	ocf_log info "New tag \"$OUR_TAG\" added to $OCF_RESKEY_volgrpname"
	return $OCF_SUCCESS
}

#
# interface to LVM
#

lvm_init() {
	OUR_TAG="pacemaker"
	if [ -n "$OCF_RESKEY_tag" ]; then
		OUR_TAG=$OCF_RESKEY_tag
	fi
	vgchange_activate_options="-aly --config activation{volume_list=[\"@${OUR_TAG}\"]}"
	vgchange_deactivate_options="-aln"
}

lvm_validate_all() {
	if ! verify_tags_environment; then
		exit $OCF_ERR_GENERIC
	fi
}

lvm_status() {
	local rc=0

	# If vg is running, make sure the correct tag is present. Otherwise we
	# can not guarantee exclusive activation.
	if ! check_tags; then
		ocf_exit_reason "WARNING: $OCF_RESKEY_volgrpname is active without the cluster tag, \"$OUR_TAG\""
		rc=$OCF_ERR_GENERIC
	fi

	# make sure the environment for tags activation is still valid
	if ! verify_tags_environment; then
		rc=$OCF_ERR_GENERIC
	fi
	# let the user know if their initrd is older than lvm.conf.
	check_initrd_warning

	return $rc
}

lvm_pre_activate() {
	if ! set_tags; then
		return $OCF_ERR_GENERIC
	fi
	return 0
}

lvm_post_activate() {
	local rc=$1
	return $rc
}

lvm_pre_deactivate() {
	return 0
}

lvm_post_deactivate() {
	local rc=$1
	if [ $rc -eq 0 ]; then
		strip_tags
		rc=$?
	fi
	return $rc
}

# vim:tabstop=4:shiftwidth=4:textwidth=0:wrapmargin=0
