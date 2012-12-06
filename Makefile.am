
#
# Copyright (C) 2008 Search Solution Corporation. All rights reserved by Search Solution. 
#
#   This program is free software; you can redistribute it and/or modify 
#   it under the terms of the GNU General Public License as published by 
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version. 
#
#  This program is distributed in the hope that it will be useful, 
#  but WITHOUT ANY WARRANTY; without even the implied warranty of 
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the 
#  GNU General Public License for more details. 
#
#  You should have received a copy of the GNU General Public License 
#  along with this program; if not, write to the Free Software 
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#

AUTOMAKE_OPTIONS = foreign

SUBDIRS = @BUILD_SUBDIRS@

EXTRA_DIST = BUILD_NUMBER COPYING CREDITS README \
	     autogen.sh build.sh @EXTRA_DIST_SUBDIRS@

if BUILD_CMSERVER
  BUILD_CMSERVER="--with-cmserver"
endif

dist-hook: 
	@list='@EXTERNAL_PKGS@'; for subdir in $$list; do \
	  if test -f $(top_srcdir)/external/$$subdir/configure.gnu \
	     -a ! -f $(distdir)/external/$$subdir/configure.gnu; then \
	    cp $(top_srcdir)/external/$$subdir/configure.gnu $(distdir)/external/$$subdir; \
	  fi; \
	done;
	@find $(distdir) -type d -name .svn -print0 | xargs -0 rm -rf

logdir = @CUBRID_LOGDIR@
log_DATA = # EMPTY

tmpdir = @CUBRID_TMPDIR@
tmp_DATA = # EMPTY

$(top_srcdir)/configure.ac: $(top_srcdir)/BUILD_NUMBER
	@touch $(top_srcdir)/configure.ac

# dist packages
BUILD_SH_OPTIONS = -s $(top_srcdir) -b $(top_builddir) -p $(prefix)
if BUILD_64
BUILD_SH_OPTIONS += -t x86_64
else
BUILD_SH_OPTIONS += -t i386
endif
if BUILD_COVERAGE
BUILD_SH_OPTIONS += -m coverage
else
if BUILD_DEBUG
BUILD_SH_OPTIONS += -m debug
else
BUILD_SH_OPTIONS += -m release
endif
endif

dist-src:
	@echo "Build source packages"
	$(top_srcdir)/build.sh $(BUILD_SH_OPTIONS) -z src -z php_src dist

dist-bin: install
	@echo "Build binary packages"
	$(top_srcdir)/build.sh $(BUILD_SH_OPTIONS) -z tarball -z shell -z cci dist

dist-jdbc:
	@echo "Build JDBC packages"
	$(top_srcdir)/build.sh $(BUILD_SH_OPTIONS) -z jdbc dist

dist-rpm:
	@echo "Build RPM packages"
	$(top_srcdir)/build.sh $(BUILD_SH_OPTIONS) -z srpm -z rpm dist

dist-pack: dist-src dist-bin dist-jdbc dist-rpm
.PHONY: dist-pack
