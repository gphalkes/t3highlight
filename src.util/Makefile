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

SOURCES.t3highlight := t3highlight.c escapes.c

TARGETS := t3highlight
#================================================#
# NO RULES SHOULD BE DEFINED BEFORE THIS INCLUDE #
#================================================#
include ../../makesys/rules.mk
#================================================#
include ../../t3shared/rules-base.mk
LDFLAGS.t3highlight := $(T3LDFLAGS.t3config) $(T3LDFLAGS.t3highlight)
LDLIBS.t3highlight := -lt3highlight -lt3config
CFLAGS.t3highlight := -I.objects -DDATADIR='"$(CURDIR)/data"'

CFLAGS += -I. -I.objects
#~ CFLAGS += -DUSE_GETTEXT -DLOCALEDIR=\"locales\"

.objects/t3highlight.o: | lib

.objects/%.bytes: %.schema
	$(GENOBJDIR)
	$(_VERBOSE_GEN) ../../t3config/src/data2bytes -s -- $< > $@

.objects/t3highlight.o: .objects/style.bytes

lib:
	@$(MAKE) -q -C ../src libt3highlight.la || $(MAKE) -C ../src libt3highlight.la

links:
	@[ -L t3config ] || ln -s ../../config/src t3config

clang-format:
	clang-format -i *.c *.h

.PHONY: links clang-format
