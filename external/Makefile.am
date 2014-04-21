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

AUTOMAKE_OPTIONS = foreign -Wno-override

SUBDIRS = @EXTERNAL_PKGS@

EXTRA_DIST = @EXTERNAL_DIST@

install: all-am
	@echo "Nothing to be installed for external packages."

uninstall: clean-local-check
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  echo "Making uninstall in $$subdir"; \
	  (cd $$subdir && $(MAKE) $(AM_MAKEFLAGS) DESTDIR="" uninstall) \
	done;

all-local:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  if test -f $$subdir/.installed; then \
	    echo "Skip install in $$subdir"; continue; \
	  fi; \
	  echo "Making install in $$subdir"; \
	  (cd $$subdir && $(MAKE) $(AM_MAKEFLAGS) DESTDIR="" install) \
	  && touch $$subdir/.installed || exit 1; \
	done;

clean-local: clean-local-check
.PHONY: clean-local-check
clean-local-check:
	@list='$(SUBDIRS)'; for subdir in $$list; do \
	  (cd $$subdir && rm -f .installed) \
	done;
