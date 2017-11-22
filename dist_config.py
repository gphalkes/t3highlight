import os

package = 'libt3highlight'
excludesrc = '/(Makefile|TODO.*|SciTE.*|test\.c|run|valgrind|debug)$'
auxsources = [ 'src/.objects/*.bytes', 'src.util/.objects/*.bytes', 'src/highlight_api.h',
	'src/highlight_errors.h', 'src/highlight_shared.c' ]
auxfiles = [ 'doc/doxygen.conf', 'doc/DoxygenLayout.xml', 'doc/main_doc.h' ]
extrabuilddirs = [ 'doc' ]

versioninfo = '2:0:0'

def get_replacements(mkdist):
	return [
		{
			'tag': '<VERSION>',
			'replacement': mkdist.version
		},
		{
			'tag': '^#define T3_HIGHLIGHT_VERSION .*',
			'replacement': '#define T3_HIGHLIGHT_VERSION ' + mkdist.get_version_bin(),
			'files': [ 'src/highlight.h' ],
			'regex': True
		},
		{
			'tag': '<OBJECTS>',
			'replacement': " ".join(mkdist.sources_to_objects(mkdist.include_by_regex(mkdist.sources, '^src/'), '\.c$', '.lo')),
			'files': [ 'mk/libt3highlight.in' ]
		},
		{
			'tag': '<OBJECTS>',
			'replacement': " ".join(mkdist.sources_to_objects(mkdist.include_by_regex(mkdist.sources, '^src\.util/'), '\.c$', '.o')),
			'files': [ 'mk/t3highlight.in' ]
		},
		{
			'tag': '<VERSIONINFO>',
			'replacement': versioninfo,
			'files': [ 'mk/libt3highlight.in' ]
		},
		{
			'tag': '<LIBVERSION>',
			'replacement': versioninfo.split(':', 2)[0],
			'files': [ 'Makefile.in', 'mk/*.in' ]
		}
	]

def finalize(mkdist):
	os.symlink('.', os.path.join(mkdist.topdir, 'src', 't3highlight'))
