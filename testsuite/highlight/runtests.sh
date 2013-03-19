#!/bin/bash

cd `dirname $0`

LASTLOG_TIME=`find testlog.txt -printf '%TF@%TT' 2>/dev/null`
if [ $? -eq 0 ] ; then
        mv -f testlog.txt "testlog_$LASTLOG_TIME.txt"
fi

[ -d work ] || mkdir work

(
	cd work
	for i in `LANG=C ls ../tests/*`; do
		echo "==== Testcase $i ===="
		../_runtest.sh "$i" 2>&1
	done
) > testlog.txt

if ! diff -q testlog.txt expectedTestlog.txt 2>&1 >/dev/null ; then
	echo "!! Output was not what was expected"
	exit 1
fi

echo "Testsuite passed correctly"
exit 0
