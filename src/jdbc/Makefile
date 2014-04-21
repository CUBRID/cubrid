
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

JDBC_SRC_DIR = .

BIN_DIR = bin
SRC_DIR = src

JDBC_BUILD_NUMBER := $(shell cat ../../BUILD_NUMBER)

VERSION_FILE = CUBRID-JDBC-$(JDBC_BUILD_NUMBER)
JDBC_DRIVER = JDBC-$(JDBC_BUILD_NUMBER)-cubrid.jar

all: src/$(VERSION_FILE) $(JDBC_DRIVER)

clean:
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dversion=$(JDBC_BUILD_NUMBER) clean

src/$(VERSION_FILE):
	mkdir -p src
	touch $@

$(JDBC_DRIVER):
	ant -buildfile ../../jdbc/build.xml -Dbasedir=. -Dsrc=$(JDBC_SRC_DIR) -Dversion=$(JDBC_BUILD_NUMBER) dist-cubrid

