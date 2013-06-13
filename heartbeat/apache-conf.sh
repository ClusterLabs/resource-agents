#
# Common apache code
# (sourced by apache)
#
# Author:	Alan Robertson
#		Sun Jiang Dong
#
# Support:	linux-ha@lists.linux-ha.org
#
# License:	GNU General Public License (GPL)
#
# Copyright:	(C) 2002-2005 International Business Machines
#

source_envfiles() {
	for f; do
		[ -f "$f" -a -r "$f" ] &&
			. "$f"
	done
}

apachecat() {
	awk '
	function procline() {
		split($0,a);
		if( a[1]~/^[Ii]nclude$/ ) {
			procinclude(a[2]);
		} else {
			if( a[1]=="ServerRoot" ) {
				rootdir=a[2];
				gsub("\"","",rootdir);
			}
			print;
		}
	}
	function printfile(infile, a) {
		while( (getline<infile) > 0 ) {
			procline();
		}
		close(infile);
	}
	function allfiles(dir, cmd,f) {
		cmd="find -L "dir" -type f";
		while( ( cmd | getline f ) > 0 ) {
			printfile(f);
		}
		close(cmd);
	}
	function listfiles(pattern, cmd,f) {
		cmd="ls "pattern" 2>/dev/null";
		while( ( cmd | getline f ) > 0 ) {
			printfile(f);
		}
		close(cmd);
	}
	function procinclude(spec) {
		if( rootdir!="" && spec!~/^\// ) {
			spec=rootdir"/"spec;
		}
		if( isdir(spec) ) {
			allfiles(spec); # read all files in a directory (and subdirs)
		} else {
			listfiles(spec); # there could be jokers
		}
	}
	function isdir(s) {
		return !system("test -d \""s"\"");
	}
	{ procline(); }
	' $1 |
	sed 's/#.*//;s/[[:blank:]]*$//;s/^[[:blank:]]*//' |
	grep -v '^$'
}

#
# set parameters (as shell vars) from our apache config file
#
get_apache_params() {
	configfile=$1
	shift 1
	vars=`echo $@ | sed 's/ /,/g'`

	eval `
	apachecat $configfile | awk -v vars="$vars" '
	BEGIN{
		split(vars,v,",");
		for( i in v )
			vl[i]=tolower(v[i]);
	}
	{
		for( i in v )
			if( tolower($1)==vl[i] ) {
			print v[i]"="$2
			delete vl[i]
			break
		}
	}
	'`
}

#
# Return the location(s) that are handled by the given handler
#
FindLocationForHandler() {
	PerlScript='while (<>) {
		/<Location "?([^ >"]+)/i && ($loc=$1);
		'"/SetHandler +$2"'/i && print "$loc\n"; 
	}'
	apachecat $1 | perl -e "$PerlScript"
}

#
# Check if the port is valid
#
CheckPort() {
	ocf_is_decimal "$1" && [ $1 -gt 0 ]
}

buildlocalurl() {
	[ "x$Listen" != "x" ] &&
	echo "http://${Listen}" ||
	echo "${LOCALHOST}:${PORT}"
}
# the test url may need a local prefix (as specified in the
# apache Listen directive)
fixtesturl() {
	echo $test_url | grep -qs "^http" && return
	test_url="`buildlocalurl`$test_url"
}
#
# Get all the parameters we need from the Apache config file
#
GetParams() {
	ConfigFile=$1
	if [ ! -f $ConfigFile ]; then
		return $OCF_ERR_INSTALLED
	fi
	get_apache_params $ConfigFile ServerRoot PidFile Port Listen
	case $PidFile in
		/*) ;;
		[[:alnum:]]*) PidFile=$ServerRoot/$PidFile;;
		*)
			# If the PidFile is not set in the config, set
			# a default location.
			PidFile=$HA_VARRUNDIR/${httpd_basename}.pid
			# Force the daemon to use this location by using
			# the -c option, which adds the PidFile directive
			# as if it was in the configuration file to begin with.
			PIDFILE_DIRECTIVE="true"
			;;
	esac

	for p in "$PORT" "$Port" 80; do
		if CheckPort "$p"; then
			PORT="$p"
			break
		fi
	done
 
	echo $Listen | grep ':' >/dev/null || # Listen could be just port spec
		Listen="localhost:$Listen"

	#
	# It's difficult to figure out whether the server supports
	# the status operation.
	# (we start our server with -DSTATUS - just in case :-))
	#
	# Typically (but not necessarily) the status URL is /server-status
	#
	# For us to think status will work, we have to have the following things:
	#
	# - The server-status handler has to be mapped to some URL somewhere
	#
	# We assume that:
	#
	# - the "main" web server at $PORT will also support it if we can find it
	#   somewhere in the file
	# - it will be supported at the same URL as the one we find in the file
	#
	# If this doesn't work for you, then set the statusurl attribute.
	#
	if
		 [ "X$STATUSURL" = "X" ]
	then
		StatusURL=`FindLocationForHandler $1 server-status | tail -1`
		STATUSURL="`buildlocalurl`$StatusURL"
	fi

	if ! test "$PidFile"; then
		return $OCF_ERR_INSTALLED
	else
		return $OCF_SUCCESS
	fi
}
