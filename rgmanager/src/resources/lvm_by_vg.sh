#!/bin/bash

# vg_owner
#
# Returns:
#    1 == We are the owner
#    2 == We can claim it
#    0 == Owned by someone else
function vg_owner
{
	local owner=`vgs -o tags --noheadings $OCF_RESKEY_vg_name`
	local my_name=$(local_node_name)

	if [ -z $my_name ]; then
		ocf_log err "Unable to determine cluster node name"
		return 0
	fi

	if [ -z $owner ]; then
		# No-one owns this VG yet, so we can claim it
		return 2
	fi

	if [ $owner != $my_name ]; then
		if is_node_member_clustat $owner ; then
			return 0
		fi
		return 2
	fi

	return 1
}

function strip_tags
{
	local i

	for i in `vgs --noheadings -o tags $OCF_RESKEY_vg_name | sed s/","/" "/g`; do
		ocf_log info "Stripping tag, $i"
		vgchange --deltag $i $OCF_RESKEY_vg_name
	done

	if [ ! -z `vgs -o tags --noheadings $OCF_RESKEY_vg_name` ]; then
		ocf_log err "Failed to remove ownership tags from $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

function strip_and_add_tag
{
	if ! strip_tags; then
		ocf_log err "Failed to remove tags from volume group, $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
	fi

	vgchange --addtag $(local_node_name) $OCF_RESKEY_vg_name
	if [ $? -ne 0 ]; then
		ocf_log err "Failed to add ownership tag to $OCF_RESKEY_vg_name"
		return $OCF_ERR_GENERIC
        fi

	ocf_log info "New tag \"$(local_node_name)\" added to $OCF_RESKEY_vg_name"

	return $OCF_SUCCESS
}

function vg_status_clustered
{
	return $OCF_SUCCESS
}

# vg_status
#
# Are all the LVs active?
function vg_status_single
{
	local i
	local dev
	local my_name=$(local_node_name)

	#
	# Check that all LVs are active
	#
	for i in `lvs $OCF_RESKEY_vg_name --noheadings -o attr`; do
		if [[ ! $i =~ ....a. ]]; then
			return $OCF_ERR_GENERIC
		fi
	done

	#
	# Check if all links/device nodes are present
	#
	for i in `lvs $OCF_RESKEY_vg_name --noheadings -o name`; do
		dev="/dev/$OCF_RESKEY_vg_name/$i"

		if [ -h $dev ]; then
			realdev=$(readlink -f $dev)
			if [ $? -ne 0 ]; then
				ocf_log err "Failed to follow link, $dev"
				return $OCF_ERR_GENERIC
			fi

			if [ ! -b $realdev ]; then
				ocf_log err "Device node for $dev is not present"
				return $OCF_ERR_GENERIC
			fi
		else
			ocf_log err "Symbolic link for $lv_path is not present"
			return $OCF_ERR_GENERIC
		fi
	done

	#
	# Verify that we are the correct owner
	#
	vg_owner
	if [ $? -ne 1 ]; then
		ocf_log err "WARNING: $OCF_RESKEY_vg_name should not be active"
		ocf_log err "WARNING: $my_name does not own $OCF_RESKEY_vg_name"
		ocf_log err "WARNING: Attempting shutdown of $OCF_RESKEY_vg_name"

		# FIXME: may need more force to shut this down
		vgchange -an $OCF_RESKEY_vg_name
		return $OCF_ERR_GENERIC
	fi

	return $OCF_SUCCESS
}

##
# Main status function for volume groups
##
function vg_status
{
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		vg_status_clustered
	else
		vg_status_single
	fi
}

function vg_verify
{
	# Anything to verify?
	return $OCF_SUCCESS
}

function vg_start_clustered
{
	local a
	local results
	local all_pvs
	local resilience

	ocf_log info "Starting volume group, $OCF_RESKEY_vg_name"

	if ! vgchange -aey $OCF_RESKEY_vg_name; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_vg_name"
		ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name"

		if ! vgreduce --removemissing $OCF_RESKEY_vg_name; then
			ocf_log err "Failed to make $OCF_RESKEY_vg_name consistent"
			return $OCF_ERR_GENERIC
		fi

		if ! vgchange -aey $OCF_RESKEY_vg_name; then
			ocf_log err "Failed second attempt to activate $OCF_RESKEY_vg_name"
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Second attempt to activate $OCF_RESKEY_vg_name successful"
		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings 2> /dev/null $OCF_RESKEY_vg_name`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
			        for i in ${all_pvs[*]}; do
			                resilience=$resilience'"a|'$i'|",'
        			done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange -aey $OCF_RESKEY_vg_name $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z $resilience ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
		                        ocf_log err "Failed to activate $OCF_RESKEY_vg_name"
                		        return $OCF_ERR_GENERIC
                		fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_vg_name slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

function vg_start_single
{
	local a
	local results
	local all_pvs
	local resilience

	ocf_log info "Starting volume group, $OCF_RESKEY_vg_name"

	vg_owner
	case $? in
	0)
		ocf_log info "Someone else owns this volume group"
		return $OCF_ERR_GENERIC
		;;
	1)
		ocf_log info "I own this volume group"
		;;
	2)
		ocf_log info "I can claim this volume group"
		;;
	esac

	if ! strip_and_add_tag ||
	   ! vgchange -ay $OCF_RESKEY_vg_name; then
		ocf_log err "Failed to activate volume group, $OCF_RESKEY_vg_name"
		ocf_log notice "Attempting cleanup of $OCF_RESKEY_vg_name"

		if ! vgreduce --removemissing --config \
			"activation { volume_list = \"$OCF_RESKEY_vg_name\" }" \
			$OCF_RESKEY_vg_name; then

			ocf_log err "Failed to make $OCF_RESKEY_vg_name consistent"
			return $OCF_ERR_GENERIC
		fi

		vg_owner
		if [ $? -eq 0 ]; then
			ocf_log err "Unable to claim ownership of $OCF_RESKEY_vg_name"
			return $OCF_ERR_GENERIC
		fi

		if ! strip_and_add_tag ||
		   ! vgchange -ay $OCF_RESKEY_vg_name; then
			ocf_log err "Failed second attempt to activate $OCF_RESKEY_vg_name"
			return $OCF_ERR_GENERIC
		fi

		ocf_log notice "Second attempt to activate $OCF_RESKEY_vg_name successful"
		return $OCF_SUCCESS
	else
		# The activation commands succeeded, but did they do anything?
		# Make sure all the logical volumes are active
		results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
		a=0
		while [ ! -z ${results[$a]} ]; do
			if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
				all_pvs=(`pvs --noheadings -o name 2> /dev/null`)
				resilience=" --config devices{filter=["
			        for i in ${all_pvs[*]}; do
			                resilience=$resilience'"a|'$i'|",'
        			done
				resilience=$resilience"\"r|.*|\"]}"

				vgchange -ay $OCF_RESKEY_vg_name $resilience
				break
			fi
			a=$(($a + 2))
		done

		#  We need to check the LVs again if we made the command resilient
		if [ ! -z $resilience ]; then
			results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name $resilience 2> /dev/null`)
			a=0
			while [ ! -z ${results[$a]} ]; do
				if [[ ! ${results[$(($a + 1))]} =~ ....a. ]]; then
		                        ocf_log err "Failed to activate $OCF_RESKEY_vg_name"
                		        return $OCF_ERR_GENERIC
                		fi
				a=$(($a + 2))
			done
			ocf_log err "Orphan storage device in $OCF_RESKEY_vg_name slowing operations"
		fi
	fi

	return $OCF_SUCCESS
}

##
# Main start function for volume groups
##
function vg_start
{
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		vg_start_clustered
	else
		vg_start_single
	fi
}

function vg_stop_clustered
{
	local a
	local results
	typeset self_fence=""

	case ${OCF_RESKEY_self_fence} in
		"yes")          self_fence=1 ;;
		1)              self_fence=1 ;;
		*)              self_fence="" ;;
	esac

	#  Shut down the volume group
	#  Do we need to make this resilient?
	vgchange -aln $OCF_RESKEY_vg_name

	#  Make sure all the logical volumes are inactive
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			if [ "$self_fence" ]; then
				ocf_log err "Unable to deactivate $lv_path REBOOT"
				sync
				reboot -fn
			else
				ocf_log err "Logical volume $OCF_RESKEY_vg_name/${results[$a]} failed to shutdown"
			fi
			return $OCF_ERR_GENERIC
		fi
		a=$(($a + 2))
	done

	return $OCF_SUCCESS
}

function vg_stop_single
{
	local a
	local results
	typeset self_fence=""

	case ${OCF_RESKEY_self_fence} in
		"yes")          self_fence=1 ;;
		1)              self_fence=1 ;;
		*)              self_fence="" ;;
	esac

	#  Shut down the volume group
	#  Do we need to make this resilient?
	vgchange -an $OCF_RESKEY_vg_name

	#  Make sure all the logical volumes are inactive
	results=(`lvs -o name,attr --noheadings $OCF_RESKEY_vg_name 2> /dev/null`)
	a=0
	while [ ! -z ${results[$a]} ]; do
		if [[ ${results[$(($a + 1))]} =~ ....a. ]]; then
			if [ "$self_fence" ]; then
				ocf_log err "Unable to deactivate $lv_path REBOOT"
				sync
				reboot -fn
			else
				ocf_log err "Logical volume $OCF_RESKEY_vg_name/${results[$a]} failed to shutdown"
			fi
			return $OCF_ERR_GENERIC
		fi
		a=$(($a + 2))
	done

	#  Make sure we are the owner before we strip the tags
	vg_owner
	if [ $? -ne 0 ]; then
		strip_tags
	fi

	return $OCF_SUCCESS
}

##
# Main stop function for volume groups
##
function vg_stop
{
	if [[ $(vgs -o attr --noheadings $OCF_RESKEY_vg_name) =~ .....c ]]; then
		vg_stop_clustered
	else
		vg_stop_single
	fi
}
