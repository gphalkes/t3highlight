PACKAGE=libt3highlight
SRCDIRS="src"
EXCLUDESRC="/(Makefile|TODO.*|SciTE.*|run\.sh|test\.c)$"
GENSOURCES="`echo src/.objects/*.bytes` src/highlight_api.h src/highlight_errors.h src/highlight_shared.c"
VERSIONINFO="0:0:0"
