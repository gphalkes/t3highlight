PACKAGE=libt3highlight
SRCDIRS="src src.util"
EXCLUDESRC="/(Makefile|TODO.*|SciTE.*|run\.sh|test\.c)$"
GENSOURCES="`echo src/.objects/*.bytes src.util/.objects/*.bytes` src/highlight_api.h src/highlight_errors.h src/highlight_shared.c"
VERSIONINFO="1:0:0"
