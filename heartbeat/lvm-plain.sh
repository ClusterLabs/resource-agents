# lvm-plain.sh
#
# Description: LVM management with no VG protection
#
#
# Author:      Dejan Muhamedagic
# Support:     users@clusterlabs.org
# License:     GNU General Public License (GPL)
# Copyright:   (C) 2017 Dejan Muhamedagic
#

#
# interface to the LVM RA
#

# apart from the standard vgchange options,
# this is mostly a template
# please copy and modify appropriately
# when adding new VG protection mechanisms

# lvm_init sets the vgchange options:
#   vgchange_activate_options
#   vgchange_deactivate_options
# (for both activate and deactivate)

lvm_init() {
	vgchange_activate_options="-aly"
	vgchange_deactivate_options="-aln"
	# for clones (clustered volume groups), we'll also have to force
	# monitoring, even if disabled in lvm.conf.
	if ocf_is_clone; then
		vgchange_activate_options="$vgchange_activate_options --monitor y"
	fi
}

lvm_validate_all() {
	: nothing to validate
}

lvm_status() {
	return 0
}

lvm_pre_activate() {
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
	return $rc
}

# vim:tabstop=4:shiftwidth=4:textwidth=0:wrapmargin=0
