#!/bin/bash

# Easy peasy test for findif, configuration via direct edit...
# Jan Pokorny <jpokorny@redhat.com>

set -u
ISATTY=$(tput colors &>/dev/null && echo 0 || echo 1)
ok () {
	[ $ISATTY -eq 0 ] \
	    && echo -ne "[\033[32m OK \033[0m]" \
	    || echo -n "[ OK ]"
	echo " $@"
}
fail () {
	[ $ISATTY -eq 0 ] \
	    && echo -ne "[\033[31mFAIL\033[0m]" \
	    || echo -n "[FAIL]"
	echo " $@"
}
info () {
	[ $ISATTY -eq 0 ] \
	    && echo -e "\033[34m$@\033[0m" \
	    || echo "$@"
}
mimic_return () { return $1; }


PRG=./findif

DUMMY_USER=test-findif

# carefully selected to fit TEST-NET-2
# http://en.wikipedia.org/wiki/Reserved_IP_addresses#Reserved_IPv4_addresses
DUMMY_IP=198.51.100.1
DUMMY_NM=255.255.255.0

# derived
DUMMY_IP_13=$(echo $DUMMY_IP | cut -d. -f1-3)
DUMMY_IP_4=$( echo $DUMMY_IP | cut -d. -f4  )
DUMMY_NM_13=$(echo $DUMMY_NM | cut -d. -f1-3)
DUMMY_NM_4=$( echo $DUMMY_NM | cut -d. -f4  )


TEST_DATA=\
"   # 1-9: valid cases, invalid cases: 10-19: ip, 20-29: netmask bits, 30-39: netmask
    # OCF_RESKEY_ip, OCF_RESKEY_cidr_netmask, ret, exp_dev, exp_nm, exp_bc
    #  1) loopback
    127.0.0.1,						,	0,	lo,	255.0.0.0,	127.255.255.255
    #  2) DUMMY_IP+1
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),			,	0,	dummy0,	255.255.255.0,	${DUMMY_IP_13}.255
    #  3) DUMMY_IP+1, explicit netmask bits
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		24,		0,	dummy0,	255.255.255.0,	${DUMMY_IP_13}.255
    #  4) DUMMY_IP+1, explicit netmask
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		${DUMMY_NM},	0,	dummy0,	255.255.255.0,	${DUMMY_IP_13}.255
    #
    # 10) *invalid* ip (missing last item in quad)
    ${DUMMY_IP_13}.,					,	6,	NA,	NA,		NA
    # 11) *invalid* ip (random string)
    foobar,						,	6,	NA,	NA,		NA
    # 20) DUMMY_IP+1, explicit *invalid* netmask bits (33)
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		33	,	6,	NA,	NA,		NA
    # 21) DUMMY_IP+1, explicit *invalid* netmask bits (-1)
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		-1	,	6,	NA,	NA,		NA
    # 22) DUMMY_IP+1, explicit *invalid* netmask bits (random string)
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		foobar	,	6,	NA,	NA,		NA
    # 30) DUMMY_IP+1, explicit *invalid* netmask (missing last item in quad)
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),	${DUMMY_NM_13}.	,	6,	NA,	NA,		NA
    # 30) DUMMY_IP+1, explicit *invalid* netmask (random string containing dot)
    ${DUMMY_IP_13}.$((DUMMY_IP_4 + 1)),		foo.bar	,	6,	NA,	NA,		NA
"


die() {
	echo "$@"
	exit
}

warn() {
	echo "$@"
}

setup() {
	echo $FUNCNAME...

	if [ "$(uname -o)" != "GNU/Linux" ]; then
		die "Only tested with Linux, feel free to edit the condition."
	fi

	[ -x ${PRG} ] || die "Forgot to compile ${PRG} for me to test?"

	if [ $(id -u) -ne 0 ]; then
		die "Due to (unobtrusive) juggling with routing, run as root."
	fi

	if ! useradd -M ${DUMMY_USER} 2>/dev/null; then
		die "Cannot add user ${DUMMY_USER} for testing purposes."
	fi

	if lsmod | grep -q "^dummy[ ]\+"; then
		warn "Looks like you already use dummy network device."
	else
		modprobe dummy || die "No dummy kernel module (per name) at hand."
	fi


	if ! ifconfig dummy0 ${DUMMY_IP} netmask ${DUMMY_NM}; then
		die "Cannot ifconfig dummy0"
	fi
}

teardown() {
	echo $FUNCNAME...

	userdel ${DUMMY_USER} || warn "Cannot kick user ${DUMMY_USER} out."
	rmmod dummy || warn "Cannot kick dummy kernel module out."
}

proceed() {
	echo $FUNCNAME...

	err_cnt=0
	echo "${TEST_DATA}" | while read curline; do
		if echo "${curline}" | grep -qv -e '^[ \t]*#' -e '^$'; then
			err_this=0
			curline="$(echo "${curline}" | tr '\t' ' ' | tr -s ' ')"
			info "$(echo "${lastline}" | sed 's|^#|------|')"

			ip="$(echo "${curline}"      | cut -d, -f1 | sed 's|^ *||g')"
			mask="$(echo "${curline}"    | cut -d, -f2 | sed 's|^ *||g')"
			exp_ec="$(echo "${curline}"  | cut -d, -f3 | sed 's|^ *||g')"
			exp_dev="$(echo "${curline}" | cut -d, -f4 | sed 's|^ *||g')"
			exp_nm="$(echo "${curline}"  | cut -d, -f5 | sed 's|^ *||g')"
			exp_bc="$(echo "${curline}"  | cut -d, -f6 | sed 's|^ *||g')"
			#echo "${ip}, ${mask}, ${exp_ret}, ${exp_dev}, ${exp_nm}"

			env="OCF_RESKEY_ip=${ip} OCF_RESKEY_cidr_netmask=${mask}"
			echo "${env}"
			res="$(su ${DUMMY_USER} -c "${env} ${PRG} 2>&1")"
			got_ec=$?

			res="$(echo "${res}" | tr '\t' ' ' | tr -s ' ')"
			res_check=
			echo "${res}" | while read res_line; do
				echo "${res_line}" | grep -q "^[a-z0-9]\+ netmask"
				if [ $? -ne 0 ]; then
					echo "${res_line}" | grep -q "Copyright Alan Robertson" \
					     && break
					echo "${res_line}"
				elif [ -z "${res_check}" ]; then
					res_check="${res_line}"
				else
					warn "More than one line with results."
				fi
			done

			if [ "${got_ec}" != "${exp_ec}" ]; then
				warn "FAIL exit code: ${got_ec} vs ${exp_ec}"
				let err_this+=1
			fi

			if [ -n "$res_check" ]; then
				got_dev="$(echo "${res_check}" | cut -d' ' -f1)"
				got_nm="$(echo "${res_check}"  | cut -d' ' -f3)"
				got_bc="$(echo "${res_check}"  | cut -d' ' -f5)"
				#echo "${got_dev}, ${got_nm}, ${got_bc}"

				if [ "${got_dev}" != "${exp_dev}" ]; then
					warn "FAIL device: ${got_dev} vs ${exp_dev}"
					let err_this+=1
				fi
				if [ "${got_nm}" != "${exp_nm}" ]; then
					warn "FAIL netmask: ${got_nm} vs ${exp_nm}"
					let err_this+=1
				fi
				if [ "${got_bc}" != "${exp_bc}" ]; then
					warn "FAIL broadcast: ${got_bc} vs ${exp_bc}"
					let err_this+=1
				fi
			fi

			if [ $err_this -eq 0 ]; then
				ok
			else
				fail
				let err_cnt+=1
			fi
		fi
		lastline="${curline}"
		mimic_return $err_cnt  # workaround separate process limitation
	done
	err_cnt=$?

	echo "--- TOTAL ---"
	[ $err_cnt -eq 0 ] && ok || fail $err_cnt
	return $err_cnt
}

if [ $# -ge 1 ]; then
	case $1 in
	setup|proceed|teardown)
		$1
		exit 0
		;;
	*)
		echo "usage: ./$0 [setup|proceed|teardown]"
		exit 0
		;;
	esac
fi

setup
proceed
ret=$?
teardown

exit $ret
