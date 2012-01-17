#!/bin/bash

if ! cd "`dirname \"$0\"`" ; then
	echo "Could not change to correct directory"
	exit
fi

cat testlog.txt | sed '/^Output was not what was expected !!/d' > expectedTestlog.txt
