#!/bin/sh

# Easy peasy test for findif, configuration via direct edit...
# Jan Pokorny <jpokorny@redhat.com>

export LC_ALL=C
test -n "$BASH_VERSION" && set -o posix
set -u
COLOR=0
if [ -t 1 ] && echo -e foo | grep -Eqv "^-e"; then
	COLOR=1
else
	COLOR=0
fi
ok () {
	[ $COLOR -eq 1 ] \
	    && echo -en "[\033[32m OK \033[0m]" \
	    || echo -n "[ OK ]"
	echo " $*"
}
fail () {
	[ $COLOR -eq 1 ] \
	    && echo -en "[\033[31mFAIL\033[0m]" \
	    || echo -n "[FAIL]"
	echo " $*"
}
info () {
	[ $COLOR -eq 1 ] \
	    && echo -e "\033[34m$@\033[0m" \
	    || echo "$*"
}
die() { echo "$*"; exit 255; }
warn() { echo "> $*"; }
mimic_return () { return $1; }
verbosely () { echo "$1..."; $1; }

HERE="$(dirname "$0")"
. "${HERE}/../heartbeat/ocf-returncodes"  # obtain OCF_ERR_CONFIGURED et al.

#
# soft-config
#

: ${DEBUG_IN:=0}
: ${DEBUG_OUT:=0}

: "${PRG:=${HERE}/findif}"
: "${SCRIPT:=${HERE}/../heartbeat/findif.sh}"

: ${LO_IF:=lo}
: ${LO_IP4:=127.0.0.1}
: ${LO_NM4:=8}
: ${LO_BC4:=127.255.255.255}
: ${LO_IP6:=::1}

: ${DUMMY_IF:=dummy0}
# carefully selected to fit TEST-NET-2
# http://en.wikipedia.org/wiki/Reserved_IP_addresses#Reserved_IPv4_addresses
: ${DUMMY_IP4:=198.51.100.1}
: ${DUMMY_NM4:=24}
: ${DUMMY_BC4:=198.51.100.255}

: ${DUMMY_IP6:=2001:db8::1}
: ${DUMMY_NM6:=32}
: ${DUMMY_BC6:=198.51.100.255}

#
# hard-wired
#

PRG_CMD="${PRG} -C"
SCRIPT_CMD="$(head -n1 "${SCRIPT}" | sed 's|#!||') \
	-c \"export OCF_FUNCTIONS_DIR=$(dirname "${SCRIPT}"); . $(dirname "${SCRIPT}")/ocf-shellfuncs; . ${SCRIPT}; findif\""

DUMMY_USER=test-findif

LO_IP4_13=${LO_IP4%.[0-9]*}
LO_IP4_4=${LO_IP4##[0-9][0-9]*.}
LO_IP4_INC=${LO_IP4_13}.$((LO_IP4_4 + 1))

DUMMY_IP4_13=${DUMMY_IP4%.[0-9]*}
DUMMY_IP4_4=${DUMMY_IP4##[0-9][0-9]*.}
DUMMY_IP4_INC=${DUMMY_IP4_13}.$((DUMMY_IP4_4 + 1))

DUMMY_IP6_17=${DUMMY_IP6%:[0-9A-Fa-f:][0-9A-Fa-f:]*}
DUMMY_IP6_8=${DUMMY_IP6##[0-9A-Fa-f][0-9A-Fa-f]*:}
DUMMY_IP6_INC=${DUMMY_IP6_17}::$((DUMMY_IP6_8 + 1))

#
# command-line config
#

CMD="${PRG_CMD}"
TESTS="4"

#
# test declarations
#

TEST_FORMAT=\
"   OCF_RESKEY_ip	, OCF_RESKEY_cidr_netmask	, expected_ec		, expected_dev	, expected_nm		, expected_bc"

TEST_DATA4=\
"   # valid: 1-9: loopback, 10-19: dummy if; invalid cases: 20-29: ip, 30-39: netmask bits
    #  1) LO4_IP
    ${LO_IP4}		, 				, $OCF_SUCCESS		, ${LO_IF}	, ${LO_NM4}	, ${LO_BC4}
    #  2) LO4_IP+1
    ${LO_IP4_INC}	, 				, $OCF_SUCCESS		, ${LO_IF}	, ${LO_NM4}	, ${LO_BC4}
    #
    # 10) DUMMY4_IP+1
    ${DUMMY_IP4_INC}	, 				, $OCF_SUCCESS		, ${DUMMY_IF}	, ${DUMMY_NM4}	, ${DUMMY_BC4}
    # 11) DUMMY4_IP+1, explicit netmask
    ${DUMMY_IP4_INC}	, ${DUMMY_NM4}			, $OCF_SUCCESS		, ${DUMMY_IF}	, ${DUMMY_NM4}	, ${DUMMY_BC4}
    #
    # 20) *invalid* IPv4 (missing last item in quad)
    ${DUMMY_IP4_13}.	,				, $OCF_ERR_CONFIGURED	, NA		, NA		, NA
    # 21) *invalid* IP (random string)
    foobar		,				, $OCF_ERR_CONFIGURED	, NA		, NA		, NA
    #
    # 30) DUMMY4_IP+1, explicit *invalid* netmask (33)
    ${DUMMY_IP4_INC}	, 33				, $OCF_ERR_CONFIGURED	, NA		, NA		, NA
    # 31) DUMMY4_IP+1, explicit *invalid* netmask (-1)
    ${DUMMY_IP4_INC}	, -1				, $OCF_ERR_CONFIGURED	, NA		, NA		, NA
    # 32) DUMMY4_IP+1, explicit *invalid* netmask (random string)
    ${DUMMY_IP4_INC}	, foobar			, $OCF_ERR_CONFIGURED	, NA		, NA		, NA
"

TEST_DATA6=\
"   # valid: A0-A9: loopback, B0-B9: dummy if; invalid cases: C0-C9: ip, D0-D9: netmask bits
    # A0) LO6_IP
    ${LO_IP6}		, 				, $OCF_SUCCESS		, ${LO_IF}	, ${LO_NM4}	,
    #
    # B0) DUMMY6_IP+1
    ${DUMMY_IP6_INC}	, 				, $OCF_SUCCESS		, ${DUMMY_IF}	, ${DUMMY_NM6}	,
    # B1) DUMMY4_IP+1, explicit netmask
    ${DUMMY_IP6_INC}	, ${DUMMY_NM6}			, $OCF_SUCCESS		, ${DUMMY_IF}	, ${DUMMY_NM6}	,
"

#
# private routines
#

_get_test_field () {
	echo "$1" | cut -s -d, -f$2 | sed 's|^ \?\(.*[^ ]\) \?$|\1|g'
}

#
# public routines
#

setup () {
	if [ "$(uname -o)" != "GNU/Linux" ]; then
		die "Only tested with Linux, feel free to edit the condition."
	fi

	if [ "${CMD}" = "${PRG_CMD}" ]; then
		[ -x "${PRG}" ] || die "Forgot to compile ${PRG} for me to test?"
	fi

	if [ $(id -u) -ne 0 ]; then
		die "Due to (unobtrusive) juggling with routing, run as root."
	fi

	if ! useradd -M ${DUMMY_USER} 2>/dev/null; then
		if ! getent passwd ${DUMMY_USER} >/dev/null; then
			die "Cannot add user ${DUMMY_USER} for testing purposes."
		fi
		warn "User ${DUMMY_USER} already exists, be careful not" \
		     " to confirm its removal."
	fi

	if lsmod | grep -Eq "^dummy[ ]+"; then
		warn "Looks like you already use dummy network device."
	else
		modprobe dummy || die "No dummy kernel module (per name) at hand."
	fi

	#if ! ifconfig ${DUMMY_IF} ${DUMMY_IP4}/${DUMMY_NM4}; then
	#if ! ip addr add ${DUMMY_IP4}/${DUMMY_NM4} broadcast + dev ${DUMMY_IF}; then
	if ! ip addr add ${DUMMY_IP4}/${DUMMY_NM4} dev ${DUMMY_IF}; then
		die "Cannot add IPv4 address (${DUMMY_IP4}/${DUMMY_NM4}) to ${DUMMY_IF}."
	fi

	# TODO: IPv6 support check first, disabling 6'ish tests if negative?
	if ! ip addr add ${DUMMY_IP6}/${DUMMY_NM6} dev ${DUMMY_IF}; then
		die "Cannot add IPv6 address (${DUMMY_IP6}/${DUMMY_NM6}) to ${DUMMY_IF}."
	fi

	if ! ip link set ${DUMMY_IF} up; then
		die "Cannot bring ${DUMMY_IF} up."
	fi
}

teardown () {
	while true; do
		echo -n "Remove user ${DUMMY_USER}? [y/n] "
		case $(read l < /dev/tty; echo "${l}") in
		n)
			break
			;;
		y)
			[ "${DUMMY_USER}" = "$(whoami)" ] && break
			userdel ${DUMMY_USER} || warn "Cannot kick user ${DUMMY_USER} out."
			break
			;;
		esac
	done

	rmmod dummy || warn "Cannot kick dummy kernel module out."
}

proceed () {
	err_cnt=0
	(for test in ${TESTS}; do eval "echo \"\${TEST_DATA${test}}\""; done
	tty 2>&1 >/dev/null || cat | sed 's|^\(.*\)$|# stdin: \1\n\1|') \
	  | while read curline; do
		if echo "${curline}" | grep -Eqv -e '^[ \t]*#' -e '^$'; then
			err_this=0
			curline="$(echo "${curline}" | tr '\t' ' ' | tr -s ' ')"
			info "$(echo "${lastline}" | sed 's|^#|------|')"

			ip="$(     _get_test_field "${curline}" 1)"
			[ -z "${ip}" ]      &&  die "test spec.: empty ip"
			mask="$(   _get_test_field "${curline}" 2)"
			[ -z "${mask}" ]    && warn "test spec.: empty mask"
			exp_ec="$( _get_test_field "${curline}" 3)"
			[ -z "${exp_ec}" ]  && warn "test spec.: empty exp_ec"
			exp_dev="$(_get_test_field "${curline}" 4)"
			[ -z "${exp_dev}" ] && warn "test spec.: empty exp_dev"
			exp_nm="$( _get_test_field "${curline}" 5)"
			[ -z "${exp_nm}" ]  && warn "test spec.: empty exp_nm"
			exp_bc="$( _get_test_field "${curline}" 6)"
			[ -z "${exp_bc}" ]  && warn "test spec.: empty exp_bc"
			[ $DEBUG_IN -ne 0 ] \
			    && echo "${ip}, ${mask}, ${exp_ec}, ${exp_dev}, ${exp_nm}, ${exp_bc}"

			env="OCF_RESKEY_ip=${ip} OCF_RESKEY_cidr_netmask=${mask}"
			echo "${env}"
			res="$(su ${DUMMY_USER} -c "${env} ${CMD} 2>&1")"
			got_ec=$?

			if [ ${got_ec} -ne ${exp_ec} ]; then
				warn "FAIL exit code: ${got_ec} vs ${exp_ec}"
				err_this=$((err_this+1))
			fi

			res="$(echo "${res}" | tr '\t' ' ' | tr -s ' ')"

			res_check=
			stop=0
			echo "${res}" | while read res_line && [ $stop -eq 0 ]; do
				echo "${res_line}" | grep -Eq "^[0-9A-Za-z_-]+ netmask [0-9]"
				if [ $? -ne 0 ]; then
					if [ -z "${res_line}" ] || echo "${res_line}" \
					  | grep -Fq "Copyright Alan Robertson"; then
						# findif finishes w/ (C)+usage on failure
						stop=1
					else
						echo "${res_line}"
					fi
				elif [ -n "${res_check}" ]; then
					warn "More than one line with results."
					stop=1
				else
					res_check=${res_line}
					got_dev="$(echo "${res_check}" | cut -d' ' -f1)"
					got_nm="$(echo "${res_check}"  | cut -d' ' -f3)"
					got_bc="$(echo "${res_check}"  | cut -d' ' -f5)"
					[ ${DEBUG_OUT} -ne 0 ] \
					    && echo "${got_dev}, ${got_nm}, ${got_bc}"

					if [ "${got_dev}" != "${exp_dev}" ]; then
						warn "FAIL device: ${got_dev} vs ${exp_dev}"
						err_this=$((err_this+1))
					fi
					if [ "${got_nm}" != "${exp_nm}" ]; then
						warn "FAIL netmask: ${got_nm} vs ${exp_nm}"
						err_this=$((err_this+1))
					fi
					if [ "${got_bc}" != "${exp_bc}" ]; then
						warn "FAIL broadcast: ${got_bc} vs ${exp_bc}"
						err_this=$((err_this+1))
					fi
				fi

				mimic_return ${err_this}  # workaround separate process limitation
			done

			err_this=$?
			if [ ${err_this} -eq 0 ]; then
				ok
			else
				fail
				err_cnt=$((err_cnt+1))
			fi
		fi
		lastline="${curline}"
		mimic_return ${err_cnt}  # workaround separate process limitation
	done
	err_cnt=$?

	echo "--- TOTAL ---"
	[ $err_cnt -eq 0 ] && ok || fail $err_cnt
	return $err_cnt
}

if [ $# -ge 1 ]; then
	while true; do
		case $1 in
		-script)
			CMD="${SCRIPT_CMD}"
			[ $# -eq 1 ] && break
			;;
		--)
			TESTS=
			[ $# -eq 1 ] && break
			;;
		-4)
			TESTS="4"
			[ $# -eq 1 ] && break
			;;
		-6)
			TESTS="6"
			[ $# -eq 1 ] && break
			;;
		-46)
			TESTS="4 6"
			[ $# -eq 1 ] && break
			;;
		setup|proceed|teardown)
			verbosely $1
			ret=$?
			[ $ret -ne 0 ] && exit $ret
			;;
		*)
			echo "usage: ./$0 [-script] [--|-4|-6|-46] (setup,proceed,teardown)"
			echo "additional tests may be piped to standard input, format:"
			echo "${TEST_FORMAT}" | tr '\t' ' ' | tr -s ' '
			exit 0
			;;
		esac
		[ $# -eq 1 ] && exit 0
		shift
	done
fi

verbosely setup
verbosely proceed
ret=$?
verbosely teardown

exit $ret
