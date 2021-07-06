#!/usr/bin/env bash
set -o pipefail
[[ "${DEBUG:-}" ]] && set -x

declare -i failed
failed=0

# SC2046: Quote this to prevent word splitting.
# SC1090: Can't follow non-constant source. Use a directive to specify location.
# SC2039: In POSIX sh, 'local' is undefined.
# SC2086: Double quote to prevent globbing and word splitting.
# SC2154: var is referenced but not assigned.
# SC1087: Use braces when expanding arrays.
ignored_errors="SC1090,SC2039,SC2154,SC1087"

success() {
	printf "\r\033[2K  [ \033[00;32mOK\033[0m ] Checking %s...\n" "$1"
}

warn() {
	printf "\r\033[2K  [\033[0;33mWARNING\033[0m] Checking %s...\n" "$1"
}

fail() {
	printf "\r\033[2K  [\033[0;31mFAIL\033[0m] Checking %s...\n" "$1"
	failed=$((failed + 1))
}

check() {
	local script="$1"

	out="$(shellcheck -s sh -f gcc -x -e "$ignored_errors" "$script" 2>&1)"
	rc=$?
	if [ $rc -eq 0 ]; then
		success "$script"
	elif echo "$out" | grep -i 'error' >/dev/null; then
		fail "$script"
	else
		warn "$script"
	fi
	echo "$out"
}

find_prunes() {
	local prunes="! -path './.git/*'"
	if [ -f .gitmodules ]; then
		while read -r module; do
			prunes="$prunes ! -path './$module/*'"
		done < <(grep path .gitmodules | awk '{print $3}')
	fi
	echo "$prunes"
}

find_cmd() {
	echo "find heartbeat -type f -and \( -perm /111 -or -name '*.sh' -or -name '*.c' -or -name '*.in' \) $(find_prunes)"
}

check_all_executables() {
	echo "Checking executables and .sh files..."
	while read -r script; do
		file --mime "$script" | grep 'charset=binary' >/dev/null 2>&1 && continue
		file --mime "$script" | grep 'text/x-python' >/dev/null 2>&1 && continue
		# upstream CI doesnt detect MIME-format correctly for Makefiles
		[[ "$script" =~ .*/Makefile.in ]] && continue

		if grep -qE "\<action.*(timeout|interval|delay)=\\\?\"[0-9]+\\\?\"" "$script"; then
			fail "$script: \"s\"-suffix missing in timeout, interval or delay"
		fi

		head=$(head -n1 "$script")
		[[ "$head" =~ .*python.* ]] && continue
		[[ "$head" =~ .*ruby.* ]] && continue
		[[ "$head" =~ .*zsh.* ]] && continue
		[[ "$head" =~ ^#compdef.* ]] && continue
		[[ "$script" =~ ^.*\.c ]] && continue
		[[ "$script" =~ ^.*\.in ]] && continue
		[[ "$script" =~ ^.*\.orig ]] && continue
		check "$script"

	done < <(eval "$(find_cmd)")
	if [ $failed -gt 0 ]; then
		echo "ci/build.sh: $failed failure(s) detected."
		exit 1
	fi
	exit 0
}

if [ "$1" != "check" ]; then
	./autogen.sh
	./configure
	make check
	[ $? -eq 0 ] || failed=$((failed + 1))
fi

check_all_executables
