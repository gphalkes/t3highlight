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

SOURCES.libt3highlight.la := highlight.c vector.c highlight_shared.c io.c

LDLIBS.libt3highlight.la += -lt3config
LDFLAGS.libt3highlight.la += -Lt3config/.libs
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
include ../../shared/rules.mk

all: | links

.objects/%.bytes: %.schema
	$(GENOBJDIR)
	$(_VERBOSE_GEN) t3config/data2bytes -s -- $< > $@

highlight.c: .objects/syntax.bytes .objects/map.bytes

links:
	@[ -L t3highlight ] || ln -s . t3highlight
	@[ -L t3config ] || ln -s ../../config/src t3config

.PHONY: links
