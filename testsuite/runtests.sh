#!/bin/bash

for i in */runtests.sh ; do
	echo "Running testsuite `dirname $i`"
	(
		cd `dirname $i`
		./runtests.sh
	)
done
