#!/bin/bash

if [ $# -eq 0 ] ; then
	echo "Usage: _runtest.sh [-n<num>] <test file>"
	exit 0
fi

if [ "${PWD##*/}" != work ] ; then
	echo "Execute this script in the work subdirectory"
	exit 1
fi

unset TESTNR
while [ $# -gt 1 ] ; do
	case "$1" in
		-n*)
			TESTNR="`printf \"test%02d\"  \"${1#-n}\"`"
			shift
			;;
		*)
			echo "Error in command line: $1"
			exit 1
			;;
	esac
done

fail() {
	echo "$@" >&2
	exit 1
}

csplit "$1" -ftest -z -s '/^#TEST/' '{*}' 2>/dev/null
mv test00 pattern
sed -i '/^#TEST/d' test*

failed=0
for i in test[0-9][0-9] ; do
	if [ -n "$TESTNR" ] && [ "$i" != "$TESTNR" ] ; then
		continue
	fi

	csplit $i -z -s '/^==/' {*} 2>/dev/null
	sed -i '/^==/d' xx*
	../../../src.util/t3highlight pattern xx00 > out
	if diff -u xx01 out ; then
		let failed++
	fi
	if [ -n "$TESTNR" ] && [ "$i" == "$TESTNR" ] ; then
		break
	fi
done

if [ "$failed" -ne 0 ] ; then
	echo "$failed tests failed"
	exit 1
fi
exit 0
