# Makefile.am for libgpg-error.
# Copyright (C) 2003, 2006, 2007, 2013 g10 Code GmbH
#
# This file is part of libgpg-error.
#
# libgpg-error is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# libgpg-error is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, see <http://www.gnu.org/licenses/>.

ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = dist-bzip2

# (A suitable gitlog-to-changelog script can be found in GnuPG master.)
GITLOG_TO_CHANGELOG=gitlog-to-changelog

EXTRA_DIST = autogen.sh config.rpath libgpg-error.spec.in 		\
             COPYING COPYING.LIB potomo doc/HACKING ChangeLog-2011   	\
	     po/ChangeLog-2011 m4/ChangeLog-2011 contrib/ChangeLog-2011 \
             build-aux/git-log-footer  build-aux/git-log-fix

if LANGUAGES_SOME
lang_subdirs = lang
else
lang_subdirs =
endif

SUBDIRS = m4 src tests po $(lang_subdirs)


dist-hook: gen-ChangeLog
	sed -e 's/@pkg_version@/$(VERSION)/g' \
	    $(top_srcdir)/libgpg-error.spec.in  > $(distdir)/libgpg-error.spec
	@set -e; echo "$(VERSION)" > $(distdir)/VERSION


gen_start_date = 2011-12-01T00:00:00
.PHONY: gen-ChangeLog
gen-ChangeLog:
	set -e;                         				\
	if test -d $(top_srcdir)/.git; then				\
	  (cd $(top_srcdir) &&                      			\
	    $(GITLOG_TO_CHANGELOG) --append-dot --tear-off		\
	    --amend=build-aux/git-log-fix  --tear-off			\
	    --since=$(gen_start_date) ) > $(distdir)/cl-t;		\
          cat $(top_srcdir)/build-aux/git-log-footer >> $(distdir)/cl-t;\
	  rm -f $(distdir)/ChangeLog;					\
	  mv $(distdir)/cl-t $(distdir)/ChangeLog;			\
	fi


if HAVE_W32_SYSTEM
install-data-hook:
	set -e; \
	for i in $$($(top_srcdir)/potomo --get-linguas $(top_srcdir)/po); do \
           $(MKDIR_P) "$(DESTDIR)$(localedir)/$$i/LC_MESSAGES" || true; \
           rm -f "$(DESTDIR)$(localedir)/$$i/LC_MESSAGES/libgpg-error.mo" \
                                                 2>/dev/null || true; \
           $(top_srcdir)/potomo $(top_srcdir)/po/$$i.po \
              "$(DESTDIR)$(localedir)/$$i/LC_MESSAGES/libgpg-error.mo" ; \
	done
endif

stowinstall:
	$(MAKE) $(AM_MAKEFLAGS) install prefix=/usr/local/stow/libgpg-error

