#!/bin/bash

DIR="`dirname \"$0\"`"

for i in `hg manifest | egrep '^src/data' | egrep -v '^src/data/def' | sed -r "s%^%$DIR/../%"` ; do
	echo "Testing file $i"
	"$DIR/../src.util/run" --language-file="$i" "$0" > /dev/null
done
