#!/bin/bash

for i in */cleanlogs.sh ; do
	(
		cd `dirname $i`
		./cleanlogs.sh
	)
done
