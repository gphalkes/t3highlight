#/bin/sh

cd "`dirname \"$0\"`"

diff -u expectedTestlog.txt testlog.txt | \
sed -r -e 's/(^-.*)/\x1b[0;32m\1\x1b[0;30m/' -e \
	's/(^\+.*)/\x1b[1;31m\1\x1b[0;30m/' | less -R -S
