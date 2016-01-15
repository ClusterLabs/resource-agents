get_rundir() {
	local rundir
	rundir="`mount | grep '/run ' | awk '{print $3}'`"
	echo ${rundir:-"/var/run"}
}

loopbackeddev() {
	local action file size ctlfile

	action="$1"
	file="$2"
	size="$3"
	ctlfile=$HA_RSCTMP/`echo $file | tr / _`
	
	case "$action" in
	start|setup|make)
		if [ ! -f "$ctlfile" ]; then
			if [ -z "$size" ]; then
				echo "usage: $0 action file size" >&2
				exit 1
			fi
			loopdev=`losetup -f`
			if ! dd if=/dev/zero of=$file bs=1 count=0 seek=$size 2>/dev/null; then
				echo "$0: dd failed" >&2
				exit 1
			fi
			if ! losetup $loopdev $file; then
				echo "$0: losetup failed" >&2
				exit 1
			fi
			echo $loopdev | tee $ctlfile
		else
			cat $ctlfile
		fi
	;;
	stop|undo|unmake)
		if [ -f "$ctlfile" ]; then
			losetup -d `cat $ctlfile`
			rm -f $file $ctlfile
		fi
	;;
	esac
}
