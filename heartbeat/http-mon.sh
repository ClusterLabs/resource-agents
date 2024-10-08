#
# General http monitor code
# (sourced by apache and httpmon)
#
# Author:	Alan Robertson
#		Sun Jiang Dong
#
# Support:	users@clusterlabs.org
#
# License:	GNU General Public License (GPL)
#
# Copyright:	(C) 2002-2005 International Business Machines
#

# default options for http clients
# NB: We _always_ test a local resource, so it should be
# safe to connect from the local interface.
bind_address="127.0.0.1"
curl_ipv6_opts=""
if ocf_is_true "$OCF_RESKEY_use_ipv6" || echo "$STATUSURL" | grep -qs "::"; then
	bind_address="::1"
	curl_ipv6_opts="-g"
fi
WGETOPTS="-O- -q --no-proxy --bind-address=$bind_address"
CURLOPTS="-o - -Ss -L --noproxy '*' --interface $bind_address $curl_ipv6_opts"

request_url_header() {
	which curl >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		curl -IL --connect-timeout 5 --interface lo $curl_ipv6_opts "$1" > /dev/null 2>&1
		return $?
	fi

	which wget >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		local header=$(wget --server-response --spider --timeout=5 --tries=2 "$1" 2>&1)
		if [ $? -eq 0 ]; then
			return $OCF_SUCCESS
		fi

		# a 4xx error is still a server response.
		echo "$header" | grep "HTTP/1.1 4.. " > /dev/null 2>&1
		return $?
	fi
	return $OCF_ERR_GENERIC
}

#
# run the http client
#
curl_func() {
	cl_opts="$CURLOPTS $test_httpclient_opts"
	if [ x != "x$test_user" ]; then
		echo "-u $test_user:$test_password" |
			curl -K - $cl_opts "$1"
	else
		curl $cl_opts "$1"
	fi
}
wget_func() {
	# -L not implemented in wget2
	wget -V | grep -q "^GNU Wget2 " || WGETOPTS="$WGETOPTS -L"
	auth=""
	cl_opts="$WGETOPTS $test_httpclient_opts"
	[ x != "x$test_user" ] &&
		auth="--http-user=$test_user --http-passwd=$test_password"
	wget $auth $cl_opts "$1"
}
#
# rely on whatever the user provided
userdefined() {
	$test_httpclient $test_httpclient_opts "$1"
}

#
# find a good http client
#
findhttpclient() {
	# prefer curl (see b2ca07d)
	if [ "x$CLIENT" != x ] && which "$CLIENT" >/dev/null 2>&1; then
		echo "$CLIENT"
	elif which curl >/dev/null 2>&1; then
		echo "curl"
	elif which wget >/dev/null 2>&1; then
		echo "wget"
	else
		return 1
	fi
}
gethttpclient() {
	[ -z "$test_httpclient" ] &&
		test_httpclient=$ourhttpclient
	case "$test_httpclient" in
		curl|wget) echo ${test_httpclient}_func;;  #these are supported
		*) echo userdefined;;
	esac
}

# test configuration good?
is_testconf_sane() {
	if [ "x$test_regex" = x -o "x$test_url" = x ]; then
		ocf_log err "test regular expression or test url empty"
		return 1
	fi
	if [ "x$test_user$test_password" != x -a \( "x$test_user" = x -o "x$test_password" = x \) ]; then
		ocf_log err "bad user authentication for extended test"
		return 1
	fi
	return 0
}
#
# read the test definition from the config
#
readtestconf() {
	test_name="$1" # we look for this one or the first one if empty
	lcnt=0
	readdef=""
	test_url="" test_regex=""
	test_user="" test_password=""
	test_httpclient="" test_httpclient_opts=""

	while read key value; do
		lcnt=$((lcnt+1))
		if [ "$readdef" ]; then
			case "$key" in
			"url") test_url="$value" ;;
			"user") test_user="$value" ;;
			"password") test_password="$value" ;;
			"client") test_httpclient="$value" ;;
			"client_opts") test_httpclient_opts="$value" ;;
			"match") test_regex="$value" ;;
			"end") break ;;
			"#"*|"") ;;
			*) ocf_log err "$lcnt: $key: unknown keyword"; return 1 ;;
			esac
		else
			[ "$key" = "test" ] &&
				[ -z "$test_name" -o "$test_name" = "$value" ] &&
				readdef=1
		fi
	done
}
