# Copyright (C) 2011 G.P. Halkes
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3, as
# published by the Free Software Foundation.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

SOURCES.libt3highlight.la := highlight.c vector.c highlight_shared.c io.c utf8.c match.c analyse.c

LDLIBS.libt3highlight.la += -lt3config
LDFLAGS.libt3highlight.la += $(T3LDFLAGS.t3config)
LDFLAGS.libt3highlight.la += `pkg-config --libs libpcre`
CFLAGS.highlight += -I.objects -DDATADIR='"$(CURDIR)/data"'
CFLAGS.io += -I.objects -DDATADIR='"$(CURDIR)/data"'

LTTARGETS := libt3highlight.la
#================================================#
# NO RULES SHOULD BE DEFINED BEFORE THIS INCLUDE #
#================================================#
include ../../makesys/rules.mk
#================================================#
T3NAME := highlight
include ../../t3shared/rules.mk
include ../../t3shared/rules-base.mk

.objects/%.bytes: %.schema
	$(GENOBJDIR)
	$(_VERBOSE_GEN) ../../t3config/src/data2bytes -s -- $< > $@

highlight.c: .objects/syntax.bytes .objects/map.bytes

clang-format:
	clang-format -i *.c *.h

.PHONY: clang-format
