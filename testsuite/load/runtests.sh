#!/bin/bash

RETVAL=0
for i in ../../src/data/*.lang ; do
	../../src.util/t3highlight --language-file=$i <(echo) > /dev/null 2>.loadlog.txt
	if [ $? -ne 0 ] ; then
		echo -e '\033[31;1mFailed to load $i\033[0m'
		sed -r 's/^/  /' .loadlog.txt
		RETVAL=1
	fi
done
rm .loadlog.txt
if [ "$RETVAL" -eq 0 ] ; then
	echo "Testsuite passed correctly"
fi
exit $RETVAL
