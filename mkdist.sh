#!/bin/bash

cd "`dirname \"$0\"`"

. ../../repo-scripts/mkdist_funcs.sh

setup_hg
get_version_hg
check_mod_hg
build_all
[ -z "${NOBUILD}" ] && { make -C doc clean ; make -C doc all ; }
get_sources_hg
make_tmpdir
copy_sources ${SOURCES} ${GENSOURCES} ${AUXSOURCES}
copy_dist_files
copy_files doc/API `hg manifest | egrep '^man/'`
create_configure

if [[ "${VERSION}" =~ [0-9]{8} ]] ; then
	VERSION_BIN=1
else
	VERSION_BIN="$(printf "0x%02x%02x%02x" $(echo ${VERSION} | tr '.' ' '))"
fi

sed -i "s/<VERSION>/${VERSION}/g" `find ${TOPDIR} -type f`
sed -i "/#define T3_HIGHLIGHT_VERSION/c #define T3_HIGHLIGHT_VERSION ${VERSION_BIN}" ${TOPDIR}/src/highlight.h

( cd ${TOPDIR}/src ; ln -s . t3highlight )

OBJECTS_LIB="`echo \"${SOURCES} ${GENSOURCES} ${AUXSOURCES}\" | tr ' ' '\n' | sed -r 's%\.objects/%%' | egrep '^src/[^/]*\.c$' | sed -r 's/\.c\>/.lo/g' | tr '\n' ' '`"
OBJECTS_T3HIGHLIGHT="`echo \"${SOURCES} ${GENSOURCES} ${AUXSOURCES}\" | tr ' ' '\n' | sed -r 's%\.objects/%%' | egrep '^src\.util/.*\.c$' | sed -r 's/\.c\>/.o/g' | tr '\n' ' '`"

#FIXME: somehow verify binary compatibility, and print an error if not compatible
LIBVERSION="${VERSIONINFO%%:*}"

sed -r -i "s%<LIBVERSION>%${LIBVERSION}%g" ${TOPDIR}/Makefile.in ${TOPDIR}/mk/*.in ${TOPDIR}/man/*

sed -r -i "s%<OBJECTS>%${OBJECTS_LIB}%g;\
s%<VERSIONINFO>%${VERSIONINFO}%g" ${TOPDIR}/mk/libt3highlight.in
sed -r -i "s%<OBJECTS>%${OBJECTS_T3HIGHLIGHT}%g" ${TOPDIR}/mk/t3highlight.in

create_tar
